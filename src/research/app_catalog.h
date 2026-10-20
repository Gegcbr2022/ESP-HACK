#pragma once
#include "app_filename.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

namespace research { namespace apps {
enum class Category : uint8_t { Tools, Games, Radio };
enum class Section : uint8_t { All, Tools, Games, Radio, Favorites, Recent };
struct Entry {
    char file[41], title[22], description[65];
    uint32_t bytes;
    Category category;
};
struct Preferences {
    char favorites[8][41], recent[4][41];
    uint8_t favoriteCount, recentCount;
};
inline const char* sectionName(Section section) {
    static const char* names[]={"All apps","Tools","Games","Radio","Favorites","Recent"};
    return names[uint8_t(section)<6?uint8_t(section):0];
}
inline bool copyAscii(char* output,size_t capacity,const char* text,size_t length) {
    while(length&&(text[length-1]==' '||text[length-1]=='\r'||text[length-1]=='\t'))--length;
    while(length&&(*text==' '||*text=='\t')){++text;--length;}
    if(!length||length>=capacity)return false;
    for(size_t i=0;i<length;++i)if(uint8_t(text[i])<32||uint8_t(text[i])>126)return false;
    memcpy(output,text,length);output[length]=0;return true;
}
inline void initialize(Entry& entry,const char* file,uint32_t bytes) {
    memset(&entry,0,sizeof entry);entry.bytes=bytes;entry.category=Category::Tools;
    snprintf(entry.file,sizeof entry.file,"%s",file);
    const size_t length=strlen(file);const size_t stem=length>=4?length-4:length;
    snprintf(entry.title,sizeof entry.title,"%.*s",int(stem),file);
    snprintf(entry.description,sizeof entry.description,"SD application / hold BACK to exit");
}
// Metadata is data, never Lua execution. Stop at the first non-comment source line.
inline void metadata(Entry& entry,const char* source,size_t length) {
    if(length>1024)length=1024;
    size_t at=length>=3&&uint8_t(source[0])==0xEF&&uint8_t(source[1])==0xBB&&uint8_t(source[2])==0xBF?3:0;
    for(unsigned lines=0;at<length&&lines<32;++lines) {
        const size_t start=at;while(at<length&&source[at]!='\n')++at;
        const size_t end=at;if(at<length)++at;
        size_t p=start;while(p<end&&(source[p]==' '||source[p]=='\t'||source[p]=='\r'))++p;
        if(p==end)continue;
        if(p+2>end||source[p]!='-'||source[p+1]!='-')break;
        p+=2;while(p<end&&source[p]==' ')++p;
        if(p>=end||source[p++]!='@')continue;
        const size_t key=p;while(p<end&&source[p]!=':')++p;
        if(p==end)continue;
        const size_t keyLength=p-key;++p;
        if(keyLength==4&&!memcmp(source+key,"name",4))copyAscii(entry.title,sizeof entry.title,source+p,end-p);
        else if(keyLength==11&&!memcmp(source+key,"description",11))copyAscii(entry.description,sizeof entry.description,source+p,end-p);
        else if(keyLength==8&&!memcmp(source+key,"category",8)) {
            char category[12]={};if(!copyAscii(category,sizeof category,source+p,end-p))continue;
            if(!strcmp(category,"Games"))entry.category=Category::Games;
            else if(!strcmp(category,"Radio"))entry.category=Category::Radio;
            else if(!strcmp(category,"Tools"))entry.category=Category::Tools;
        }
    }
}
inline int findName(const char names[][41],size_t count,const char* name) {
    for(size_t i=0;i<count;++i)if(!strcmp(names[i],name))return int(i);
    return -1;
}
inline bool favorite(const Preferences& prefs,const char* name) {
    return findName(prefs.favorites,prefs.favoriteCount,name)>=0;
}
inline bool toggleFavorite(Preferences& prefs,const char* name) {
    if(!validAppFilename(name))return false;
    const int found=findName(prefs.favorites,prefs.favoriteCount,name);
    if(found>=0) {
        for(size_t i=size_t(found)+1;i<prefs.favoriteCount;++i)memcpy(prefs.favorites[i-1],prefs.favorites[i],41);
        --prefs.favoriteCount;return true;
    }
    if(prefs.favoriteCount==8)return false;
    snprintf(prefs.favorites[prefs.favoriteCount++],41,"%s",name);return true;
}
inline void recordRecent(Preferences& prefs,const char* name) {
    if(!validAppFilename(name))return;
    const int old=findName(prefs.recent,prefs.recentCount,name);
    if(old<0&&prefs.recentCount<4)++prefs.recentCount;
    const size_t last=old>=0?size_t(old):prefs.recentCount-1;
    for(size_t i=last;i>0;--i)memcpy(prefs.recent[i],prefs.recent[i-1],41);
    snprintf(prefs.recent[0],41,"%s",name);
}
inline bool preferenceLine(Preferences& prefs,const char* line) {
    if(strlen(line)<3||line[1]!=' '||!validAppFilename(line+2))return false;
    if(line[0]=='F') {
        if(favorite(prefs,line+2))return true;
        return toggleFavorite(prefs,line+2);
    }
    if(line[0]=='R') {
        if(findName(prefs.recent,prefs.recentCount,line+2)>=0)return true;
        if(prefs.recentCount==4)return false;
        snprintf(prefs.recent[prefs.recentCount++],41,"%s",line+2);return true;
    }
    return false;
}
inline size_t visible(const Entry* entries,size_t count,const Preferences& prefs,Section section,uint8_t indices[32]) {
    if(count>32)count=32;
    size_t used=0;
    if(section==Section::Recent) {
        for(size_t r=0;r<prefs.recentCount;++r)for(size_t i=0;i<count;++i)
            if(!strcmp(prefs.recent[r],entries[i].file))indices[used++]=uint8_t(i);
        return used;
    }
    for(size_t i=0;i<count;++i) {
        const bool match=section==Section::All||
            (section==Section::Tools&&entries[i].category==Category::Tools)||
            (section==Section::Games&&entries[i].category==Category::Games)||
            (section==Section::Radio&&entries[i].category==Category::Radio)||
            (section==Section::Favorites&&favorite(prefs,entries[i].file));
        if(match)indices[used++]=uint8_t(i);
    }
    return used;
}
inline size_t firstRow(size_t selected,size_t count,size_t rows=4) {
    if(!rows||count<=rows)return 0;
    if(selected>=count)selected=count-1;
    const size_t first=rows>1&&selected?selected-1:selected;
    return first>count-rows?count-rows:first;
}
} }
