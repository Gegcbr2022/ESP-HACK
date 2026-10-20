#include "radio_config.h"
#include "lab_limits.h"
#include "trim_math.h"
#include "raw_radio_restore.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace research;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static const unsigned CC1101_GDO0=4, BUTTON_BACK=27;
static const int INPUT=0,LOW=0,ESP_OK=0,ESP_ERR_TIMEOUT=1,RMT_CHANNEL_0=0;
static const int RMT_IDLE_LEVEL_LOW=0,MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static int lock;
struct LabSnapshot {uint32_t spiErrors=0;uint8_t registers[47]={};};
static LabSnapshot state;
static RadioSettings activeSettings;
static bool stopRequested=false,syncSeen=false;
static uint32_t rxArmedAt=0,syncStarted=0,clockUs=0;
static std::string lastStatus;
static unsigned stopCalls,configureCalls,txStrobes,rmtStops,rmtUninstalls,rxEntries;
static uint32_t transmittedWord,configureHz;
static bool workerStopped=true,failConfigure=false,failIdle=false,failPower=false;
static bool failAlloc=false,failConfig=false,failInstall=false,failTxState=false;
static bool failWrite=false,failPoll=false,cancelBack=false,cancelStop=false,runTimeout=false;
static bool failRmtStop=false,failUninstall=false,driverInstalled=false,restorationPhase=false;
static unsigned restoreFailAt=0,restoreIo=0,snapshotFailAt=0,snapshotIo=0;
static bool permanentSpi=false;
static bool corruptRestoreReg=false,corruptRestorePa=false;
struct Reg {uint32_t val=0;};
struct WriteOne {
    uint32_t* target;bool set;
    void operator=(uint32_t value){if(set)*target|=value;else *target&=~value;}
};
struct MockGpio {
    uint32_t enable=0,out=0;
    WriteOne enable_w1ts{&enable,true},enable_w1tc{&enable,false};
    WriteOne out_w1ts{&out,true},out_w1tc{&out,false};
    Reg pin[40],func_out_sel_cfg[40];
} GPIO;
static uint32_t mux[40];
static const uint32_t GPIO_PIN_MUX_REG[40]={0,1,2,3,4};
#define REG_READ(address) (mux[(address)])
#define REG_WRITE(address,value) (mux[(address)]=(value))
static void pinMode(unsigned pin,int){GPIO.enable&=~(1UL<<pin);mux[pin]=0xA5;GPIO.func_out_sel_cfg[pin].val=0x100;}
struct Driver {
    uint8_t regs[47]={},pa[8]={},marc=13;
    uint32_t timeouts=0;bool sticky=false;
    bool io() {
        bool fail=permanentSpi;
        if(restorationPhase){++restoreIo;if(restoreFailAt&&restoreIo==restoreFailAt)fail=true;}
        else if(!configureCalls){++snapshotIo;if(snapshotFailAt&&snapshotIo==snapshotFailAt)fail=true;}
        if(fail){if(timeouts!=UINT32_MAX)++timeouts;sticky=true;return false;}
        return true;
    }
    bool spiReady()const{return !sticky;}
    uint32_t spiTimeouts()const{return timeouts;}
    uint8_t SpiReadReg(uint8_t address){return io()?regs[address]:0;}
    uint8_t SpiReadStatus(uint8_t){return io()?marc:0;}
    void SpiWriteReg(uint8_t address,uint8_t value){if(io())regs[address]=restorationPhase&&corruptRestoreReg&&address==13?uint8_t(value^1):value;}
    void SpiReadBurstReg(uint8_t,uint8_t* bytes,uint8_t count){std::memset(bytes,0,count);if(io())std::memcpy(bytes,pa,count);}
    void SpiWriteBurstReg(uint8_t,uint8_t* bytes,uint8_t count){if(io()){std::memcpy(pa,bytes,count);if(restorationPhase&&corruptRestorePa&&bytes[0])pa[0]^=1;}}
    void SpiStrobe(uint8_t command){
        if(!io())return;
        if(command==0x36)marc=1;
        if(command==0x34){
            ++rxEntries;for(unsigned i=0;i<8;++i)CHECK(pa[i]==0);
            CHECK((regs[0x18]&0x30)==0);marc=13;
        }
        if(command==0x35){++txStrobes;marc=19;transmittedWord=(uint32_t(regs[13])<<16)|(uint32_t(regs[14])<<8)|regs[15];}
    }
} ELECHOUSE_cc1101;
static uint8_t stableStatus(uint8_t address){return ELECHOUSE_cc1101.SpiReadStatus(address);}
static void status(const char* text){lastStatus=text;}
static void errorCount(uint32_t LabSnapshot::*field){++(state.*field);}
static uint32_t micros(){clockUs+=100;return clockUs;}
static void delay(unsigned ms){clockUs+=ms*1000;}
static int digitalRead(unsigned pin){CHECK(pin==BUTTON_BACK);return cancelBack?LOW:1;}
static bool labStop(){++stopCalls;return workerStopped;}
static bool idle(){ELECHOUSE_cc1101.SpiStrobe(0x36);if(failIdle){failIdle=false;return false;}return true;}
static bool waitState(uint8_t wanted,uint32_t){CHECK(wanted==19);return !failTxState;}
static bool configure(const RegisterImage& image,uint32_t hz){
    ++configureCalls;configureHz=hz;activeSettings.frequencyHz=hz;rxArmedAt=888;syncSeen=false;syncStarted=999;
    // The real configurator resets hardware and applies calibration. Model that
    // mutation so every early return has a changed physical image to undo.
    for(unsigned i=0;i<47;++i)ELECHOUSE_cc1101.regs[i]=image.present[i]?image.value[i]:uint8_t(i*7);
    std::memset(ELECHOUSE_cc1101.pa,0,8);ELECHOUSE_cc1101.marc=13;
    uint32_t programmed=0;CHECK(trimmedFrequencyHz(hz,115,programmed));
    const uint32_t word=cc1101FrequencyWord(programmed);
    ELECHOUSE_cc1101.regs[13]=word>>16;ELECHOUSE_cc1101.regs[14]=word>>8;ELECHOUSE_cc1101.regs[15]=word;
    pinMode(CC1101_GDO0,INPUT);
    if(failConfigure){ELECHOUSE_cc1101.sticky=true;++ELECHOUSE_cc1101.timeouts;restorationPhase=true;return false;}
    return true;
}
static void powerTable(uint32_t,uint8_t,bool){
    ELECHOUSE_cc1101.pa[1]=0xC0;
    if(failPower){ELECHOUSE_cc1101.sticky=true;++ELECHOUSE_cc1101.timeouts;restorationPhase=true;}
}
struct rmt_item32_t {uint16_t duration0,duration1;uint8_t level0,level1;};
static rmt_item32_t bank[2048],*rmtItems=nullptr;
static void* heap_caps_calloc(size_t n,size_t size,int){CHECK(n*size==sizeof(bank));return failAlloc?nullptr:bank;}
struct rmt_config_t {int clk_div,mem_block_num;struct {bool idle_output_en;int idle_level;}tx_config;};
using gpio_num_t=int;using esp_err_t=int;
static rmt_config_t RMT_DEFAULT_CONFIG_TX(gpio_num_t,int){return rmt_config_t{};}
static int rmt_config(const rmt_config_t*){
    GPIO.enable|=1UL<<CC1101_GDO0;GPIO.out&=~(1UL<<CC1101_GDO0);mux[4]=77;GPIO.pin[4].val=88;GPIO.func_out_sel_cfg[4].val=99;
    if(failConfig){restorationPhase=true;return -1;}return ESP_OK;
}
static int rmt_driver_install(int,int,int){if(failInstall){restorationPhase=true;return -1;}driverInstalled=true;return ESP_OK;}
static int rmt_write_items(int,rmt_item32_t*,size_t,bool){if(failWrite){restorationPhase=true;return -1;}return ESP_OK;}
static int rmt_wait_tx_done(int,int){
    if(cancelStop)stopRequested=true;
    if(runTimeout)clockUs+=6000000;
    if(failPoll){restorationPhase=true;return -1;}
    return cancelBack||cancelStop||runTimeout?ESP_ERR_TIMEOUT:ESP_OK;
}
static int rmt_tx_stop(int){++rmtStops;restorationPhase=true;return failRmtStop?-1:ESP_OK;}
static int rmt_driver_uninstall(int){++rmtUninstalls;driverInstalled=false;return failUninstall?-1:ESP_OK;}
// PRODUCTION_FUNCTIONS
struct Expected {uint8_t regs[47],pa[8],marc;uint32_t mux,pin,outSelect,enable,out;};
static Expected original;
static void reset(uint8_t prior=13){
    ELECHOUSE_cc1101=Driver();state=LabSnapshot();
    for(unsigned i=0;i<47;++i)ELECHOUSE_cc1101.regs[i]=uint8_t(3+i*5);
    for(unsigned i=0;i<8;++i)ELECHOUSE_cc1101.pa[i]=uint8_t(0xA0+i);
    // Deliberately already calibrated/quantized; restoring via Hz+trim again is wrong.
    uint32_t programmed=0;CHECK(trimmedFrequencyHz(433920000,115,programmed));
    const uint32_t word=cc1101FrequencyWord(programmed);
    ELECHOUSE_cc1101.regs[13]=word>>16;ELECHOUSE_cc1101.regs[14]=word>>8;ELECHOUSE_cc1101.regs[15]=word;
    ELECHOUSE_cc1101.regs[0x18]=0x38;ELECHOUSE_cc1101.marc=prior;
    GPIO.enable=0x22334450;GPIO.out=0x55667790;mux[4]=0x2A0;
    GPIO.pin[4].val=0xB4;GPIO.func_out_sel_cfg[4].val=0x100;
    std::memcpy(original.regs,ELECHOUSE_cc1101.regs,47);std::memcpy(original.pa,ELECHOUSE_cc1101.pa,8);
    original.marc=prior;original.mux=mux[4];original.pin=GPIO.pin[4].val;original.outSelect=GPIO.func_out_sel_cfg[4].val;
    original.enable=GPIO.enable;original.out=GPIO.out;
    activeSettings=RadioSettings();activeSettings.frequencyHz=433920000;stopRequested=true;syncSeen=true;rxArmedAt=123;syncStarted=456;
    workerStopped=true;failConfigure=failIdle=failPower=failAlloc=failConfig=failInstall=failTxState=false;
    failWrite=failPoll=cancelBack=cancelStop=runTimeout=failRmtStop=failUninstall=driverInstalled=restorationPhase=permanentSpi=false;
    corruptRestoreReg=corruptRestorePa=false;
    restoreFailAt=restoreIo=snapshotFailAt=snapshotIo=stopCalls=configureCalls=txStrobes=rmtStops=rmtUninstalls=rxEntries=0;
    configureHz=transmittedWord=clockUs=0;rmtItems=nullptr;lastStatus.clear();
}
static void checkPin(){CHECK(mux[4]==original.mux);CHECK(GPIO.pin[4].val==original.pin);CHECK(GPIO.func_out_sel_cfg[4].val==original.outSelect);CHECK(GPIO.enable==original.enable);CHECK(GPIO.out==original.out);}
static void checkRestored(){
    CHECK(!std::memcmp(original.regs,ELECHOUSE_cc1101.regs,47));CHECK(!std::memcmp(original.pa,ELECHOUSE_cc1101.pa,8));
    CHECK(ELECHOUSE_cc1101.marc==original.marc);checkPin();
    CHECK(activeSettings.frequencyHz==433920000&&stopRequested&&syncSeen&&rxArmedAt==123&&syncStarted==456);
    CHECK(!driverInstalled);
}
static bool transmit(uint32_t hz=906400000){
    RegisterImage image;CHECK(loadPreset("AM270",image));
    const int32_t pulses[]={500,-1000,500,-1000};uint32_t elapsed=123;int32_t error=123;
    return labTransmitRaw(pulses,4,image,hz,TxProfile::FilePlayback,1,5000,elapsed,error);
}
int main(){
    for(uint8_t prior:{uint8_t(1),uint8_t(13)}){
        reset(prior);CHECK(transmit());checkRestored();CHECK(txStrobes==1&&rmtStops==1&&rmtUninstalls==1);
        uint32_t programmed=0;CHECK(trimmedFrequencyHz(906400000,115,programmed));CHECK(transmittedWord==cc1101FrequencyWord(programmed));
        CHECK(configureHz==906400000);CHECK(rxEntries==(prior==13?1U:0U));
    }
    bool* failures[]={&failConfigure,&failIdle,&failPower,&failAlloc,&failConfig,&failInstall,&failTxState,&failWrite,&failPoll,&cancelBack,&cancelStop,&runTimeout};
    for(size_t i=0;i<sizeof(failures)/sizeof(*failures);++i){reset();*failures[i]=true;CHECK(!transmit());checkRestored();if(i==0||i==2)CHECK(!ELECHOUSE_cc1101.spiReady());}
    for(uint8_t prior:{uint8_t(19),uint8_t(18),uint8_t(0),uint8_t(8)}){reset(prior);CHECK(!transmit());CHECK(ELECHOUSE_cc1101.marc==1);CHECK(!configureCalls&&!txStrobes);checkPin();}
    reset();workerStopped=false;CHECK(!transmit());CHECK(!configureCalls&&!txStrobes);checkRestored();
    reset();ELECHOUSE_cc1101.sticky=true;CHECK(!transmit());CHECK(!configureCalls&&!txStrobes);checkRestored();
    reset();ELECHOUSE_cc1101.timeouts=UINT32_MAX;CHECK(!transmit());CHECK(!configureCalls&&!txStrobes);checkRestored();
    // Every snapshot transaction may fail; no transmit/configure follows it.
    reset();CHECK(transmit());const unsigned snapshotCount=snapshotIo;
    for(unsigned i=1;i<=snapshotCount;++i){reset();snapshotFailAt=i;CHECK(!transmit());CHECK(!configureCalls&&!txStrobes);checkPin();}
    // Inject a fresh intermittent failure at each restoration I/O boundary.
    // Restoration must never claim success; it leaves PA zero, IDLE, GPIO saved.
    reset();CHECK(transmit());const unsigned restorationCount=restoreIo;
    for(unsigned i=1;i<=restorationCount;++i){
        reset();restoreFailAt=i;CHECK(!transmit());CHECK(lastStatus=="Radio restore failed; RX stopped");
        CHECK(ELECHOUSE_cc1101.marc==1);for(unsigned j=0;j<8;++j)CHECK(ELECHOUSE_cc1101.pa[j]==0);checkPin();
        CHECK(!ELECHOUSE_cc1101.spiReady());CHECK(!driverInstalled);
        CHECK(activeSettings.frequencyHz==433920000&&stopRequested&&syncSeen);
    }
    for(bool* failed:{&failRmtStop,&failUninstall}){reset();*failed=true;CHECK(!transmit());CHECK(ELECHOUSE_cc1101.marc==1);checkPin();}
    for(bool* corrupt:{&corruptRestoreReg,&corruptRestorePa}){
        reset();*corrupt=true;CHECK(!transmit());CHECK(lastStatus=="Radio restore failed; RX stopped");
        CHECK(ELECHOUSE_cc1101.marc==1&&ELECHOUSE_cc1101.spiReady());
        for(unsigned i=0;i<8;++i)CHECK(ELECHOUSE_cc1101.pa[i]==0);
        checkPin();
    }
    reset();clockUs=UINT32_MAX-2500;CHECK(transmit());checkRestored();
    // Guard also restores on scope exit without an explicit finish call.
    reset();{RawRestoreBackend backend;RawRadioRestore<RawRestoreBackend> restore(backend);CHECK(restore.capture());restore.mutating();ELECHOUSE_cc1101.regs[13]^=0xFF;}
    checkRestored();
    std::printf("RAW physical restore: %u checks passed (actual TX body/backend, physical-register/PA/GPIO/state doubles)\n",checks);
}
