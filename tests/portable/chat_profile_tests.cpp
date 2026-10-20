#include "chat_profile.h"
#include "lab_limits.h"
#include "chat_channel.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace research;
static unsigned checks;
#define CHECK(test) do { ++checks; if(!(test)) { std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#test); return 1; } } while(0)
int main() {
    RegisterImage image; RadioSettings settings; const char* error=0;
    // Reproduce the actual startup failure: registers alone are not a preset.
    for(size_t i=0;i<FLIPPER_CHAT_REGISTER_BYTES;i+=2) image.set(flipper_chat_registers[i],flipper_chat_registers[i+1]);
    CHECK(!describeRadio(image,433920000,settings,&error));
    CHECK(error&&std::strstr(error,"PA table"));
    CHECK(makeFlipperChatProfile(image,settings,&error));
    CHECK(!error&&validateRegisterImage(image)); CHECK(image.paPresent);
    for(unsigned i=0;i<8;++i)CHECK(image.pa[i]==0);
    CHECK(settings.frequencyHz==433920000); CHECK(settings.modulation==Modulation::Gfsk);
    CHECK(settings.packetFormat==PacketFormat::Fifo&&settings.packetLength==60);
    CHECK(settings.syncWord==0x464C&&settings.syncMode==2&&settings.crc);
    CHECK(!settings.whitening&&!settings.manchester&&!settings.fec);
    CHECK(std::fabs(settings.dataRateBaud-9992.5994873046875)<0.001);
    CHECK(std::fabs(settings.bandwidthHz-101562.5)<0.001);
    CHECK(settings.txLimitMs==1000&&settings.rxTimeoutMs==0);
    CHECK(settings.rssiThresholdDbm==-75);
    CHECK(!chatChannelBusy(-88,settings.rssiThresholdDbm,false));
    CHECK(chatChannelBusy(-44,settings.rssiThresholdDbm,false));
    CHECK(!chatChannelBusy(-76,-75,false));
    CHECK(chatChannelBusy(-75,-75,false));
    CHECK(chatChannelBusy(-74,-75,false));
    // Exhaust every signed RSSI representation, including invalid/extreme
    // samples. Sync must block regardless of received power or threshold.
    static const int16_t thresholds[]={INT16_MIN,-120,-100,-85,-75,-40,0,INT16_MAX};
    for(int32_t rssi=INT16_MIN;rssi<=INT16_MAX;++rssi)for(int16_t threshold:thresholds) {
        CHECK(chatChannelBusy(int16_t(rssi),threshold,false)==(rssi>=threshold));
        CHECK(chatChannelBusy(int16_t(rssi),threshold,true));
    }
    CHECK(!permitLabTx(TxProfile::ReceiveOnly,settings.frequencyHz,100,settings.txLimitMs));
    // General settings require a finite deadline; only the interactive host
    // deliberately uses zero. Its worker does not call encodeRadioSettings.
    settings.rxTimeoutMs=5000; CHECK(validateSettings(settings));
    // Reusing the helper must clear stale PA/register data from another preset.
    CHECK(loadPreset("AM270",image)); image.pa[3]=0xC0; image.set(0x2E,0x09);
    CHECK(makeFlipperChatProfile(image,settings));
    for(unsigned i=0;i<8;++i)CHECK(image.pa[i]==0);
    CHECK(!image.present[0]&&!image.present[0x2E]);
    for(size_t i=0;i<FLIPPER_CHAT_REGISTER_BYTES;i+=2) {
        const uint8_t address=flipper_chat_registers[i];
        CHECK(image.present[address]&&image.value[address]==flipper_chat_registers[i+1]);
    }
    std::printf("automatic chat profile: %u checks passed\n",checks);
}
