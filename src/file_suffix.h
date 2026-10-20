#pragma once
#include <stddef.h>
#include <string.h>

// FAT filenames and Flipper exports may use .SUB or mixed-case extensions.
inline bool fileHasSuffix(const char* name,const char* suffix) {
    if(!name||!suffix)return false;
    const size_t length=strlen(name),tail=strlen(suffix);
    if(!tail||tail>length)return false;
    for(size_t i=0;i<tail;++i) {
        char a=name[length-tail+i],b=suffix[i];
        if(a>='A'&&a<='Z')a+=char('a'-'A');
        if(b>='A'&&b<='Z')b+=char('a'-'A');
        if(a!=b)return false;
    }
    return true;
}
