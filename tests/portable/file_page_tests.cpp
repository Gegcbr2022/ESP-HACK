#include "file_page.h"
#include "file_entry.h"
#include <cstdio>
#include <cstring>
#include <string>
using namespace research;
static unsigned checks;
#define CHECK(test) do { ++checks; if(!(test)) { std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#test); return 1; } } while(0)
int main() {
    for(unsigned total=0;total<=1000;++total) {
        unsigned collected=0;
        const unsigned pages=total?(total+38)/39:1;
        for(unsigned page=0;page<pages;++page) {
            FilePage selection(page);unsigned stored=0;
            for(unsigned item=0;item<total;++item)if(selection.consider()) {
                CHECK(item==collected++);++stored;CHECK(stored<=39);
            }
            CHECK(stored==(total-page*39<39?total-page*39:39));
            CHECK(selection.more()==(page+1<pages));CHECK(stored+unsigned(selection.more())<=40);
            CHECK(selection.lastPage()==(total?(total-1)/39:0));
        }
        CHECK(collected==total);
    }
    FilePage excessive(UINT32_MAX);for(unsigned i=0;i<42;++i)CHECK(!excessive.consider());
    CHECK(excessive.lastPage()==1&&!excessive.more());
    struct Guarded{uint32_t before;char text[112];uint32_t after;} path;
    std::memset(&path,0x5A,sizeof path);path.before=path.after=0x12345678;
    CHECK(completeFilePath(path.text,sizeof path.text,"/subghz","Дверь (1).sub"));
    CHECK(!std::strcmp(path.text,"/subghz/Дверь (1).sub"));
    CHECK(completeFilePath(path.text,sizeof path.text,"/","A B (2).sub"));CHECK(!std::strcmp(path.text,"/A B (2).sub"));
    CHECK(completeFilePath(path.text,sizeof path.text,"/subghz/","x.sub"));CHECK(!std::strcmp(path.text,"/subghz/x.sub"));
    for(unsigned bytes=1;bytes<=220;++bytes) {
        const std::string leaf(bytes,'a');std::memset(path.text,0x5A,sizeof path.text);
        const bool fits=8+bytes<sizeof path.text;
        CHECK(completeFilePath(path.text,sizeof path.text,"/subghz",leaf.c_str())==fits);
        if(fits){CHECK(std::strlen(path.text)==8+bytes);CHECK(!std::strcmp(path.text+8,leaf.c_str()));}
        else for(unsigned i=0;i<sizeof path.text;++i)CHECK(path.text[i]==0x5A);
        CHECK(path.before==0x12345678&&path.after==0x12345678);
    }
    char small[8]="unchang";CHECK(!completeFilePath(small,sizeof small,"/subghz","x"));CHECK(!std::strcmp(small,"unchang"));
    CHECK(!completeFilePath(nullptr,112,"/subghz","x"));CHECK(!completeFilePath(path.text,112,"","x"));
    CHECK(!completeFilePath(path.text,112,"/subghz",""));
    // Long real leaf names stay on disk; lightweight previews never substitute
    // for their identity or the full selected path.
    static_assert(sizeof(FileEntry)<=80,"A file page must not store full long paths");
    CHECK(sizeof(FileEntry)*39+2*kRadioPathCapacity<6000);
    const std::string russian=std::string(200,'A')+" Дверь (24bit) .SUB";
    const std::string unicode="Вход "+std::string(80,'x')+" \xe4\xb8\xad.sub";
    const std::string names[]={"Door..static (24).sub",russian,unicode,std::string(765,'z')};
    for(unsigned i=0;i<sizeof names/sizeof names[0];++i) {
        FileEntry entry={};CHECK(setFileEntry(entry,names[i].c_str(),i,i==2));
        CHECK(sameFileEntry(entry,names[i].c_str(),i,i==2));
        CHECK(!sameFileEntry(entry,(names[i]+"x").c_str(),i,i==2));
        CHECK(!sameFileEntry(entry,names[i].c_str(),i+1,i==2));
        CHECK(!sameFileEntry(entry,names[i].c_str(),i,i!=2));
        size_t glyphs=0;CHECK(ui_text::validate(entry.preview,std::strlen(entry.preview),&glyphs));CHECK(glyphs<=18);
        char selected[kRadioPathCapacity];CHECK(completeFilePath(selected,sizeof selected,"/subghz",names[i].c_str()));
        CHECK(!std::strcmp(selected+8,names[i].c_str()));CHECK(radioFilePath(selected));
    }
    struct LongGuard {uint32_t first;char text[kRadioPathCapacity];uint32_t last;} longPath;
    longPath.first=longPath.last=0x12345678;
    for(unsigned bytes=100;bytes<=1050;++bytes) {
        std::memset(longPath.text,0x5A,sizeof longPath.text);const std::string leaf(bytes,'L');
        const bool fit=bytes+8<sizeof longPath.text;
        CHECK(completeFilePath(longPath.text,sizeof longPath.text,"/subghz",leaf.c_str())==fit);
        if(fit){CHECK(!std::strcmp(longPath.text+8,leaf.c_str()));CHECK(radioFilePath(longPath.text));}
        else for(size_t i=0;i<sizeof longPath.text;++i)CHECK(longPath.text[i]==0x5A);
        CHECK(longPath.first==0x12345678&&longPath.last==0x12345678);
    }
    CHECK(radioFilePath("/subghz",true));CHECK(!radioFilePath("/subghz"));
    CHECK(radioFilePath("/subghz/door..static.sub"));
    const char* badPaths[]={"/subghzx/a.sub","/subghz/../a.sub","/subghz/./a.sub","/subghz//a.sub","/subghz/a/../b.sub","/subghz/a\\b.sub","/subghz/"};
    for(const char* invalid:badPaths)CHECK(!radioFilePath(invalid));
    FileEntry unchanged={};CHECK(setFileEntry(unchanged,"unchanged",2,false));const FileEntry saved=unchanged;
    CHECK(!setFileEntry(unchanged,std::string(1024,'x').c_str(),0,true));CHECK(!std::memcmp(&saved,&unchanged,sizeof saved));
    std::printf("file paging: %u checks passed\n",checks);
}
