#pragma once
#include "mock_state.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
inline uint32_t millis(){return mock.now;}
inline void delay(unsigned value){mock.now+=value;}
struct MockSerial {
    size_t available(){return mock.input.size()-mock.inputAt;}
    size_t readBytes(uint8_t* out,size_t count) {
        count=std::min(count,available());memcpy(out,mock.input.data()+mock.inputAt,count);mock.inputAt+=count;return count;
    }
    void println(const char* value){mock.output+=value;mock.output+='\n';}
    void printf(const char* format,...) {
        char text[256];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);mock.output+=text;
    }
};
extern MockSerial Serial;
