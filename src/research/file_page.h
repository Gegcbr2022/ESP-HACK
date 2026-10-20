#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace research {
// One additional UI slot is reserved for a synthetic NextPage row.
class FilePage {
public:
    static const size_t kEntries = 39;
    explicit FilePage(uint32_t page):offset_(uint64_t(page)*kEntries),seen_(0),stored_(0),more_(false) {}
    bool consider() {
        const uint64_t index=seen_;
        if(seen_!=UINT64_MAX)++seen_;
        if(index<offset_)return false;
        if(stored_==kEntries){more_=true;return false;}
        ++stored_;return true;
    }
    bool more() const{return more_;}
    uint64_t total() const{return seen_;}
    uint32_t lastPage() const {
        const uint64_t page=seen_?(seen_-1)/kEntries:0;
        return page>UINT32_MAX?UINT32_MAX:uint32_t(page);
    }
private:
    uint64_t offset_,seen_;
    size_t stored_;
    bool more_;
};

// Preserve complete filesystem bytes, including UTF8/spaces/parentheses.
// Failure leaves output unchanged; never truncate a path or UTF8 sequence.
inline bool completeFilePath(char* output,size_t capacity,const char* folder,const char* leaf) {
    if(!output||!folder||!leaf||!*folder||!*leaf||!capacity)return false;
    const size_t prefix=strlen(folder),name=strlen(leaf),separator=folder[prefix-1]=='/'?0:1;
    if(prefix>=capacity||separator>=capacity-prefix||name>=capacity-prefix-separator)return false;
    memcpy(output,folder,prefix);
    if(separator)output[prefix]='/';
    memcpy(output+prefix+separator,leaf,name+1);return true;
}
}
