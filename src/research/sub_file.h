#pragma once

#include "radio_config.h"
#include <stddef.h>
#include <stdint.h>

namespace research {

struct RawStatistics {
    uint32_t pulses, positivePulses, negativePulses, shortPulses, gaps;
    uint32_t minimumUs, maximumUs;
    uint64_t durationUs, highUs, lowUs;
    RawStatistics();
    void add(int32_t pulse);
    uint64_t bufferedBytes() const { return uint64_t(pulses) * sizeof(int32_t); }
};

enum class SubKind : uint8_t { Unknown, Key, Raw };
enum class SubError : uint8_t {
    None, Syntax, FieldTooLong, DuplicateField, InvalidNumber, MissingHeader,
    UnsupportedVersion, InvalidFrequency, UnsupportedPreset, InvalidCustomPreset,
    InvalidRaw, PulseLimit, SinkRejected, MissingRaw, UnsupportedFiletype,
    UnsupportedModule, TooManyValues, InvalidMetadata
};

struct SubInfo {
    SubKind kind;
    uint32_t frequencyHz;
    uint32_t version;
    char preset[64], protocol[64], module[16];
    RegisterImage radio;
    RawStatistics raw;
    uint16_t bits;
    uint32_t teUs;
    uint8_t key[8], keyBytes;
    bool rawCompatible; // Async ASK/OOK or 2-FSK/GFSK only; never implies TX permission.
    uint8_t hostFields; // bit0 RSSI, bit1 RX deadline, bit2 TX deadline; optional X-ESP metadata.
    int16_t rssiThresholdDbm;
    uint32_t rxTimeoutMs, txLimitMs;
    SubInfo();
};

typedef bool (*PulseSink)(void* context, int32_t pulseUs);
typedef bool (*TextSink)(void* context, const char* text, size_t length);

// Preset storage uses Key metadata with a developer-defined protocol, no dummy RAW pulses.
bool writePresetMetadata(TextSink sink, void* context, const RegisterImage& image,
                         const RadioSettings& settings);
bool settingsFromSub(const SubInfo& info, RadioSettings& settings, const char** error = 0);

class SubParser {
public:
    explicit SubParser(PulseSink sink = 0, void* context = 0, uint32_t maxPulses = 1000000);
    bool feed(char byte);
    bool feed(const char* data, size_t length);
    bool finish();
    const SubInfo& info() const { return info_; }
    SubError error() const { return error_; }
    const char* errorText() const;
    uint32_t line() const { return line_; }
private:
    bool fail(SubError error);
    bool endLine();
    bool field();
    bool rawToken();
    bool resolveRadio();
    bool claim(uint16_t mask);
    void resetLine();
    SubInfo info_;
    PulseSink sink_;
    void* context_;
    uint32_t maxPulses_, line_;
    SubError error_;
    uint16_t fields_, linePulses_;
    char name_[40], value_[400], token_[12];
    size_t nameLength_, valueLength_, tokenLength_;
    uint8_t custom_[kMaxPresetBytes];
    size_t customSize_;
    bool hasColon_, comment_, rawLine_, lastPositive_, finished_, radioResolved_, crPending_;
};

// Writes RAW in small chunks, with globally alternating signs and 16 values/line.
class RawWriter {
public:
    RawWriter(TextSink sink, void* context);
    bool begin(uint32_t frequencyHz, const char* presetName);
    bool beginCustom(uint32_t frequencyHz, const RegisterImage& image);
    bool pulse(int32_t pulseUs);
    bool finish();
    const RawStatistics& statistics() const { return stats_; }
private:
    bool text(const char* data);
    bool start(uint32_t frequencyHz);
    TextSink sink_;
    void* context_;
    RawStatistics stats_;
    uint8_t linePulses_;
    bool started_, finished_, failed_, lastPositive_;
};

} // namespace research
