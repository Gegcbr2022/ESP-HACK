#include "sub_file.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace research {
namespace {
enum Fields {
    FiletypeField=1, VersionField=2, FrequencyField=4, PresetField=8,
    ModuleField=16, CustomField=32, ProtocolField=64, BitField=128,
    TeField=256, KeyField=512
};
const uint16_t required = FiletypeField | VersionField | FrequencyField | PresetField | ProtocolField;
bool space(char c) { return c == ' ' || c == '\t'; }
char* trim(char* value) {
    while(space(*value)) ++value;
    size_t n = strlen(value);
    while(n && space(value[n-1])) value[--n] = 0;
    return value;
}
bool decimal(const char* value, uint32_t& output) {
    if(!*value) return false;
    uint32_t number = 0;
    for(; *value; ++value) {
        if(*value < '0' || *value > '9') return false;
        const uint8_t digit = uint8_t(*value - '0');
        if(number > (UINT32_MAX - digit) / 10) return false;
        number = number * 10 + digit;
    }
    output = number;
    return true;
}
int hex(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
bool hexBytes(const char* value, uint8_t* output, size_t capacity, size_t& length) {
    length = 0;
    while(*value) {
        while(space(*value)) ++value;
        if(!*value) break;
        if(hex(value[0]) < 0 || !value[1] || hex(value[1]) < 0 || length == capacity) return false;
        output[length++] = uint8_t(hex(value[0]) * 16 + hex(value[1]));
        value += 2;
        if(*value && !space(*value)) return false;
    }
    return length != 0;
}
bool rawModulation(const RadioSettings& settings) {
    return settings.packetFormat == PacketFormat::Async &&
           (settings.modulation == Modulation::AskOok || settings.modulation == Modulation::Fsk2 ||
            settings.modulation == Modulation::Gfsk);
}
}

RawStatistics::RawStatistics()
    : pulses(0), positivePulses(0), negativePulses(0), shortPulses(0), gaps(0),
      minimumUs(0), maximumUs(0), durationUs(0), highUs(0), lowUs(0) {}
void RawStatistics::add(int32_t pulse) {
    const uint32_t duration = pulse < 0 ? uint32_t(-int64_t(pulse)) : uint32_t(pulse);
    if(!pulses || duration < minimumUs) minimumUs = duration;
    if(duration > maximumUs) maximumUs = duration;
    ++pulses;
    durationUs += duration;
    if(pulse > 0) { ++positivePulses; highUs += duration; }
    else { ++negativePulses; lowUs += duration; }
    if(duration < 10) ++shortPulses;
    if(duration >= 10000) ++gaps;
}
SubInfo::SubInfo() : kind(SubKind::Unknown), frequencyHz(0), version(0), bits(0), teUs(0),
                     keyBytes(0), rawCompatible(false) {
    preset[0] = protocol[0] = module[0] = 0;
    memset(key, 0, sizeof key);
}
SubParser::SubParser(PulseSink sink, void* context, uint32_t maxPulses)
    : sink_(sink), context_(context), maxPulses_(maxPulses), line_(1), error_(SubError::None),
      fields_(0), linePulses_(0), nameLength_(0), valueLength_(0), tokenLength_(0), customSize_(0),
      hasColon_(false), comment_(false), rawLine_(false), lastPositive_(false), finished_(false),
      radioResolved_(false), crPending_(false) {
    name_[0] = value_[0] = token_[0] = 0;
}
bool SubParser::fail(SubError error) { if(error_ == SubError::None) error_ = error; return false; }
bool SubParser::claim(uint16_t mask) {
    if(fields_ & mask) return fail(SubError::DuplicateField);
    fields_ |= mask;
    return true;
}
void SubParser::resetLine() {
    nameLength_ = valueLength_ = tokenLength_ = linePulses_ = 0;
    hasColon_ = comment_ = rawLine_ = false;
    name_[0] = value_[0] = token_[0] = 0;
}
bool SubParser::feed(const char* data, size_t length) {
    if(!data && length) return fail(SubError::Syntax);
    for(size_t i = 0; i < length; ++i) if(!feed(data[i])) return false;
    return true;
}
bool SubParser::feed(char byte) {
    if(error_ != SubError::None) return false;
    if(finished_) return fail(SubError::Syntax);
    if(crPending_) {
        if(byte != '\n') return fail(SubError::Syntax);
        crPending_ = false;
    }
    if(byte == '\r') { crPending_ = true; return true; }
    if(byte == '\n') {
        if(!endLine()) return false;
        resetLine(); ++line_; return true;
    }
    if(byte == 0 || (uint8_t(byte) < 32 && byte != '\t')) return fail(SubError::Syntax);
    if(comment_) return true;
    if(!hasColon_) {
        if(!nameLength_ && space(byte)) return true;
        if(!nameLength_ && byte == '#') { comment_ = true; return true; }
        if(byte == ':') {
            name_[nameLength_] = 0;
            hasColon_ = true;
            rawLine_ = !strcmp(trim(name_), "RAW_Data");
            if(rawLine_) {
                if((fields_ & required) != required) return fail(SubError::MissingHeader);
                if(info_.kind != SubKind::Raw || strcmp(info_.protocol, "RAW")) return fail(SubError::InvalidMetadata);
                if(!resolveRadio()) return false;
            }
            return true;
        }
        if(nameLength_ + 1 >= sizeof name_) return fail(SubError::FieldTooLong);
        name_[nameLength_++] = byte;
        return true;
    }
    if(rawLine_) {
        if(space(byte)) return rawToken();
        if(tokenLength_ + 1 >= sizeof token_) return fail(SubError::InvalidNumber);
        token_[tokenLength_++] = byte;
    } else {
        if(valueLength_ + 1 >= sizeof value_) return fail(SubError::FieldTooLong);
        value_[valueLength_++] = byte;
    }
    return true;
}
bool SubParser::rawToken() {
    if(!tokenLength_) return true;
    token_[tokenLength_] = 0;
    const char* token = token_;
    bool positive = true;
    if(*token == '-' || *token == '+') { positive = *token != '-'; ++token; }
    uint32_t number;
    if(!decimal(token, number) || !number || number > INT32_MAX) return fail(SubError::InvalidRaw);
    if((!info_.raw.pulses && !positive) || (info_.raw.pulses && positive == lastPositive_))
        return fail(SubError::InvalidRaw);
    if(info_.raw.pulses >= maxPulses_) return fail(SubError::PulseLimit);
    if(linePulses_ >= 512) return fail(SubError::TooManyValues);
    const int32_t pulse = positive ? int32_t(number) : -int32_t(number);
    // A sink must stage data; metadata/timings later in the file may still fail validation.
    if(sink_ && !sink_(context_, pulse)) return fail(SubError::SinkRejected);
    info_.raw.add(pulse);
    lastPositive_ = positive;
    ++linePulses_; tokenLength_ = 0;
    return true;
}
bool SubParser::endLine() {
    if(comment_) return true;
    if(!hasColon_) return nameLength_ ? fail(SubError::Syntax) : true;
    if(rawLine_) {
        if(!rawToken()) return false;
        return linePulses_ ? true : fail(SubError::InvalidRaw);
    }
    name_[nameLength_] = 0;
    value_[valueLength_] = 0;
    return field();
}
bool SubParser::field() {
    if(info_.raw.pulses) return fail(SubError::InvalidMetadata);
    const char* name = trim(name_);
    const char* value = trim(value_);
    uint32_t number;
    if(!*name) return fail(SubError::Syntax);
    if(!strcmp(name, "Filetype")) {
        if(!claim(FiletypeField)) return false;
        if(!strcmp(value, "Flipper SubGhz RAW File")) info_.kind = SubKind::Raw;
        else if(!strcmp(value, "Flipper SubGhz Key File")) info_.kind = SubKind::Key;
        else return fail(SubError::UnsupportedFiletype);
    } else if(!strcmp(name, "Version")) {
        if(!claim(VersionField)) return false;
        if(!decimal(value, info_.version)) return fail(SubError::InvalidNumber);
        if(info_.version != 1) return fail(SubError::UnsupportedVersion);
    } else if(!strcmp(name, "Frequency")) {
        if(!claim(FrequencyField)) return false;
        if(!decimal(value, info_.frequencyHz)) return fail(SubError::InvalidNumber);
        if(!supportedFrequency(info_.frequencyHz)) return fail(SubError::InvalidFrequency);
    } else if(!strcmp(name, "Preset")) {
        if(!claim(PresetField)) return false;
        if(!*value || strlen(value) >= sizeof info_.preset) return fail(SubError::FieldTooLong);
        strcpy(info_.preset, value);
    } else if(!strcmp(name, "Custom_preset_module")) {
        if(!claim(ModuleField)) return false;
        if(strlen(value) >= sizeof info_.module) return fail(SubError::FieldTooLong);
        strcpy(info_.module, value);
    } else if(!strcmp(name, "Custom_preset_data")) {
        if(!claim(CustomField)) return false;
        if(!hexBytes(value, custom_, sizeof custom_, customSize_)) return fail(SubError::InvalidCustomPreset);
    } else if(!strcmp(name, "Protocol")) {
        if(!claim(ProtocolField)) return false;
        if(!*value || strlen(value) >= sizeof info_.protocol) return fail(SubError::FieldTooLong);
        strcpy(info_.protocol, value);
    } else if(!strcmp(name, "Bit")) {
        if(!claim(BitField)) return false;
        if(!decimal(value, number) || !number || number > 65535) return fail(SubError::InvalidNumber);
        info_.bits = uint16_t(number);
    } else if(!strcmp(name, "TE")) {
        if(!claim(TeField)) return false;
        if(!decimal(value, info_.teUs) || !info_.teUs) return fail(SubError::InvalidNumber);
    } else if(!strcmp(name, "Key")) {
        if(!claim(KeyField)) return false;
        size_t length;
        if(!hexBytes(value, info_.key, sizeof info_.key, length)) return fail(SubError::InvalidMetadata);
        info_.keyBytes = uint8_t(length);
    }
    // Protocol-specific extension fields are inspected but never decoded/replayed here.
    return true;
}
bool SubParser::resolveRadio() {
    if(radioResolved_) return true;
    if(!strcmp(info_.preset, "FuriHalSubGhzPresetCustom")) {
        if(!(fields_ & ModuleField) || !(fields_ & CustomField)) return fail(SubError::InvalidCustomPreset);
        if(strcmp(info_.module, "CC1101")) return fail(SubError::UnsupportedModule);
        if(!decodeCustomPreset(custom_, customSize_, info_.radio)) return fail(SubError::InvalidCustomPreset);
    } else {
        if(fields_ & (ModuleField | CustomField)) return fail(SubError::InvalidCustomPreset);
        if(!loadPreset(info_.preset, info_.radio)) return fail(SubError::UnsupportedPreset);
    }
    RadioSettings settings;
    if(!describeRadio(info_.radio, info_.frequencyHz, settings)) return fail(SubError::InvalidCustomPreset);
    info_.rawCompatible = info_.kind == SubKind::Raw && !strcmp(info_.protocol, "RAW") && rawModulation(settings);
    radioResolved_ = true;
    return true;
}
bool SubParser::finish() {
    if(error_ != SubError::None) return false;
    if(finished_) return true;
    if(crPending_) return fail(SubError::Syntax);
    if(!endLine()) return false;
    if((fields_ & required) != required) return fail(SubError::MissingHeader);
    if(!resolveRadio()) return false;
    if(info_.kind == SubKind::Raw) {
        if(strcmp(info_.protocol, "RAW")) return fail(SubError::InvalidMetadata);
        if(!info_.raw.pulses) return fail(SubError::MissingRaw);
    } else if(!strcmp(info_.protocol, "RAW") || info_.raw.pulses) return fail(SubError::InvalidMetadata);
    if(info_.keyBytes && info_.bits && info_.bits > unsigned(info_.keyBytes) * 8)
        return fail(SubError::InvalidMetadata);
    finished_ = true;
    return true;
}
const char* SubParser::errorText() const {
    switch(error_) {
    case SubError::None: return "OK";
    case SubError::Syntax: return "Malformed field or invalid character";
    case SubError::FieldTooLong: return "Field exceeds bounded parser storage";
    case SubError::DuplicateField: return "Duplicate metadata field";
    case SubError::InvalidNumber: return "Invalid or overflowing number";
    case SubError::MissingHeader: return "Missing filetype/version/frequency/preset/protocol";
    case SubError::UnsupportedVersion: return "Only .sub version 1 is supported";
    case SubError::InvalidFrequency: return "Frequency outside CC1101 hardware bands";
    case SubError::UnsupportedPreset: return "Unknown preset; no substitution applied";
    case SubError::InvalidCustomPreset: return "Invalid or unsupported CC1101 custom preset";
    case SubError::InvalidRaw: return "RAW must start positive, alternate, and be nonzero int32";
    case SubError::PulseLimit: return "RAW pulse count exceeds configured limit";
    case SubError::SinkRejected: return "Pulse consumer rejected data";
    case SubError::MissingRaw: return "RAW file has no pulse data";
    case SubError::UnsupportedFiletype: return "Unsupported Flipper filetype";
    case SubError::UnsupportedModule: return "Only CC1101 custom presets supported";
    case SubError::TooManyValues: return "RAW_Data line exceeds 512 values";
    case SubError::InvalidMetadata: return "File type/protocol/key metadata mismatch";
    }
    return "Unknown parser error";
}

RawWriter::RawWriter(TextSink sink, void* context)
    : sink_(sink), context_(context), linePulses_(0), started_(false), finished_(false),
      failed_(false), lastPositive_(false) {}
bool RawWriter::text(const char* data) {
    if(failed_ || !sink_ || !sink_(context_, data, strlen(data))) { failed_ = true; return false; }
    return true;
}
bool RawWriter::start(uint32_t frequency) {
    if(started_ || !supportedFrequency(frequency)) { failed_ = true; return false; }
    started_ = true;
    char buffer[100];
    snprintf(buffer, sizeof buffer, "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: %lu\n", (unsigned long)frequency);
    return text(buffer);
}
bool RawWriter::begin(uint32_t frequency, const char* name) {
    const RadioPreset* preset = findPreset(name);
    if(!preset) { failed_ = true; return false; }
    if(!start(frequency) || !text("Preset: ") || !text(preset->flipperName)) return false;
    return text("\nProtocol: RAW\n");
}
bool RawWriter::beginCustom(uint32_t frequency, const RegisterImage& image) {
    uint8_t bytes[kMaxPresetBytes];
    const size_t length = encodeCustomPreset(image, bytes, sizeof bytes);
    RadioSettings settings;
    if(!length || !describeRadio(image, frequency, settings) || !rawModulation(settings)) {
        failed_ = true; return false;
    }
    if(!start(frequency) || !text("Preset: FuriHalSubGhzPresetCustom\nCustom_preset_module: CC1101\nCustom_preset_data: "))
        return false;
    char token[4];
    for(size_t i = 0; i < length; ++i) {
        snprintf(token, sizeof token, "%02X%c", bytes[i], i + 1 == length ? '\n' : ' ');
        if(!text(token)) return false;
    }
    return text("Protocol: RAW\n");
}
bool RawWriter::pulse(int32_t pulse) {
    if(!started_ || finished_ || failed_ || !pulse || pulse == INT32_MIN ||
       (!stats_.pulses && pulse < 0) || (stats_.pulses && (pulse > 0) == lastPositive_) || stats_.pulses == UINT32_MAX) {
        failed_ = true; return false;
    }
    if(!linePulses_ && !text("RAW_Data: ")) return false;
    char token[16];
    snprintf(token, sizeof token, "%ld%c", (long)pulse, linePulses_ == 15 ? '\n' : ' ');
    if(!text(token)) return false;
    stats_.add(pulse);
    lastPositive_ = pulse > 0;
    linePulses_ = uint8_t((linePulses_ + 1) % 16);
    return true;
}
bool RawWriter::finish() {
    if(failed_ || !started_ || !stats_.pulses) return false;
    if(finished_) return true;
    if(linePulses_ && !text("\n")) return false;
    finished_ = true;
    return true;
}

} // namespace research
