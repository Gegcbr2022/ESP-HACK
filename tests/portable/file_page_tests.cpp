#include "file_page.h"
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
    std::printf("file paging: %u checks passed\n",checks);
}
