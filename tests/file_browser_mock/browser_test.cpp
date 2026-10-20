#include "file_entry.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace research;
struct MockEntry {std::string name;bool directory;};
static std::map<std::string,std::vector<MockEntry>> tree;
static unsigned checks,yields;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
class String {
    std::string value;
public:
    String(){}String(const std::string& text):value(text){}
    size_t length() const{return value.length();}
    const char* c_str() const{return value.c_str();}
};
class File {
    const std::vector<MockEntry>* entries=nullptr;
    size_t cursor=0;
    std::string folder;
public:
    File(){}File(const std::string& path):folder(path){auto it=tree.find(path);if(it!=tree.end())entries=&it->second;}
    explicit operator bool() const{return entries!=nullptr;}
    String getNextFileName(bool* directory) {
        if(!entries||cursor>=entries->size())return String();
        const MockEntry& entry=(*entries)[cursor++];*directory=entry.directory;
        return String(folder+"/"+entry.name);
    }
    void close(){entries=nullptr;}
};
struct MockSD {File open(const char* path){return File(path);}} SD;
void delay(unsigned){++yields;}
FileEntry fileEntries[FilePage::kEntries];
bool fileHasNext,folderOpenError;
size_t pathCount,fileEntryCount;
uint64_t matchingFileCount;
uint32_t filePage,longPathCount;
char currentFolder[kRadioPathCapacity]="/subghz";
char selectedFilePath[kRadioPathCapacity];
char filter[32]="";
// PRODUCTION_FUNCTIONS
int main() {
    const unsigned sizes[]={0,1,38,39,40,78,79,300,1000};
    for(unsigned total:sizes) {
        tree.clear();auto& entries=tree["/subghz"];
        for(unsigned i=0;i<total;++i)entries.push_back({"file ("+std::to_string(i)+").SUB",i%11==0});
        unsigned covered=0;
        const unsigned pages=total?(total+38)/39:1;
        for(filePage=0;filePage<pages;++filePage) {
            listFiles();CHECK(!folderOpenError&&matchingFileCount==total);
            CHECK(fileHasNext==(filePage+1<pages));CHECK(pathCount==fileEntryCount+unsigned(fileHasNext));
            CHECK(fileEntryCount<=39);
            for(size_t i=0;i<fileEntryCount;++i) {
                CHECK(resolveFileEntry(i));
                CHECK(std::string(selectedFilePath)=="/subghz/"+entries[covered].name);
                CHECK(fileEntries[i].directory==entries[covered].directory);++covered;
            }
            CHECK(!resolveFileEntry(fileEntryCount));CHECK(selectedFilePath[0]==0);
        }
        CHECK(covered==total);
    }
    CHECK(yields>0);
    tree.clear();tree["/subghz"]={{".hidden",false},{std::string(200,'A')+" Дверь (24 bit).sub",false},{std::string(765,'x'),true},{"door..static.sub",false}};
    filePage=0;listFiles();CHECK(matchingFileCount==3&&fileEntryCount==3);
    CHECK(resolveFileEntry(0));CHECK(std::strlen(selectedFilePath)>111);
    CHECK(std::string(selectedFilePath)=="/subghz/"+tree["/subghz"][1].name);
    CHECK(resolveFileEntry(1));CHECK(std::strlen(selectedFilePath)==773);
    CHECK(resolveFileEntry(2));CHECK(std::string(selectedFilePath)=="/subghz/door..static.sub");
    // The preview can be identical; the original name/ordinal must be verified.
    tree["/subghz"][1].name+="changed";CHECK(!resolveFileEntry(0));CHECK(selectedFilePath[0]==0);
    listFiles();tree["/subghz"].erase(tree["/subghz"].begin());CHECK(!resolveFileEntry(0));
    filePage=1000;listFiles();CHECK(filePage==0&&fileEntryCount==3);
    std::snprintf(filter,sizeof filter,"static");listFiles();CHECK(fileEntryCount==1&&matchingFileCount==1);
    CHECK(resolveFileEntry(0));CHECK(std::string(selectedFilePath)=="/subghz/door..static.sub");filter[0]=0;
    tree["/subghz"]={{std::string(1016,'x'),false},{std::string(1015,'y'),false}};listFiles();
    CHECK(longPathCount==1&&fileEntryCount==1);CHECK(resolveFileEntry(0));CHECK(std::strlen(selectedFilePath)==1023);
    std::snprintf(currentFolder,sizeof currentFolder,"/subghz/%s",std::string(700,'n').c_str());
    tree[currentFolder]={{std::string(300,'z'),false}};listFiles();CHECK(fileEntryCount==1&&resolveFileEntry(0));
    CHECK(std::strlen(selectedFilePath)==1009);
    std::strcpy(currentFolder,"/subghz/missing");listFiles();CHECK(folderOpenError&&pathCount==0);
    std::printf("Actual file browser: %u checks PASS (long UTF8 paths, page coverage, identity changes, filters, limits)\n",checks);
}
