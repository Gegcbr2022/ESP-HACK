#include "app_install.h"
#include "app_filename.h"
#include <Arduino.h>
#include <SD.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace {
void digestHex(const uint8_t digest[32],char text[65]) {
    for(size_t i=0;i<32;++i)snprintf(text+i*2,3,"%02x",digest[i]);
}

// Verify bytes returned by the SD card after close, before replacing a working app.
bool verifyStoredApp(const char* path,size_t length,const char* expected) {
    File file=SD.open(path,FILE_READ);
    if(!file)return false;
    if(file.isDirectory()||file.size()!=length){file.close();return false;}
    mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
    bool valid=mbedtls_sha256_starts_ret(&hash,0)==0;
    uint8_t block[256],digest[32];size_t checked=0;const uint32_t started=millis();
    while(valid&&checked<length&&millis()-started<5000) {
        size_t n=length-checked;if(n>sizeof(block))n=sizeof(block);
        int read=file.read(block,n);
        valid=read==static_cast<int>(n)&&mbedtls_sha256_update_ret(&hash,block,n)==0;
        if(valid)checked+=n;
        delay(1);
    }
    valid=valid&&checked==length&&file.size()==length&&mbedtls_sha256_finish_ret(&hash,digest)==0;
    mbedtls_sha256_free(&hash);file.close();
    if(!valid)return false;
    char actual[65];digestHex(digest,actual);return !strcmp(actual,expected);
}
}

bool handleAppInstallCommand(const char* command) {
    if(strncmp(command,"APP_PUT ",8))return false;
    char name[41]={},expected[65]={},extra[2]={};unsigned length=0;
    if(sscanf(command+8,"%40s %u %64s %1s",name,&length,expected,extra)!=3||
       !validAppFilename(name)||!length||length>32768||strlen(expected)!=64) {
        Serial.println("APP_ERROR invalid request");return true;
    }
    for(size_t i=0;i<64;++i)if(!((expected[i]>='0'&&expected[i]<='9')||(expected[i]>='a'&&expected[i]<='f'))) {
        Serial.println("APP_ERROR invalid digest");return true;
    }
    if(!SD.exists("/apps")&&!SD.mkdir("/apps")){Serial.println("APP_ERROR SD unavailable");return true;}
    char path[64],temporary[72],previous[72];
    snprintf(path,sizeof(path),"/apps/%s",name);snprintf(temporary,sizeof(temporary),"%s.upload",path);snprintf(previous,sizeof(previous),"%s.bak",path);
    // Recover an interrupted promotion before starting a new upload.
    if(!SD.exists(path)&&SD.exists(previous)&&!SD.rename(previous,path)) {
        Serial.println("APP_ERROR previous recovery failed");return true;
    }
    if(SD.exists(path)) {
        File existing=SD.open(path,FILE_READ);
        const bool regular=existing&&!existing.isDirectory();existing.close();
        if(!regular){Serial.println("APP_ERROR invalid destination");return true;}
    }
    if(SD.exists(temporary)&&!SD.remove(temporary)){Serial.println("APP_ERROR stale upload cleanup");return true;}
    File file=SD.open(temporary,FILE_WRITE);
    if(!file){Serial.println("APP_ERROR SD unavailable");return true;}
    mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
    bool valid=mbedtls_sha256_starts_ret(&hash,0)==0;
    if(!valid) {
        mbedtls_sha256_free(&hash);file.close();SD.remove(temporary);
        Serial.println("APP_ERROR digest initialization");return true;
    }
    Serial.printf("APP_READY %u\n",length);
    uint8_t block[256];size_t received=0;const uint32_t started=millis();
    while(valid&&received<length&&millis()-started<15000) {
        size_t available=Serial.available();
        if(available) {
            size_t n=available>sizeof(block)?sizeof(block):available;
            if(n>length-received)n=length-received;
            n=Serial.readBytes(block,n);
            valid=n&&file.write(block,n)==n&&mbedtls_sha256_update_ret(&hash,block,n)==0;
            received+=n;
        }
        delay(1);
    }
    uint8_t digest[32];char actual[65];
    valid=valid&&received==length&&mbedtls_sha256_finish_ret(&hash,digest)==0;
    mbedtls_sha256_free(&hash);file.flush();file.close();
    if(valid){digestHex(digest,actual);valid=!strcmp(actual,expected);}
    if(!valid){SD.remove(temporary);Serial.println("APP_ERROR transfer timeout/digest/write");return true;}
    if(!verifyStoredApp(temporary,length,expected)) {
        SD.remove(temporary);Serial.println("APP_ERROR SD readback failed");return true;
    }
    bool hadPrevious=SD.exists(path);
    if(hadPrevious&&SD.exists(previous)&&!SD.remove(previous)) {
        SD.remove(temporary);Serial.println("APP_ERROR stale backup cleanup");return true;
    }
    if(hadPrevious&&!SD.rename(path,previous)){SD.remove(temporary);Serial.println("APP_ERROR preserve previous");return true;}
    if(!SD.rename(temporary,path)) {
        const bool recovered=!hadPrevious||SD.rename(previous,path);
        SD.remove(temporary);
        Serial.println(recovered?"APP_ERROR promote failed":"APP_ERROR promote failed; previous at .bak");return true;
    }
    if(hadPrevious)SD.remove(previous);
    Serial.printf("APP_INSTALLED %s %u %s\n",name,length,actual);return true;
}
