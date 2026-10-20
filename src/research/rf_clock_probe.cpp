#include "rf_clock_probe.h"
#include "lab_service.h"
#include "../CONFIG.h"
#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <driver/pcnt.h>
#include <esp_timer.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

namespace research {
namespace {
// TI SWRS061I table41: IOCFG0=3F routes CLK_XOSC/192. Other GDO clock
// selectors must be<30. Clock outputs impair RF, so probe only in SIDLE.
// https://www.ti.com/lit/ds/symlink/cc1101.pdf pp61-62/71.
const pcnt_unit_t kUnit=PCNT_UNIT_7;
volatile bool counterOverflow;
void IRAM_ATTR overflow(void*) { counterOverflow=true; }
struct Cleanup {
    RfClockProbeResult& result;
    bool saved=false, configured=false, handler=false, service=false;
    explicit Cleanup(RfClockProbeResult& value): result(value) {}
    ~Cleanup() {
        if(configured) {
            bool released=pcnt_counter_pause(kUnit)==ESP_OK;
            if(pcnt_intr_disable(kUnit)!=ESP_OK)released=false;
            if(pcnt_event_disable(kUnit,PCNT_EVT_H_LIM)!=ESP_OK)released=false;
            if(handler && pcnt_isr_handler_remove(kUnit)!=ESP_OK)released=false;
            if(pcnt_set_mode(kUnit,PCNT_CHANNEL_0,PCNT_COUNT_DIS,PCNT_COUNT_DIS,PCNT_MODE_KEEP,PCNT_MODE_KEEP)!=ESP_OK)released=false;
            // IDF4 pcnt_set_pin(...,-1,-1) leaves its previous matrix route;
            // explicitly disconnect the reserved channel from GPIO4 instead.
            esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,PCNT_SIG_CH0_IN7_IDX,false);
            if(!released && result.error==RfClockProbeError::None)result.error=RfClockProbeError::Counter;
        }
        if(service)pcnt_isr_service_uninstall();
        if(saved) {
            ELECHOUSE_cc1101.SpiWriteReg(0x02,result.oldIocfg0);
            const uint8_t restored=ELECHOUSE_cc1101.SpiReadReg(0x02);
            result.restored=ELECHOUSE_cc1101.spiReady() && restored==result.oldIocfg0;
            if(!result.restored)result.error=RfClockProbeError::Restore;
        }
        pinMode(CC1101_GDO0,INPUT);
    }
};
bool sample(RfClockSample& output,RfClockProbeError& error) {
    counterOverflow=false;
    if(pcnt_counter_pause(kUnit)!=ESP_OK || pcnt_counter_clear(kUnit)!=ESP_OK) {
        error=RfClockProbeError::Counter;return false;
    }
    const int64_t start=esp_timer_get_time();
    if(pcnt_counter_resume(kUnit)!=ESP_OK){error=RfClockProbeError::Counter;return false;}
    delay(50);
    const esp_err_t paused=pcnt_counter_pause(kUnit);
    const int64_t end=esp_timer_get_time();
    int16_t edges=0;
    if(paused!=ESP_OK || pcnt_get_counter_value(kUnit,&edges)!=ESP_OK) {
        error=RfClockProbeError::Counter;return false;
    }
    if(end<=start || uint64_t(end-start)>200000) {error=RfClockProbeError::Timing;return false;}
    if(counterOverflow || edges<0 || edges==32767){error=RfClockProbeError::Overflow;return false;}
    output.edges=uint32_t(edges);output.elapsedUs=uint32_t(end-start);
    if(!edges){error=RfClockProbeError::NoClock;return false;}
    if(!rfClockEstimate(output.edges,output.elapsedUs,output.oscillatorHz)) {
        error=RfClockProbeError::Timing;return false;
    }
    return true;
}
}
bool radioLabClockProbe(RfClockProbeResult& result) {
    result=RfClockProbeResult{};
    if(labSnapshot().active){result.error=RfClockProbeError::Busy;return false;}
    if(!ELECHOUSE_cc1101.spiReady()){result.error=RfClockProbeError::Spi;return false;}
    {
        Cleanup cleanup(result);
        result.oldIocfg0=ELECHOUSE_cc1101.SpiReadReg(0x02);
        if(!ELECHOUSE_cc1101.spiReady()){result.error=RfClockProbeError::Spi;return false;}
        cleanup.saved=true;
        const uint8_t iocfg2=ELECHOUSE_cc1101.SpiReadReg(0x00);
        const uint8_t iocfg1=ELECHOUSE_cc1101.SpiReadReg(0x01);
        if(!ELECHOUSE_cc1101.spiReady()){result.error=RfClockProbeError::Spi;return false;}
        if((iocfg2&0x3F)>=0x30 || (iocfg1&0x3F)>=0x30){result.error=RfClockProbeError::OtherClock;return false;}
        ELECHOUSE_cc1101.SpiStrobe(0x36); // SIDLE only: no calibration/RX/TX.
        bool idle=false;
        for(unsigned attempt=0;attempt<40;++attempt) {
            if(!ELECHOUSE_cc1101.spiReady())break;
            if((ELECHOUSE_cc1101.SpiReadStatus(0x35)&0x1F)==1){idle=true;break;}
            delay(1);
        }
        if(!idle){result.error=RfClockProbeError::NotIdle;return false;}
        pcnt_config_t config={};config.pulse_gpio_num=CC1101_GDO0;config.ctrl_gpio_num=PCNT_PIN_NOT_USED;
        config.lctrl_mode=config.hctrl_mode=PCNT_MODE_KEEP;
        config.pos_mode=PCNT_COUNT_INC;config.neg_mode=PCNT_COUNT_DIS;
        config.counter_h_lim=32767;config.counter_l_lim=-32768;
        config.unit=kUnit;config.channel=PCNT_CHANNEL_0;
        if(pcnt_unit_config(&config)!=ESP_OK){result.error=RfClockProbeError::Counter;return false;}
        cleanup.configured=true;
        if(pcnt_counter_pause(kUnit)!=ESP_OK || pcnt_filter_disable(kUnit)!=ESP_OK ||
           pcnt_set_mode(kUnit,PCNT_CHANNEL_1,PCNT_COUNT_DIS,PCNT_COUNT_DIS,PCNT_MODE_KEEP,PCNT_MODE_KEEP)!=ESP_OK) {
            result.error=RfClockProbeError::Counter;return false;
        }
        const esp_err_t installed=pcnt_isr_service_install(ESP_INTR_FLAG_IRAM);
        if(installed!=ESP_OK && installed!=ESP_ERR_INVALID_STATE){result.error=RfClockProbeError::Counter;return false;}
        cleanup.service=installed==ESP_OK;
        if(pcnt_isr_handler_add(kUnit,overflow,nullptr)!=ESP_OK){result.error=RfClockProbeError::Counter;return false;}
        cleanup.handler=true;
        if(pcnt_event_enable(kUnit,PCNT_EVT_H_LIM)!=ESP_OK || pcnt_intr_enable(kUnit)!=ESP_OK){result.error=RfClockProbeError::Counter;return false;}
        ELECHOUSE_cc1101.SpiWriteReg(0x02,0x3F);
        if(!ELECHOUSE_cc1101.spiReady() || ELECHOUSE_cc1101.SpiReadReg(0x02)!=0x3F) {
            result.error=RfClockProbeError::Spi;return false;
        }
        for(unsigned i=0;i<3;++i)if(!sample(result.samples[i],result.error))return false;
        result.medianHz=rfClockMedian(result.samples[0].oscillatorHz,result.samples[1].oscillatorHz,result.samples[2].oscillatorHz);
        if(result.medianHz<20000000 || result.medianHz>30000000)result.error=RfClockProbeError::Implausible;
        for(unsigned i=0;i<3;++i) {
            const uint32_t hz=result.samples[i].oscillatorHz;
            const uint32_t difference=hz>result.medianHz?hz-result.medianHz:result.medianHz-hz;
            if(difference>result.medianHz/100)result.error=RfClockProbeError::Implausible;
        }
    }
    return result.error==RfClockProbeError::None && result.restored;
}
const char* rfClockProbeErrorText(RfClockProbeError error) {
    switch(error) {
    case RfClockProbeError::None:return "OK";case RfClockProbeError::Busy:return "radio worker active";
    case RfClockProbeError::Spi:return "SPI not ready/readback failed";case RfClockProbeError::NotIdle:return "SIDLE timeout";
    case RfClockProbeError::OtherClock:return "another GDO routes XOSC clock";case RfClockProbeError::Counter:return "PCNT setup/read failed";
    case RfClockProbeError::Overflow:return "PCNT overflow";case RfClockProbeError::NoClock:return "no clock on GPIO4";
    case RfClockProbeError::Timing:return "invalid/stalled/long timer interval";case RfClockProbeError::Implausible:return "unstable/out-of-range clock";
    case RfClockProbeError::Restore:return "IOCFG0 restore failed";
    }
    return "unknown";
}
}
