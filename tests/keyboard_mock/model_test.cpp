#include "keyboard_model.h"
#include "production_default.inc"
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
using namespace keyboard;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return false;}}while(0)
static bool select(Model& model,size_t index) {
    for(unsigned i=0;i<32 && model.selected()!=index;++i)model.move(1);
    CHECK(model.selected()==index);return true;
}
static std::string unique(const std::string& alphabet,bool includeSpace) {
    std::string result;
    for(size_t i=0;i<alphabet.size();++i)
        if((includeSpace||alphabet[i]!=' ')&&result.find(alphabet[i])==std::string::npos)result+=alphabet[i];
    return result;
}
static bool pageOrder(const char* alphabet,size_t bytes) {
    char buffer[161]={};Model model(buffer,sizeof buffer,alphabet,bytes);CHECK(model.valid());
    const std::string ordered=unique(std::string(alphabet,bytes),false);std::string collected;
    CHECK(model.pages()==std::max(size_t(1),(ordered.size()+27)/28));
    for(size_t page=0;page<model.pages();++page) {
        CHECK(model.page()==page);
        for(size_t i=0;i<28;++i) {
            const Cell cell=model.cell(i);const size_t at=page*28+i;
            if(at<ordered.size()){CHECK(cell.kind==Kind::Character&&cell.enabled);CHECK(cell.character==ordered[at]);collected+=cell.character;}
            else CHECK(cell.kind==Kind::Empty&&!cell.enabled);
        }
        CHECK(model.cell(28).kind==Kind::Space);CHECK(model.cell(28).enabled==(std::string(alphabet,bytes).find(' ')!=std::string::npos));
        CHECK(model.cell(29).kind==Kind::Delete&&!model.cell(29).enabled);
        CHECK(model.cell(30).kind==Kind::Next&&model.cell(30).enabled==(model.pages()>1));
        CHECK(model.cell(31).kind==Kind::Done&&model.cell(31).enabled);
        model.changePage(1);
    }
    CHECK(model.page()==0);CHECK(collected==ordered);CHECK(model.outcome()==Outcome::Editing);CHECK(buffer[0]==0);
    CHECK(model.cell(32).kind==Kind::Empty);CHECK(model.cell(SIZE_MAX).kind==Kind::Empty);
    return true;
}
static bool boundaries() {
    struct Guarded {unsigned char before[8];char text[4];unsigned char after[8];} guarded;
    memset(&guarded,0xA5,sizeof guarded);memset(guarded.text,0,sizeof guarded.text);
    Model model(guarded.text,sizeof guarded.text,"AB",2);CHECK(model.valid());
    CHECK(model.activate()==Result::Changed);CHECK(model.activate()==Result::Changed);CHECK(model.activate()==Result::Changed);
    CHECK(!strcmp(guarded.text,"AAA")&&model.length()==3);
    CHECK(model.activate()==Result::Full);CHECK(!strcmp(guarded.text,"AAA"));
    for(unsigned i=0;i<8;++i){CHECK(guarded.before[i]==0xA5);CHECK(guarded.after[i]==0xA5);}
    CHECK(select(model,28));CHECK(model.activate()==Result::Unavailable);CHECK(!strcmp(guarded.text,"AAA"));
    CHECK(select(model,29));CHECK(model.activate()==Result::Changed);CHECK(!strcmp(guarded.text,"AA"));
    CHECK(model.erase()==Result::Changed);CHECK(model.erase()==Result::Changed);CHECK(model.erase()==Result::Unavailable);CHECK(guarded.text[0]==0);
    CHECK(select(model,31));CHECK(model.activate()==Result::Accepted);CHECK(model.outcome()==Outcome::Accepted);
    const size_t selected=model.selected(),page=model.page();model.move(1);
    CHECK(model.selected()==selected);CHECK(model.changePage(1)==Result::None&&model.page()==page);
    CHECK(model.activate()==Result::None&&model.erase()==Result::None&&model.cancel()==Result::None&&model.accept()==Result::None);
    char single[1]={0};Model full(single,1,"X",1);CHECK(full.valid());CHECK(full.activate()==Result::Full);CHECK(single[0]==0);
    char space[4]={0};Model spaces(space,sizeof space," ",1);CHECK(spaces.valid()&&spaces.pages()==1&&spaces.selected()==28);
    CHECK(spaces.activate()==Result::Changed);CHECK(!strcmp(space," "));spaces.move(-1);CHECK(spaces.selected()==31);
    CHECK(spaces.changePage(1)==Result::Unavailable);CHECK(spaces.cancel()==Result::Cancelled);CHECK(!strcmp(space," "));
    CHECK(spaces.outcome()==Outcome::Cancelled);CHECK(spaces.activate()==Result::None&&spaces.accept()==Result::None);
    char ascii[95];for(unsigned i=0;i<95;++i)ascii[i]=char(i+32);
    char text[161];memset(text,'A',160);text[160]=0;Model longText(text,sizeof text,ascii,sizeof ascii);CHECK(longText.valid());
    CHECK(longText.tail(20)==text+140);CHECK(longText.tail(31)==text+129);CHECK(longText.tail(0)==text+160);CHECK(longText.tail(SIZE_MAX)==text);
    CHECK(strlen(longText.tail(20))==20);CHECK(longText.length()==160);CHECK(longText.activate()==Result::Full);
    longText.move(27);CHECK(longText.selected()==27);CHECK(longText.changePage(-1)==Result::PageChanged);
    CHECK(longText.page()==3&&longText.selected()==0);CHECK(select(longText,30));CHECK(longText.activate()==Result::PageChanged);CHECK(longText.page()==0&&longText.selected()==30);
    longText.move(INT_MIN);longText.move(INT_MAX);CHECK(longText.selected()<32&&longText.cell(longText.selected()).kind!=Kind::Empty);
    longText.changePage(INT_MIN);longText.changePage(INT_MAX);CHECK(longText.page()<longText.pages());
    return true;
}
static bool invalidInputs() {
    char text[4]={'x',0,'Z','Z'};char absent[4]={'a','a','a','a'};
    const char* invalidAlphabet[]={"", "a\n", "a\t", "a\x7F", "\xD0\xB0", "\xF0\x9F\x98\x80"};
    for(unsigned i=0;i<sizeof invalidAlphabet/sizeof *invalidAlphabet;++i) {
        Model model(text,sizeof text,invalidAlphabet[i],strlen(invalidAlphabet[i]));CHECK(!model.valid());
        CHECK(model.activate()==Result::None&&model.erase()==Result::None&&model.accept()==Result::None&&model.cancel()==Result::None);
        CHECK(!strcmp(model.tail(20),""));CHECK(text[0]=='x'&&text[2]=='Z');
    }
    Model nullBuffer(nullptr,1,"a",1);CHECK(!nullBuffer.valid());Model nullAlphabet(text,4,nullptr,1);CHECK(!nullAlphabet.valid());
    Model zeroCapacity(text,0,"x",1);CHECK(!zeroCapacity.valid());
    Model unterminated(absent,sizeof absent,"a",1);CHECK(!unterminated.valid());CHECK(!memcmp(absent,"aaaa",4));
    Model wrongInitial(text,4,"a",1);CHECK(!wrongInitial.valid());CHECK(text[0]=='x');
    const char nulAlphabet[]={'x',0,'y'};Model embedded(text,4,nulAlphabet,3);CHECK(!embedded.valid());
    const std::string oversized(96,'x');Model tooLarge(text,4,oversized.data(),oversized.size());CHECK(!tooLarge.valid());
    const char exact[]={'x','y'};Model notTerminatedAlphabet(text,4,exact,2);CHECK(notTerminatedAlphabet.valid());
    return true;
}
static bool randomized() {
    const char* alphabets[]={"0123456789","0123456789ABCDEF","abcdefghijklmnopqrstuvwxyz0123456789_-","abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-",productionDefault,"aa bb!a"};
    uint32_t random=0xA5C03211;
    for(unsigned set=0;set<6;++set)for(unsigned run=0;run<100;++run) {
        struct Guarded {uint32_t before;char text[33];uint32_t after;} memory={0xA5C07843,{0},0x4387C0A5};
        const size_t capacity=1+run%33;Model model(memory.text,capacity,alphabets[set],strlen(alphabets[set]));CHECK(model.valid());
        std::string expected;
        for(unsigned step=0;step<300;++step) {
            random=random*1664525+1013904223;const unsigned operation=random%6;
            const Cell cell=model.cell(model.selected());const Outcome before=model.outcome();Result result=Result::None;
            if(operation==0)model.move((random&0x100)?-1:1);
            else if(operation==1)result=model.changePage((random&0x200)?-1:1);
            else if(operation==2) {
                result=model.activate();
                if(before==Outcome::Editing && cell.enabled) {
                    if(cell.kind==Kind::Character||cell.kind==Kind::Space) {
                        if(expected.size()<capacity-1){expected+=cell.character;CHECK(result==Result::Changed);}
                        else CHECK(result==Result::Full);
                    } else if(cell.kind==Kind::Delete){expected.resize(expected.size()-1);CHECK(result==Result::Changed);}
                    else if(cell.kind==Kind::Done)CHECK(result==Result::Accepted);
                }
            } else if(operation==3) {
                result=model.erase();
                if(before==Outcome::Editing && !expected.empty()){expected.resize(expected.size()-1);CHECK(result==Result::Changed);}
            } else if(operation==4&&step>250)result=model.accept();
            else if(operation==5&&step>250)result=model.cancel();
            if(before!=Outcome::Editing)CHECK(model.outcome()==before&&result==Result::None);
            CHECK(model.length()==expected.size());CHECK(!strcmp(memory.text,expected.c_str()));
            CHECK(model.selected()<32&&model.cell(model.selected()).kind!=Kind::Empty);CHECK(model.page()<model.pages());
            CHECK(memory.before==0xA5C07843&&memory.after==0x4387C0A5);CHECK(memory.text[capacity-1]==0);
            for(size_t i=0;i<expected.size();++i)CHECK(strchr(alphabets[set],expected[i])!=nullptr);
        }
    }
    return true;
}
static bool tests() {
    const char original[]=" abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-!?/@";
    std::string oldSet=unique(original,true),newSet=unique(productionDefault,true);
    std::sort(oldSet.begin(),oldSet.end());std::sort(newSet.begin(),newSet.end());
    CHECK(oldSet==newSet);CHECK(strlen(productionDefault)==strlen(original));
    char ascii[95];for(unsigned i=0;i<95;++i)ascii[i]=char(i+32);
    if(!pageOrder(ascii,sizeof ascii)||!pageOrder(productionDefault,strlen(productionDefault))||!pageOrder("aa bb!a",7)||!boundaries()||!invalidInputs()||!randomized())return false;
    std::printf("Keyboard model: %u checks passed (production model, real caller default alphabet, no device)\n",checks);
    return true;
}
int main(){return tests()?0:1;}
