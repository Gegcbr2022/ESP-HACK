#include "rf_clock_probe.h"
#include "lab_service.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <driver/pcnt.h>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace research;
struct Mock {
    uint8_t regs[47];bool busy,spiFault,notIdle,noClock,overflow,restoreFail,timerStalled;
    bool configFail,pauseFail,resumeFail,readFail,filterFail,handlerFail,installFail;
    bool configured,running,connected,service,handler,uninstalled;
    int pinMode,delays,resumes,pauseCalls,pauseFailAt;int64_t now;uint32_t oscillator,forcedUs;
    int16_t count;void(*callback)(void*);void* callbackContext;
    std::vector<unsigned> writes,strobes;
    Mock():busy(false),spiFault(false),notIdle(false),noClock(false),overflow(false),restoreFail(false),timerStalled(false),
        configFail(false),pauseFail(false),resumeFail(false),readFail(false),filterFail(false),handlerFail(false),installFail(false),
        configured(false),running(false),connected(false),service(false),handler(false),uninstalled(false),
        pinMode(-1),delays(0),resumes(0),pauseCalls(0),pauseFailAt(0),now(0),oscillator(26000000),forcedUs(0),count(0),callback(nullptr),callbackContext(nullptr) {
        memset(regs,0,sizeof regs);regs[0]=0x29;regs[1]=0x2E;regs[2]=0x0D;
    }
} mock;
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return false;}}while(0)
FakeClockRadio ELECHOUSE_cc1101;
bool FakeClockRadio::spiReady()const{return !mock.spiFault;}
uint8_t FakeClockRadio::SpiReadReg(uint8_t a){return mock.regs[a];}
uint8_t FakeClockRadio::SpiReadStatus(uint8_t){return mock.notIdle?13:1;}
void FakeClockRadio::SpiWriteReg(uint8_t a,uint8_t v){mock.writes.push_back(a);if(!(mock.restoreFail&&v==0x0D))mock.regs[a]=v;}
void FakeClockRadio::SpiStrobe(uint8_t command){mock.strobes.push_back(command);}
namespace research {LabSnapshot labSnapshot(){LabSnapshot s={};s.active=mock.busy;return s;}}
int64_t esp_timer_get_time(){return mock.timerStalled?0:mock.now;}
void pinMode(int,int mode){mock.pinMode=mode;}
void delay(uint32_t ms){
    ++mock.delays;const uint32_t us=mock.forcedUs?mock.forcedUs:ms*1000;mock.now+=us;
    if(mock.running&&mock.regs[2]==0x3F){
        mock.count=mock.noClock?0:int16_t(uint64_t(mock.oscillator)*us/192000000);
        if(mock.overflow&&mock.callback)mock.callback(mock.callbackContext);
    }
}
esp_err_t pcnt_unit_config(const pcnt_config_t* c){
    if(mock.configFail)return ESP_FAIL;
    if(c->unit!=7||c->channel!=0||c->pulse_gpio_num!=4||c->pos_mode!=PCNT_COUNT_INC||c->neg_mode!=PCNT_COUNT_DIS)return ESP_FAIL;
    mock.configured=mock.connected=true;return ESP_OK;
}
esp_err_t pcnt_counter_pause(int){++mock.pauseCalls;mock.running=false;return mock.pauseFail||mock.pauseFailAt==mock.pauseCalls?ESP_FAIL:ESP_OK;}
esp_err_t pcnt_counter_resume(int){++mock.resumes;if(mock.resumeFail)return ESP_FAIL;mock.running=true;return ESP_OK;}
esp_err_t pcnt_counter_clear(int){mock.count=0;return ESP_OK;}
esp_err_t pcnt_get_counter_value(int,int16_t* n){*n=mock.count;return mock.readFail?ESP_FAIL:ESP_OK;}
esp_err_t pcnt_intr_disable(int){return ESP_OK;} esp_err_t pcnt_intr_enable(int){return ESP_OK;}
esp_err_t pcnt_filter_disable(int){return mock.filterFail?ESP_FAIL:ESP_OK;}
esp_err_t pcnt_event_disable(int,int){return ESP_OK;}esp_err_t pcnt_event_enable(int,int){return ESP_OK;}
esp_err_t pcnt_set_mode(int,int,int,int,int,int){return ESP_OK;}
esp_err_t pcnt_isr_handler_add(int,void(*fn)(void*),void* ctx){if(mock.handlerFail)return ESP_FAIL;mock.callback=fn;mock.callbackContext=ctx;mock.handler=true;return ESP_OK;}
esp_err_t pcnt_isr_handler_remove(int){mock.handler=false;mock.callback=nullptr;return ESP_OK;}
esp_err_t pcnt_isr_service_install(int){if(mock.installFail)return ESP_FAIL;if(mock.service)return ESP_ERR_INVALID_STATE;mock.service=true;return ESP_OK;}
void pcnt_isr_service_uninstall(){mock.service=false;mock.uninstalled=true;}
void esp_rom_gpio_connect_in_signal(uint32_t pin,uint32_t signal,bool){if(pin==0x30&&signal==79)mock.connected=false;}
static bool clean(const RfClockProbeResult& r,bool previousService=false){
    CHECK(r.restored);CHECK(mock.regs[2]==0x0D);CHECK(!mock.running);CHECK(!mock.connected);CHECK(!mock.handler);
    CHECK(mock.service==previousService);CHECK(mock.pinMode==0);
    for(size_t i=0;i<mock.writes.size();++i)CHECK(mock.writes[i]==0x02);
    for(size_t i=0;i<mock.strobes.size();++i)CHECK(mock.strobes[i]==0x36);
    return true;
}
static bool tests(){
    uint32_t hz=123;
    CHECK(rfClockEstimate(6771,50000,hz)&&hz==26000640);CHECK(rfClockEstimate(7031,50000,hz)&&hz==26999040);
    CHECK(rfClockEstimateInterval(6771,UINT32_MAX-24999,25000,hz)&&hz==26000640);
    const uint32_t invalid[][2]={{0,50000},{32767,50000},{UINT32_MAX,50000},{1,0},{1,999},{1,200001},{32766,1000}};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;++i){hz=123;CHECK(!rfClockEstimate(invalid[i][0],invalid[i][1],hz));CHECK(hz==123);}
    CHECK(rfClockMedian(1,3,2)==2);CHECK(rfClockMedian(3,1,2)==2);CHECK(rfClockMedian(UINT32_MAX,0,1)==1);
    RfClockProbeResult r;
    for(unsigned oscillator=26000000;oscillator<=27000000;oscillator+=1000000){
        mock=Mock();mock.oscillator=oscillator;CHECK(radioLabClockProbe(r));CHECK(clean(r));CHECK(mock.resumes==3);CHECK(mock.delays==3);
        CHECK(r.medianHz>oscillator-4000&&r.medianHz<oscillator+4000);CHECK(mock.now==150000);
    }
    mock=Mock();mock.service=true;CHECK(radioLabClockProbe(r));CHECK(clean(r,true));CHECK(!mock.uninstalled);
    mock=Mock();mock.busy=true;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::Busy);CHECK(mock.writes.empty()&&mock.strobes.empty()&&mock.pinMode==-1);
    mock=Mock();mock.spiFault=true;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::Spi);CHECK(mock.writes.empty()&&mock.pinMode==-1);
    mock=Mock();mock.regs[0]=0x30;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::OtherClock);CHECK(clean(r));
    mock=Mock();mock.notIdle=true;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::NotIdle);CHECK(clean(r));CHECK(mock.delays==40);
    mock=Mock();mock.noClock=true;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::NoClock);CHECK(clean(r));
    mock=Mock();mock.overflow=true;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::Overflow);CHECK(clean(r));
    mock=Mock();mock.forcedUs=250001;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::Timing);CHECK(clean(r));
    mock=Mock();mock.timerStalled=true;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::Timing);CHECK(clean(r));CHECK(mock.delays==1);
    mock=Mock();mock.pauseFailAt=8;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::Counter);CHECK(clean(r));CHECK(mock.resumes==3);
    mock=Mock();mock.oscillator=19000000;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::Implausible);CHECK(clean(r));
    mock=Mock();mock.restoreFail=true;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::Restore);CHECK(!r.restored&&!mock.running&&!mock.connected&&!mock.handler);
    bool Mock::* faults[]={&Mock::configFail,&Mock::pauseFail,&Mock::resumeFail,&Mock::readFail,&Mock::filterFail,&Mock::handlerFail,&Mock::installFail};
    for(size_t i=0;i<sizeof faults/sizeof *faults;++i){mock=Mock();mock.*faults[i]=true;CHECK(!radioLabClockProbe(r)&&r.error==RfClockProbeError::Counter);CHECK(clean(r));}
    std::printf("RF clock probe: %u checks passed (real probe source, mock-only hardware)\n",checks);return true;
}
int main(){return tests()?0:1;}
