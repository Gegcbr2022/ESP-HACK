// SPDX-License-Identifier: GPL-3.0-or-later
// Timing/bit order derived from official Flipper1.5.1, commit
// 9e810dc399e027e48ce07751df72168abbb19065, GPL-3.0-or-later:
// lib/subghz/protocols/{princeton,came}.c. See docs/STATIC_SUB_PLAYBACK.md.
#include "static_sub_encoder.h"
#include <string.h>

namespace research {
bool dynamicSubProtocol(const char* protocol) {
    return protocol && (!strncmp(protocol,"KeeLoq",6) || !strncmp(protocol,"Alutech",7) ||
                        !strncmp(protocol,"StarLine",8) || !strncmp(protocol,"Star Line",9));
}
StaticSubError encodeStaticSub(const SubInfo& info,int32_t* output,size_t capacity,
                              StaticSubWaveform& result) {
    if(dynamicSubProtocol(info.protocol))return StaticSubError::Dynamic;
    const bool princeton=!strcmp(info.protocol,"Princeton"),came=!strcmp(info.protocol,"CAME");
    if(!princeton&&!came)return StaticSubError::Protocol;
    if(info.kind!=SubKind::Key || (princeton&&info.bits!=24) ||
       (came&&info.bits!=12&&info.bits!=24) || !info.keyBytes || info.keyBytes>8)return StaticSubError::Key;
    uint64_t key=0;
    for(uint8_t i=0;i<info.keyBytes;++i)key=(key<<8)|info.key[i];
    if(key>>info.bits)return StaticSubError::Key;
    RadioSettings radio;
    if(!describeRadio(info.radio,info.frequencyHz,radio) || radio.modulation!=Modulation::AskOok ||
       radio.packetFormat!=PacketFormat::Async || radio.crc || radio.whitening || radio.manchester ||
       radio.fec || radio.syncMode)return StaticSubError::Radio;
    if(info.radio.pa[0] || (info.radio.present[0x22]&&(info.radio.value[0x22]&7)!=1))return StaticSubError::Radio;
    for(size_t i=2;i<8;++i)if(info.radio.pa[i])return StaticSubError::Radio;
    const uint32_t te=came?320:info.teUs;
    if(!te || te>10000)return StaticSubError::Timing;
    const uint32_t guard=info.guardTime>=15&&info.guardTime<=72?info.guardTime:30;
    const uint32_t header=info.bits==12?47:76;
    const uint64_t period=came?uint64_t(1+3*info.bits+header)*320:
                               uint64_t(4*info.bits+1+guard)*te;
    const size_t repeats=6,count=(size_t(info.bits)*2+2)*repeats;
    if(period*repeats>5000000)return StaticSubError::Duration;
    if(!output || count>capacity || count>2048)return StaticSubError::Capacity;
    size_t at=0;
    for(size_t repeat=0;repeat<repeats;++repeat) {
        // FlipperCAME starts LOWheader,+320,data. Move that leading LOW to
        // periodend soRMT canbeginHIGH; cyclicedge timings/bitorder areidentical.
        if(came)output[at++]=320;
        for(uint16_t bit=info.bits;bit;--bit) {
            const bool one=(key>>(bit-1))&1;
            if(came){output[at++]=one?-640:-320;output[at++]=one?320:640;}
            else{output[at++]=int32_t(one?te*3:te);output[at++]=-int32_t(one?te:te*3);}
        }
        if(came)output[at++]=-int32_t(header*320);
        else{output[at++]=int32_t(te);output[at++]=-int32_t(guard*te);}
    }
    StaticSubWaveform candidate={count,uint32_t(period*repeats),uint8_t(repeats),came};
    result=candidate;return StaticSubError::None;
}
const char* staticSubErrorRu(StaticSubError error) {
    switch(error) {
    case StaticSubError::None:return "Готово к передаче";
    case StaticSubError::Dynamic:return "Динамический код:\nнет кодировщика.";
    case StaticSubError::Protocol:return "Для этого протокола\nнет кодировщика.";
    case StaticSubError::Key:return "Не поддерживается\nдлина или ключ.";
    case StaticSubError::Timing:return "Нужен TE 1..10000 мкс.";
    case StaticSubError::Radio:return "Нужен двоичный OOK\nбез обработки пакетов.";
    case StaticSubError::Capacity:return "Слишком много импульсов\nдля передачи.";
    case StaticSubError::Duration:return "Сигнал длиннее 5 сек.";
    }
    return "Нет кодировщика.";
}
}
