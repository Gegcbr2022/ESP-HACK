#pragma once
#include <stddef.h>
#include <string.h>
inline bool validAppFilename(const char* name) {
    if(!name)return false;
    const size_t n=strlen(name);
    if(n<5||n>40||strcmp(name+n-4,".lua"))return false;
    for(size_t i=0;i<n-4;++i) {
        const char c=name[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    }
    return true;
}
