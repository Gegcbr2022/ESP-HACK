#pragma once
#include <stdint.h>
#include <stddef.h>
#include <map>
#include <set>
#include <string>
#include <vector>

struct MockEntry { std::vector<uint8_t> bytes; bool directory=false; };
struct MockState {
    std::map<std::string,MockEntry> files;
    std::set<std::string> failRemove,failRenameFrom;
    std::string input,output;
    size_t inputAt=0,readBytes=0;
    uint32_t now=0,readDelay=0;
    unsigned starts=0,failStart=0;
    bool corruptFlush=false,truncateFlush=false,shortWrite=false,shortRead=false;
};
extern MockState mock;

// A deterministic digest double tests control flow, not SHA-256 mathematics.
// The production translation unit continues to call the mbedTLS SHA-256 API.
inline void mockDigest(const std::vector<uint8_t>& bytes,uint8_t digest[32]) {
    uint32_t value=2166136261u;
    for(uint8_t byte:bytes)value=(value^byte)*16777619u;
    for(unsigned i=0;i<32;++i){value=value*1664525u+1013904223u;digest[i]=uint8_t(value>>24);}
}
