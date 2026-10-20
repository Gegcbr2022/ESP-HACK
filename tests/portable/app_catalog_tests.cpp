#include "research/app_catalog.h"
#include <stdio.h>
#include <string>

using namespace research::apps;
static unsigned checks;
#define CHECK(value) do { ++checks; if(!(value)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#value); return false; } } while(0)

static bool paths_and_metadata() {
    CHECK(validAppFilename("x.lua")); CHECK(validAppFilename("Tool_2-x.lua")); CHECK(!validAppFilename(nullptr));
    const char* bad[]={".lua","x.LUA","../x.lua","x/y.lua","x\\y.lua","x..lua","x.lua/..","C:x.lua","x\n.lua","x lua"};
    for(size_t i=0;i<sizeof bad/sizeof *bad;++i) CHECK(!validAppFilename(bad[i]));
    const std::string limit=std::string(36,'a')+".lua";
    CHECK(validAppFilename(limit.c_str())); CHECK(!validAppFilename(("a"+limit).c_str()));
    Entry entry; initialize(entry,"field_tool.lua",1234);
    CHECK(!strcmp(entry.file,"field_tool.lua")); CHECK(!strcmp(entry.title,"field_tool"));
    CHECK(entry.bytes==1234&&entry.category==Category::Tools); CHECK(strstr(entry.description,"BACK"));
    metadata(entry,nullptr,0); CHECK(!strcmp(entry.title,"field_tool"));
    const char source[]="\xEF\xBB\xBF\n \t-- @name:  Friendly tool \t\r\n-- @category: Games\n"
        "-- @description: Useful description\r\napp={}\n-- @name: Must not apply\n";
    metadata(entry,source,sizeof source-1); CHECK(!strcmp(entry.title,"Friendly tool"));
    CHECK(!strcmp(entry.description,"Useful description")); CHECK(entry.category==Category::Games);
    const char invalid[]="-- @category: Unknown\n-- @name: Bad\tinterior\n-- @description: bad\x01text\n";
    metadata(entry,invalid,sizeof invalid-1); CHECK(!strcmp(entry.title,"Friendly tool"));
    CHECK(!strcmp(entry.description,"Useful description")); CHECK(entry.category==Category::Games);
    std::string exact="-- @name: "+std::string(21,'N')+"\n-- @description: "+std::string(64,'D')+"\n";
    metadata(entry,exact.data(),exact.size()); CHECK(strlen(entry.title)==21); CHECK(strlen(entry.description)==64);
    exact="-- @name: "+std::string(22,'X')+"\n-- @description: "+std::string(65,'X')+"\n";
    metadata(entry,exact.data(),exact.size()); CHECK(entry.title[0]=='N'&&strlen(entry.title)==21);
    CHECK(entry.description[0]=='D'&&strlen(entry.description)==64);
    const char russian[]="-- @name: Секундомер\n-- @description: Удерживайте BACK для выхода\n";
    metadata(entry,russian,sizeof russian-1);CHECK(!strcmp(entry.title,"Секундомер"));
    CHECK(!strcmp(entry.description,"Удерживайте BACK для выхода"));
    std::string ruLimit="-- @name: ";for(unsigned i=0;i<21;++i)ruLimit+="Я";
    ruLimit+="\n-- @description: ";for(unsigned i=0;i<64;++i)ruLimit+="ё";ruLimit+='\n';
    metadata(entry,ruLimit.data(),ruLimit.size());CHECK(strlen(entry.title)==42);CHECK(strlen(entry.description)==128);
    const char malformed[]="-- @name: bad\xD0\n-- @description: \xC0\xAF\n";
    metadata(entry,malformed,sizeof malformed-1);CHECK(strlen(entry.title)==42);CHECK(strlen(entry.description)==128);
    std::string tooMany="-- @name: ";for(unsigned i=0;i<22;++i)tooMany+="я";tooMany+='\n';
    metadata(entry,tooMany.data(),tooMany.size());CHECK(strlen(entry.title)==42);
    CHECK(!strcmp(sectionNameRu(Section::Favorites),"Избранное"));
    initialize(entry,"x.lua",1); const std::string late="--"+std::string(1022,' ')+"\n-- @name: Too late\n";
    metadata(entry,late.data(),late.size()); CHECK(!strcmp(entry.title,"x"));
    const std::string lines=std::string(32,'\n')+"-- @name: Too late\n";
    metadata(entry,lines.data(),lines.size()); CHECK(!strcmp(entry.title,"x"));
    const char code[]="app={}\n-- @category: Radio";
    metadata(entry,code,sizeof code-1); CHECK(entry.category==Category::Tools);
    initialize(entry,limit.c_str(),UINT32_MAX); CHECK(strlen(entry.file)==40); CHECK(strlen(entry.title)==21);
    CHECK(entry.bytes==UINT32_MAX); CHECK(!strcmp(sectionName(Section(255)),"All apps"));
    return true;
}
static bool preferences_and_filters() {
    Preferences prefs={}; char name[41];
    for(unsigned i=0;i<8;++i) { snprintf(name,sizeof name,"a%u.lua",i); CHECK(toggleFavorite(prefs,name)); }
    CHECK(prefs.favoriteCount==8); CHECK(!toggleFavorite(prefs,"overflow.lua")); CHECK(!favorite(prefs,"overflow.lua"));
    CHECK(toggleFavorite(prefs,"a3.lua")); CHECK(prefs.favoriteCount==7); CHECK(!favorite(prefs,"a3.lua"));
    CHECK(!strcmp(prefs.favorites[3],"a4.lua")); CHECK(toggleFavorite(prefs,"new.lua"));
    CHECK(preferenceLine(prefs,"F a0.lua")); CHECK(prefs.favoriteCount==8);
    CHECK(!preferenceLine(prefs,"F ../../bad.lua")); CHECK(!preferenceLine(prefs,"R ../bad.lua"));
    CHECK(!preferenceLine(prefs,"X x.lua")); CHECK(!preferenceLine(prefs,"F"));
    recordRecent(prefs,"a0.lua"); recordRecent(prefs,"a1.lua"); recordRecent(prefs,"a2.lua");
    recordRecent(prefs,"a3.lua"); recordRecent(prefs,"a4.lua"); CHECK(prefs.recentCount==4);
    CHECK(!strcmp(prefs.recent[0],"a4.lua")); CHECK(!strcmp(prefs.recent[3],"a1.lua"));
    recordRecent(prefs,"a2.lua"); CHECK(prefs.recentCount==4); CHECK(!strcmp(prefs.recent[0],"a2.lua"));
    CHECK(!strcmp(prefs.recent[1],"a4.lua")); CHECK(!strcmp(prefs.recent[2],"a3.lua"));
    recordRecent(prefs,"../bad.lua"); CHECK(prefs.recentCount==4);
    Preferences disk={}; CHECK(preferenceLine(disk,"R a2.lua")); CHECK(preferenceLine(disk,"R a4.lua"));
    CHECK(preferenceLine(disk,"R a3.lua")); CHECK(preferenceLine(disk,"R a1.lua"));
    CHECK(preferenceLine(disk,"R a4.lua")); CHECK(disk.recentCount==4); CHECK(!strcmp(disk.recent[0],"a2.lua"));
    CHECK(!preferenceLine(disk,"R more.lua"));
    Entry entries[40];
    for(unsigned i=0;i<40;++i) {
        snprintf(name,sizeof name,"a%u.lua",i); initialize(entries[i],name,i);
        entries[i].category=Category(i%3);
    }
    uint8_t indices[32]; memset(indices,0xFF,sizeof indices);
    CHECK(visible(entries,40,prefs,Section::All,indices)==32); CHECK(indices[0]==0&&indices[31]==31);
    CHECK(visible(entries,40,prefs,Section::Tools,indices)==11); CHECK(indices[0]==0&&indices[10]==30);
    CHECK(visible(entries,40,prefs,Section::Games,indices)==11); CHECK(indices[0]==1&&indices[10]==31);
    CHECK(visible(entries,40,prefs,Section::Radio,indices)==10); CHECK(indices[0]==2&&indices[9]==29);
    CHECK(visible(entries,40,prefs,Section::Favorites,indices)==7); CHECK(indices[0]==0&&indices[3]==4);
    CHECK(visible(entries,40,prefs,Section::Recent,indices)==4);
    CHECK(indices[0]==2&&indices[1]==4&&indices[2]==3&&indices[3]==1);
    CHECK(visible(nullptr,0,prefs,Section::All,indices)==0); CHECK(visible(entries,40,prefs,Section(255),indices)==0);
    CHECK(firstRow(3,10,1)==3); CHECK(firstRow(5,10,0)==0);
    for(size_t count=0;count<=40;++count) for(size_t rows=1;rows<=8;++rows) {
        for(size_t selected=0;selected<count+5;++selected) {
            const size_t first=firstRow(selected,count,rows);
            if(count<=rows) CHECK(first==0);
            else CHECK(first<=count-rows);
            if(count) {
                const size_t clamped=selected<count?selected:count-1;
                CHECK(first<=clamped&&clamped<first+rows);
            }
        }
    }
    return true;
}
static bool printable(const char* text,size_t capacity) {
    const char* end=static_cast<const char*>(memchr(text,0,capacity));
    if(!end) return false;
    return ui_text::validate(text,size_t(end-text));
}
static bool adversarial_metadata_and_preferences() {
    struct Guarded { uint8_t before[16]; Entry entry; uint8_t after[16]; } guarded;
    uint32_t seed=0xCE891203;
    const std::string golden="-- @name: Tool\n-- @category: Games\n-- @description: Safe description\napp={}\n";
    for(unsigned trial=0;trial<10000;++trial) {
        memset(&guarded,0xA5,sizeof guarded); initialize(guarded.entry,"safe.lua",42);
        std::string source=golden;
        for(unsigned j=0;j<4;++j) { seed=seed*1664525u+1013904223u; source[seed%source.size()]=char(seed>>24); }
        metadata(guarded.entry,source.data(),source.size());
        CHECK(!strcmp(guarded.entry.file,"safe.lua")&&guarded.entry.bytes==42);
        CHECK(printable(guarded.entry.title,sizeof guarded.entry.title));
        CHECK(printable(guarded.entry.description,sizeof guarded.entry.description));
        CHECK(uint8_t(guarded.entry.category)<=2);
        for(unsigned i=0;i<16;++i) { CHECK(guarded.before[i]==0xA5); CHECK(guarded.after[i]==0xA5); }
    }
    Preferences prefs={}; char name[41],line[44];
    for(unsigned trial=0;trial<10000;++trial) {
        seed=seed*1664525u+1013904223u; snprintf(name,sizeof name,"t%u.lua",unsigned(seed%12));
        switch((seed>>16)%4) {
        case 0: (void)toggleFavorite(prefs,name); break;
        case 1: recordRecent(prefs,name); break;
        default: snprintf(line,sizeof line,"%c %s",seed&1?'R':'F',name); (void)preferenceLine(prefs,line); break;
        }
        CHECK(prefs.favoriteCount<=8&&prefs.recentCount<=4);
        for(size_t i=0;i<prefs.favoriteCount;++i) {
            CHECK(validAppFilename(prefs.favorites[i]));
            for(size_t j=0;j<i;++j) CHECK(strcmp(prefs.favorites[i],prefs.favorites[j])!=0);
        }
        for(size_t i=0;i<prefs.recentCount;++i) {
            CHECK(validAppFilename(prefs.recent[i]));
            for(size_t j=0;j<i;++j) CHECK(strcmp(prefs.recent[i],prefs.recent[j])!=0);
        }
    }
    return true;
}
int main() {
    if(!paths_and_metadata()||!preferences_and_filters()||!adversarial_metadata_and_preferences()) return 1;
    printf("App catalog: %u checks PASS, 10000 metadata mutations and 10000 preference operations\n",checks);
    return 0;
}
