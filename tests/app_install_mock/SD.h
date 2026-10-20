#pragma once
#include "mock_state.h"
#include <algorithm>
#include <cstring>
#define FILE_READ "r"
#define FILE_WRITE "w"
class File {
    std::string path;size_t position=0;bool valid=false,writing=false;
public:
    File()=default;
    File(const char* name,bool write):path(name),valid(true),writing(write){}
    explicit operator bool()const{return valid;}
    bool isDirectory()const{return valid&&mock.files.at(path).directory;}
    size_t size()const{return valid?mock.files.at(path).bytes.size():0;}
    size_t write(const uint8_t* bytes,size_t count) {
        if(!valid||!writing)return 0;
        if(mock.shortWrite&&count)--count;
        auto& data=mock.files[path].bytes;data.insert(data.end(),bytes,bytes+count);return count;
    }
    int read(uint8_t* bytes,size_t count) {
        if(!valid||writing)return -1;
        const auto& data=mock.files.at(path).bytes;count=std::min(count,data.size()-position);
        if(mock.shortRead&&count)--count;
        memcpy(bytes,data.data()+position,count);position+=count;mock.readBytes+=count;mock.now+=mock.readDelay;return int(count);
    }
    void flush() {
        if(!valid||!writing)return;
        auto& data=mock.files[path].bytes;
        if(mock.corruptFlush&&!data.empty())data[0]^=0x80;
        if(mock.truncateFlush&&!data.empty())data.pop_back();
    }
    void close(){valid=false;}
};
struct MockSD {
    bool exists(const char* path){return mock.files.count(path)!=0;}
    bool mkdir(const char* path){mock.files[path].directory=true;return true;}
    bool remove(const char* path) {
        if(mock.failRemove.count(path))return false;
        return mock.files.erase(path)!=0;
    }
    bool rename(const char* from,const char* to) {
        if(mock.failRenameFrom.count(from)||!exists(from)||exists(to))return false;
        mock.files[to]=mock.files[from];mock.files.erase(from);return true;
    }
    File open(const char* path,const char* mode=FILE_READ) {
        const bool write=!strcmp(mode,FILE_WRITE);
        if(!exists(path)&&!write)return File();
        if(write)mock.files[path]=MockEntry();
        return File(path,write);
    }
};
extern MockSD SD;
