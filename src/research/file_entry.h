#pragma once
#include "file_page.h"
#include "../interface/utf8_text.h"
#include <stdint.h>

namespace research {
static const size_t kRadioPathCapacity = 1024;
static const size_t kFilePreviewCapacity = 64;

// A preview is never used to open a file. Resolve the original directory
// ordinal again and compare its full-name identity before copying one path.
struct FileEntry {
    char preview[kFilePreviewCapacity];
    uint64_t nameHash;
    uint32_t ordinal;
    uint16_t nameBytes;
    bool directory;
};
inline uint64_t fileNameHash(const char* name) {
    uint64_t value=14695981039346656037ULL;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(name);*p;++p)
        value=(value^*p)*1099511628211ULL;
    return value;
}
inline bool setFileEntry(FileEntry& entry,const char* leaf,uint32_t ordinal,bool directory) {
    if(!leaf||!*leaf)return false;
    const size_t bytes=strlen(leaf);
    if(bytes>=kRadioPathCapacity)return false;
    FileEntry candidate={};candidate.nameHash=fileNameHash(leaf);
    candidate.ordinal=ordinal;candidate.nameBytes=uint16_t(bytes);candidate.directory=directory;
    // The OLED supports ASCII/Russian. Unknown glyphs get a visible fallback;
    // actual filesystem bytes remain intact in the directory, never in preview.
    size_t source=0,dest=0,glyphs=0;
    while(source<bytes&&glyphs<17) {
        uint16_t codepoint=0;size_t width=ui_text::decodeGlyph(leaf+source,bytes-source,codepoint);
        if(width&&dest+width<sizeof candidate.preview) {
            memcpy(candidate.preview+dest,leaf+source,width);dest+=width;source+=width;
        } else {
            candidate.preview[dest++]='?';++source;
            while(source<bytes&&(uint8_t(leaf[source])&0xC0)==0x80)++source;
        }
        ++glyphs;
    }
    if(source<bytes&&dest+1<sizeof candidate.preview)candidate.preview[dest++]='~';
    candidate.preview[dest]=0;entry=candidate;return true;
}
inline bool sameFileEntry(const FileEntry& entry,const char* leaf,uint32_t ordinal,bool directory) {
    return leaf&&entry.ordinal==ordinal&&entry.directory==directory&&
           entry.nameBytes==strlen(leaf)&&entry.nameHash==fileNameHash(leaf);
}
inline bool radioFilePath(const char* path,bool allowRoot=false) {
    if(!path||strlen(path)>=kRadioPathCapacity||strchr(path,'\\'))return false;
    if(allowRoot&&!strcmp(path,"/subghz"))return true;
    if(strncmp(path,"/subghz/",8)||!path[8])return false;
    const char* begin=path+8;
    for(const char* p=begin;;++p)if(!*p||*p=='/') {
        const size_t length=p-begin;
        if(!length||(length==1&&begin[0]=='.')||(length==2&&begin[0]=='.'&&begin[1]=='.'))return false;
        if(!*p)return true;
        begin=p+1;
    }
}
}
