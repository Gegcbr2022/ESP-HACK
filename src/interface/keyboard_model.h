#pragma once
#include <stddef.h>
#include <stdint.h>

namespace keyboard {
enum class Kind : uint8_t { Empty, Character, Space, Delete, Next, Done };
enum class Result : uint8_t { None, Changed, PageChanged, Full, Unavailable, Accepted, Cancelled };
enum class Outcome : uint8_t { Editing, Accepted, Cancelled };
struct Cell { Kind kind;char character;bool enabled; };

// ASCII-only editing state, no display, heap, radio, UART or automatic send.
// Caller exclusively owns a writable buffer of capacity bytes, including NUL.
// Initial text must terminate within capacity and use the supplied alphabet.
// Alphabet is1..95 printableASCII bytes; duplicates are removed in supplied
// order. Space is represented only by the dedicated action when permitted.
// Cancel freezes the draft and marks it cancelled; caller discards that draft.
class Model {
public:
    static const size_t kColumns=8,kRows=4,kCells=32,kCharactersPerPage=28;
    Model(char* buffer,size_t capacity,const char* alphabet,size_t alphabetBytes)
        : buffer_(buffer),capacity_(capacity),length_(0),characters_(0),page_(0),
          pages_(1),selected_(0),space_(false),valid_(false),outcome_(Outcome::Editing) {
        if(!buffer || !capacity || !alphabet || !alphabetBytes || alphabetBytes>95)return;
        bool allowed[95]={};
        for(size_t i=0;i<alphabetBytes;++i) {
            const unsigned byte=static_cast<unsigned char>(alphabet[i]);
            if(byte<32 || byte>126)return;
            if(allowed[byte-32])continue;
            allowed[byte-32]=true;
            if(byte==32)space_=true;
            else alphabet_[characters_++]=char(byte);
        }
        size_t length=0;
        for(;length<capacity && buffer[length];++length) {
            const unsigned byte=static_cast<unsigned char>(buffer[length]);
            if(byte<32 || byte>126 || !allowed[byte-32])return;
        }
        if(length==capacity)return;
        length_=length;
        pages_=characters_?(characters_+kCharactersPerPage-1)/kCharactersPerPage:1;
        selected_=characters_?0:28;
        valid_=true;
    }
    bool valid() const {return valid_;}
    size_t length() const {return length_;}
    size_t page() const {return page_;}
    size_t pages() const {return pages_;}
    size_t selected() const {return selected_;}
    Outcome outcome() const {return outcome_;}
    const char* tail(size_t maxCharacters) const {
        if(!valid_)return "";
        return buffer_+(length_>maxCharacters?length_-maxCharacters:0);
    }
    Cell cell(size_t index) const {
        if(!valid_ || index>=kCells)return Cell{Kind::Empty,0,false};
        if(index<kCharactersPerPage) {
            const size_t at=page_*kCharactersPerPage+index;
            return at<characters_?Cell{Kind::Character,alphabet_[at],true}:Cell{Kind::Empty,0,false};
        }
        if(index==28)return Cell{Kind::Space,' ',space_};
        if(index==29)return Cell{Kind::Delete,0,length_>0};
        if(index==30)return Cell{Kind::Next,0,pages_>1};
        return Cell{Kind::Done,0,true};
    }
    // UP/DOWN use-1/+1. Empty character slots are skipped; disabled actions
    // remain selectable so the UI can explain their unavailable operation.
    void move(int delta) {
        if(!editing() || !delta)return;
        int steps=delta%int(kCells);const int direction=steps<0?-1:1;
        if(steps<0)steps=-steps;
        while(steps--)for(size_t visited=0;visited<kCells;++visited) {
            int next=int(selected_)+direction;
            if(next<0)next+=int(kCells);
            if(next>=int(kCells))next-=int(kCells);
            selected_=size_t(next);
            if(cell(selected_).kind!=Kind::Empty)break;
        }
    }
    Result changePage(int delta) {
        if(!editing())return Result::None;
        if(pages_<2 || delta%int(pages_)==0)return Result::Unavailable;
        int next=int(page_)+delta%int(pages_);
        if(next<0)next+=int(pages_);
        if(next>=int(pages_))next-=int(pages_);
        page_=size_t(next);
        if(cell(selected_).kind==Kind::Empty)selected_=0;
        return Result::PageChanged;
    }
    Result activate() {
        if(!editing())return Result::None;
        const Cell selected=cell(selected_);
        if(!selected.enabled)return Result::Unavailable;
        switch(selected.kind) {
        case Kind::Character:return insert(selected.character);
        case Kind::Space:return insert(' ');
        case Kind::Delete:return erase();
        case Kind::Next:return changePage(1);
        case Kind::Done:return accept();
        case Kind::Empty:return Result::Unavailable;
        }
        return Result::Unavailable;
    }
    Result erase() {
        if(!editing())return Result::None;
        if(!length_)return Result::Unavailable;
        buffer_[--length_]=0;return Result::Changed;
    }
    Result accept() {
        if(!editing())return Result::None;
        outcome_=Outcome::Accepted;return Result::Accepted;
    }
    Result cancel() {
        if(!editing())return Result::None;
        outcome_=Outcome::Cancelled;return Result::Cancelled;
    }
private:
    bool editing() const {return valid_ && outcome_==Outcome::Editing;}
    Result insert(char character) {
        if(length_>=capacity_-1)return Result::Full;
        buffer_[length_++]=character;buffer_[length_]=0;return Result::Changed;
    }
    char* buffer_;
    size_t capacity_,length_,characters_,page_,pages_,selected_;
    char alphabet_[94];
    bool space_,valid_;
    Outcome outcome_;
};
}
