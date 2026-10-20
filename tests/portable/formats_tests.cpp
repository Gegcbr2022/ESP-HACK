#include "radio_config.h"
#include "sub_file.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>

using namespace research;
static unsigned checks = 0;
#define CHECK(test) do { ++checks; if(!(test)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#test); return false; } } while(0)
static const char* header = "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 433920000\nPreset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\n";
static bool append(void* context, const char* bytes, size_t length) {
    static_cast<std::string*>(context)->append(bytes, length); return true;
}
static bool rejectPulse(void*, int32_t) { return false; }
static bool parse(const std::string& text, SubError expected = SubError::None) {
    SubParser parser;
    const bool result = parser.feed(text.data(), text.size()) && parser.finish();
    if(result != (expected == SubError::None) || parser.error() != expected) {
        fprintf(stderr, "Unexpected result %d error %d expected %d (%s)\n", result,
                int(parser.error()), int(expected), parser.errorText()); return false;
    }
    return true;
}
static bool testPresets() {
    CHECK(presetCount() == 5);
    CHECK(!presetAt(999)); CHECK(!findPreset("FM999"));
    const uint8_t cfg4[] = {0x67,0x17,0x67,0x67,0x67};
    const uint8_t rateM[] = {0x32,0x32,0x83,0x83,0x83};
    const uint8_t dev[] = {0,0,0x04,0x47,0x30};
    for(size_t i = 0; i < presetCount(); ++i) {
        const RadioPreset* p = presetAt(i);
        RegisterImage image;
        CHECK(loadPreset(p->name, image));
        CHECK(image.value[0x10] == cfg4[i]); CHECK(image.value[0x11] == rateM[i]);
        CHECK(image.value[0x08] == 0x32); CHECK(image.value[0x02] == 0x0D);
        CHECK(!image.present[0x0D]);
        CHECK(image.pa[i < 2 ? 1 : 0] == 0xC0);
        if(i >= 2) CHECK(image.value[0x15] == dev[i]);
        RadioSettings s;
        CHECK(describeRadio(image,433920000,s));
        CHECK(s.packetFormat == PacketFormat::Async); CHECK(!s.crc);
        CHECK(fabs(s.bandwidthHz - (i == 1 ? 650000.0 : 270833.333333)) < 0.01);
        CHECK(fabs(s.dataRateBaud - (i < 2 ? 3793.716430664 : 4797.93548584)) < .01);
        uint8_t bytes[kMaxPresetBytes];
        const size_t length = encodeCustomPreset(image,bytes,sizeof bytes);
        CHECK(length == p->size);
        RegisterImage again;
        CHECK(decodeCustomPreset(bytes,length,again));
        CHECK(!memcmp(image.value,again.value,sizeof image.value));
        CHECK(!memcmp(image.present,again.present,sizeof image.present));
        CHECK(!memcmp(image.pa,again.pa,8));
        CHECK(!encodeCustomPreset(image,bytes,length-1));
    }
    CHECK(supportedFrequency(300000000)); CHECK(!supportedFrequency(299999999));
    CHECK(!supportedFrequency(348000001)); CHECK(!supportedFrequency(465000000));
    CHECK(supportedFrequency(779000000)); CHECK(supportedFrequency(928000000));
    CHECK(!supportedFrequency(928000001));
    CHECK(fabs(registerDeviation(4) - 2380.37109375) < 1e-8);
    CHECK(fabs(registerDeviation(0x47) - 47607.421875) < 1e-8);
    return true;
}
static bool testSettings() {
    RegisterImage image;
    RadioSettings requested, actual;
    const char* error = 0;
    CHECK(encodeRadioSettings(requested,image,&actual,&error));
    CHECK(fabs(actual.dataRateBaud - 9595.870971679688) < .0001);
    CHECK(fabs(actual.bandwidthHz - 203125.0) < .0001);
    CHECK(image.value[0x12] == 0x02); CHECK(image.value[0x08] == 0x05);
    CHECK(image.value[0x17] == 0x3C); CHECK(image.value[0x02] == 0x06);
    for(unsigned i = 0; i < 8; ++i) CHECK(image.pa[i] == 0);
    const uint32_t word = (uint32_t(image.value[0x0D]) << 16) |
        (uint32_t(image.value[0x0E]) << 8) | image.value[0x0F];
    CHECK(word == 1093745UL);
    CHECK(!error);
    requested.modulation = Modulation::Msk;
    CHECK(!encodeRadioSettings(requested,image,&actual,&error));
    requested.dataRateBaud = 99975;
    CHECK(encodeRadioSettings(requested,image,&actual,&error));
    CHECK(image.value[0x12] == 0x72);
    requested.packetFormat = PacketFormat::Async; requested.crc = false; requested.syncMode = 0;
    CHECK(!encodeRadioSettings(requested,image));
    const uint32_t edges[] = {300000000UL,348000000UL,387000000UL,464000000UL,779000000UL,928000000UL};
    for(unsigned i=0;i<sizeof edges/sizeof edges[0];++i) {
        requested = RadioSettings(); requested.frequencyHz = edges[i];
        CHECK(encodeRadioSettings(requested,image,&actual));
        CHECK(supportedFrequency(actual.frequencyHz));
        CHECK(actual.frequencyHz > edges[i]-400 && actual.frequencyHz < edges[i]+400);
    }
    requested = RadioSettings(); requested.packetFormat = PacketFormat::Async;
    requested.crc = false; requested.syncMode = 0;
    requested.modulation = Modulation::AskOok;
    CHECK(encodeRadioSettings(requested,image));
    requested.crc = true; CHECK(!encodeRadioSettings(requested,image));
    requested = RadioSettings(); requested.bandwidthHz = 58036; requested.deviationHz = 47607;
    CHECK(!encodeRadioSettings(requested,image));
    requested = RadioSettings(); requested.packetLength = 61;
    CHECK(!encodeRadioSettings(requested,image));
    requested = RadioSettings(); requested.txLimitMs = 0;
    CHECK(!encodeRadioSettings(requested,image));
    requested = RadioSettings(); requested.bandwidthHz = NAN;
    CHECK(!encodeRadioSettings(requested,image));
    requested = RadioSettings(); requested.dataRateBaud = INFINITY;
    CHECK(!encodeRadioSettings(requested,image));
    requested = RadioSettings(); requested.modulation = Modulation(2);
    CHECK(!encodeRadioSettings(requested,image));
    requested = RadioSettings(); requested.modulation = Modulation::Gfsk; requested.dataRateBaud = 250001;
    CHECK(!encodeRadioSettings(requested,image));
    // Exhaustively round-trip each of the 16 legal channel-bandwidth register codes.
    for(uint8_t e = 0; e < 4; ++e) for(uint8_t m = 0; m < 4; ++m) {
        requested = RadioSettings(); requested.bandwidthHz = registerBandwidth(uint8_t((e<<6)|(m<<4)));
        CHECK(encodeRadioSettings(requested,image,&actual));
        CHECK(fabs(requested.bandwidthHz-actual.bandwidthHz) < 1e-6);
    }
    // Exercise every deviation code using a low rate and the widest RX bandwidth.
    for(uint8_t e = 0; e < 8; ++e) for(uint8_t m = 0; m < 8; ++m) {
        requested = RadioSettings(); requested.dataRateBaud = 600;
        requested.bandwidthHz = 812500; requested.deviationHz = registerDeviation(uint8_t((e<<4)|m));
        CHECK(encodeRadioSettings(requested,image,&actual));
        CHECK(fabs(requested.deviationHz-actual.deviationHz) < 1e-6);
    }
    return true;
}
static bool testMalformedPresets() {
    RegisterImage image;
    const RadioPreset* preset = presetAt(0);
    uint8_t bytes[kMaxPresetBytes]; memcpy(bytes,preset->data,preset->size);
    CHECK(!decodeCustomPreset(0,0,image));
    CHECK(!decodeCustomPreset(bytes,preset->size-1,image));
    bytes[0] = 0x30; CHECK(!decodeCustomPreset(bytes,preset->size,image));
    memcpy(bytes,preset->data,preset->size);
    bytes[0] = 0; CHECK(!decodeCustomPreset(bytes,preset->size,image));
    memcpy(bytes,preset->data,preset->size);
    bytes[2] = bytes[0]; CHECK(!decodeCustomPreset(bytes,preset->size,image));
    memcpy(bytes,preset->data,preset->size);
    bytes[34] = 1; CHECK(!decodeCustomPreset(bytes,preset->size,image));
    CHECK(loadPreset("AM650",image));
    image.set(0,0x2E); CHECK(!encodeCustomPreset(image,bytes,sizeof bytes));
    CHECK(loadPreset("AM650",image));
    image.set(1,0x0D); CHECK(!validateRegisterImage(image));
    CHECK(loadPreset("FM238",image));
    image.set(0x12,0x74); CHECK(!validateRegisterImage(image));
    return true;
}
static bool testParser() {
    const std::string good = std::string(header) + "RAW_Data: 100 -200 300\nRAW_Data: -400 2147483647 -1\n";
    CHECK(parse(good));
    SubParser p;
    for(size_t i=0; i<good.size(); ++i) CHECK(p.feed(good[i]));
    CHECK(p.finish()); CHECK(p.finish());
    CHECK(p.info().raw.pulses == 6); CHECK(p.info().raw.durationUs == 2147484648ULL);
    CHECK(p.info().raw.minimumUs == 1); CHECK(p.info().raw.maximumUs == INT32_MAX);
    CHECK(p.info().raw.bufferedBytes() == 24); CHECK(p.info().rawCompatible);
    CHECK(parse(std::string(header)+"RAW_Data: 100 -200"));
    CHECK(parse(std::string(header)+"RAW_Data: -10 20",SubError::InvalidRaw));
    CHECK(parse(std::string(header)+"RAW_Data: 10 20",SubError::InvalidRaw));
    CHECK(parse(std::string(header)+"RAW_Data: 10 -20\nRAW_Data: -30 40",SubError::InvalidRaw));
    CHECK(parse(std::string(header)+"RAW_Data: 0",SubError::InvalidRaw));
    CHECK(parse(std::string(header)+"RAW_Data: 2147483648",SubError::InvalidRaw));
    CHECK(parse(std::string(header)+"RAW_Data: 1 -2147483648",SubError::InvalidRaw));
    CHECK(parse(std::string(header)+"RAW_Data: 1e6",SubError::InvalidRaw));
    CHECK(parse(std::string(header)+"RAW_Data: +",SubError::InvalidRaw));
    CHECK(parse(std::string(header)+"RAW_Data: 1\r2 -3",SubError::Syntax));
    CHECK(parse(std::string(header)+"RAW_Data: 1 -2\nCustom_preset_module: CC1101\n",SubError::InvalidMetadata));
    CHECK(parse(std::string(header)+"RAW_Data:",SubError::InvalidRaw));
    CHECK(parse(header,SubError::MissingRaw));
    CHECK(parse("",SubError::MissingHeader));
    CHECK(parse("RAW_Data: 10 -10",SubError::MissingHeader));
    CHECK(parse(std::string(header)+"Version: 1\n",SubError::DuplicateField));
    CHECK(parse("Filetype: Flipper SubGhz BIN_RAW File\n",SubError::UnsupportedFiletype));
    CHECK(parse("Filetype: Flipper SubGhz RAW File\nVersion: 2\n",SubError::UnsupportedVersion));
    CHECK(parse("Filetype: Flipper SubGhz RAW File\nVersion: 4294967296\n",SubError::InvalidNumber));
    CHECK(parse("Frequency: 500000000\n",SubError::InvalidFrequency));
    CHECK(parse(std::string(header)+"Description: "+std::string(400,'x'),SubError::FieldTooLong));
    std::string unknown = good; unknown.replace(unknown.find("FuriHalSubGhzPresetOok650Async"),28,"UnknownPreset");
    CHECK(parse(unknown,SubError::UnsupportedPreset));
    std::string lines = header; lines += "RAW_Data: ";
    for(unsigned i=0;i<513;++i) lines += i&1 ? "-1 " : "1 ";
    CHECK(parse(lines,SubError::TooManyValues));
    SubParser limited(0,0,2); const std::string three = std::string(header)+"RAW_Data: 1 -2 3";
    CHECK(limited.feed(three.data(),three.size())); CHECK(!limited.finish()); CHECK(limited.error()==SubError::PulseLimit);
    SubParser rejected(rejectPulse); const std::string one = std::string(header)+"RAW_Data: 1 ";
    CHECK(!rejected.feed(one.data(),one.size())); CHECK(rejected.error()==SubError::SinkRejected);
    const char key[] = "# Synthetic metadata only\r\nFiletype: Flipper SubGhz Key File\r\nVersion: 1\r\nFrequency: 433920000\r\nPreset: FuriHalSubGhzPresetOok650Async\r\nProtocol: DeveloperTest\r\nBit: 24\r\nKey: 00 00 00 00 00 A5 5A 01\r\nTE: 400\r\n";
    SubParser keyParser; CHECK(keyParser.feed(key,sizeof key-1)); CHECK(keyParser.finish());
    CHECK(keyParser.info().bits == 24); CHECK(keyParser.info().key[5] == 0xA5);
    CHECK(!keyParser.info().rawCompatible); CHECK(!strcmp(keyParser.info().protocol,"DeveloperTest"));
    return true;
}
static bool testWriter() {
    std::string output;
    RawWriter w(append,&output);
    CHECK(w.begin(433920000,"AM650"));
    for(unsigned i=0;i<100;++i) CHECK(w.pulse(i&1 ? -int32_t(i+1) : int32_t(i+1)));
    CHECK(w.finish()); CHECK(w.finish()); CHECK(w.statistics().durationUs == 5050);
    CHECK(parse(output));
    RegisterImage image; CHECK(loadPreset("FM238",image));
    std::string customOutput;
    RawWriter custom(append,&customOutput);
    CHECK(custom.beginCustom(433920000,image)); CHECK(custom.pulse(200)); CHECK(custom.pulse(-100)); CHECK(custom.finish());
    CHECK(parse(customOutput));
    SubParser p; CHECK(p.feed(customOutput.data(),customOutput.size())); CHECK(p.finish());
    CHECK(p.info().radio.value[0x15] == 4); CHECK(p.info().rawCompatible);
    // Advanced async images must export without address zero, which Flipper's
    // official loader treats as terminator regardless of the following value.
    RadioSettings advanced; advanced.modulation=Modulation::AskOok;
    advanced.packetFormat=PacketFormat::Async; advanced.crc=false; advanced.syncMode=0;
    CHECK(encodeRadioSettings(advanced,image)); CHECK(!image.present[0]);
    std::string advancedOutput; RawWriter advancedWriter(append,&advancedOutput);
    CHECK(advancedWriter.beginCustom(433920000,image));
    CHECK(advancedWriter.pulse(500)); CHECK(advancedWriter.pulse(-500)); CHECK(advancedWriter.finish());
    CHECK(parse(advancedOutput));
    std::string reject;
    RawWriter invalid(append,&reject); CHECK(invalid.begin(433920000,"AM270"));
    CHECK(!invalid.pulse(-1)); CHECK(!invalid.finish());
    return true;
}
static bool testFixtures(const char* directory) {
    const char* names[] = {"lab_ook.sub","lab_fm238.sub","lab_custom_ook.sub","lab_protocol_metadata.sub"};
    const unsigned pulseCounts[] = {8,8,4,0};
    for(unsigned i=0;i<4;++i) {
        const std::string path = std::string(directory)+"/"+names[i];
        FILE* file=fopen(path.c_str(),"rb"); CHECK(file != 0);
        SubParser parser; char buffer[73]; size_t length;
        bool fed=true;
        while((length=fread(buffer,1,sizeof buffer,file)) != 0)
            if(!parser.feed(buffer,length)) { fed=false; break; }
        const bool readOk=!ferror(file); fclose(file);
        CHECK(readOk); CHECK(fed); CHECK(parser.finish());
        CHECK(parser.info().raw.pulses==pulseCounts[i]);
        CHECK(parser.info().rawCompatible==(i!=3));
    }
    return true;
}
static bool testAdversarial() {
    // Deterministic mutations exercise bounded tokens, header duplicates and binary junk.
    // This is not a proof of memory safety or a substitute for on-hardware timing tests.
    const std::string base = std::string(header)+"RAW_Data: 400 -400 800 -800\n";
    uint32_t rng=0xE5A74339;
    for(unsigned test=0;test<20000;++test) {
        std::string value=base;
        for(unsigned j=0;j<1+(test%5);++j) {
            rng=rng*1664525UL+1013904223UL;
            value[rng%value.size()] = char(rng>>24);
        }
        SubParser p(0,0,1024);
        const bool fed=p.feed(value.data(),value.size());
        const bool done=fed && p.finish();
        CHECK(!done || (p.info().version==1 && supportedFrequency(p.info().frequencyHz) && p.info().raw.pulses<=1024));
    }
    return true;
}
int main(int argc,char** argv) {
    if(!testPresets() || !testSettings() || !testMalformedPresets() || !testParser() || !testWriter() ||
       (argc>1 && !testFixtures(argv[1])) || !testAdversarial()) return 1;
    printf("formats portable tests: %u checks passed (including 20000 deterministic mutations)\n",checks);
    return 0;
}
