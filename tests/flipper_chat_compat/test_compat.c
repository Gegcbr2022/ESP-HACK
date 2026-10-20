#include "../../shared/flipper_chat_compat.h"
#include "../../shared/chat_radio.h"
#include <stdio.h>

static unsigned checks;
#define CHECK(value) do { ++checks; if(!(value)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#value); return false; } } while(0)

static uint8_t reg_value(const uint8_t* pairs,size_t length,uint8_t address) {
    for(size_t i=0;i<length;i+=2) if(pairs[i]==address) return pairs[i+1];
    return 0xFF;
}
static uint64_t rate_hundredths(const uint8_t* pairs,size_t length) {
    const uint8_t e=reg_value(pairs,length,0x10)&15;
    const uint8_t m=reg_value(pairs,length,0x11);
    return (((uint64_t)(256u+m)<<e)*26000000u*100u+134217728u)/268435456u;
}
static bool profile_vectors(void) {
    CHECK(FLIPPER_CHAT_REGISTER_BYTES%2==0);
    CHECK(rate_hundredths(flipper_chat_registers,FLIPPER_CHAT_REGISTER_BYTES)==999260);
    CHECK(rate_hundredths(chat_radio_registers,CHAT_RADIO_REGISTER_BYTES)==3838348);
    CHECK(reg_value(flipper_chat_registers,FLIPPER_CHAT_REGISTER_BYTES,0x04)==0x46);
    CHECK(reg_value(flipper_chat_registers,FLIPPER_CHAT_REGISTER_BYTES,0x05)==0x4C);
    CHECK(reg_value(flipper_chat_registers,FLIPPER_CHAT_REGISTER_BYTES,0x06)==60);
    CHECK(reg_value(flipper_chat_registers,FLIPPER_CHAT_REGISTER_BYTES,0x07)==0x0C);
    CHECK(reg_value(flipper_chat_registers,FLIPPER_CHAT_REGISTER_BYTES,0x08)==0x05);
    CHECK(reg_value(flipper_chat_registers,FLIPPER_CHAT_REGISTER_BYTES,0x12)==0x12);
    CHECK(chat_radio_frequencies[0]==433920000u&&chat_radio_frequencies[1]==868350000u);
    return true;
}
static bool feed(FlipperChatRx* rx,const uint8_t* bytes,size_t length,uint32_t now,FlipperChatMessage* message) {
    bool ready=false;
    for(size_t i=0;i<length;++i) if(flipper_chat_rx_byte(rx,bytes[i],now,message)) ready=true;
    return ready;
}
static bool rx_vectors(void) {
    FlipperChatRx rx; FlipperChatMessage message;
    const char* golden="\x1b[0;33mFlipper\x1b[0m: hello\r\n";
    for(size_t split=1;split<strlen(golden);++split) {
        flipper_chat_rx_init(&rx);
        CHECK(!flipper_chat_rx_poll(&rx,0,&message));
        CHECK(!feed(&rx,(const uint8_t*)golden,split,0,&message));
        CHECK(!flipper_chat_rx_poll(&rx,20,&message));
        CHECK(feed(&rx,(const uint8_t*)golden+split,strlen(golden)-split,20,&message));
        CHECK(!strcmp(message.text,"Flipper: hello")); CHECK(!message.truncated);
    }
    // Put the color sequence across the exact 60-byte RF packet boundary.
    char packetized[120]; memset(packetized,'A',57);
    strcpy(packetized+57,"\x1b[0;33mFox\x1b[0m: test\r\n");
    flipper_chat_rx_init(&rx);
    CHECK(!feed(&rx,(const uint8_t*)packetized,60,100,&message));
    CHECK(!flipper_chat_rx_poll(&rx,120,&message));
    CHECK(feed(&rx,(const uint8_t*)packetized+60,strlen(packetized)-60,120,&message));
    CHECK(strlen(message.text)==66); CHECK(!strcmp(message.text+57,"Fox: test"));
    flipper_chat_rx_init(&rx);
    CHECK(feed(&rx,(const uint8_t*)"one\r\n",5,0,&message)); CHECK(!strcmp(message.text,"one"));
    CHECK(feed(&rx,(const uint8_t*)"two\r\n",5,10,&message)); CHECK(!strcmp(message.text,"two"));
    { const uint8_t padded[]={'A',0,'\t','B','\r',0,'\n'};
      CHECK(feed(&rx,padded,sizeof padded,20,&message)); CHECK(!strcmp(message.text,"A B")); }
    CHECK(!feed(&rx,(const uint8_t*)"quiet",5,100,&message));
    CHECK(!flipper_chat_rx_poll(&rx,599,&message));
    CHECK(flipper_chat_rx_poll(&rx,600,&message)); CHECK(!strcmp(message.text,"quiet"));
    CHECK(!flipper_chat_rx_poll(&rx,1100,&message));
    CHECK(!feed(&rx,(const uint8_t*)"wrap",4,UINT32_MAX-200,&message));
    CHECK(!flipper_chat_rx_poll(&rx,298,&message));
    CHECK(flipper_chat_rx_poll(&rx,299,&message)); CHECK(!strcmp(message.text,"wrap"));
    return true;
}
static bool rx_rejection(void) {
    FlipperChatRx rx; FlipperChatMessage message;
    for(unsigned value=1;value<256;++value) {
        if((value>=32&&value<=126)||value==27||value=='\r'||value=='\n'||value=='\t') continue;
        const uint8_t bad[]={'A',(uint8_t)value,'B','\n'};
        flipper_chat_rx_init(&rx); CHECK(!feed(&rx,bad,sizeof bad,0,&message));
        CHECK(rx.rejected==1); CHECK(!rx.active);
        CHECK(feed(&rx,(const uint8_t*)"ok\n",3,1,&message)); CHECK(!strcmp(message.text,"ok"));
    }
    { const char* bad[]={"A\x1b]0;title\n","A\x1b[", "A\x1b[000000000000000000000000000000000m\n"};
      for(unsigned i=0;i<sizeof bad/sizeof *bad;++i) {
          flipper_chat_rx_init(&rx); CHECK(!feed(&rx,(const uint8_t*)bad[i],strlen(bad[i]),0,&message));
          CHECK(!flipper_chat_rx_poll(&rx,500,&message)); CHECK(rx.rejected==1);
      } }
    flipper_chat_rx_init(&rx);
    for(uint32_t now=0;now<=4000;now+=100) {
        CHECK(!flipper_chat_rx_poll(&rx,now,&message));
        CHECK(!flipper_chat_rx_byte(&rx,'A',now,&message));
    }
    CHECK(rx.invalid); CHECK(!flipper_chat_rx_byte(&rx,'\n',4100,&message)); CHECK(rx.rejected==1);
    CHECK(feed(&rx,(const uint8_t*)"recovered\n",10,4101,&message)); CHECK(!strcmp(message.text,"recovered"));
    for(size_t length=159;length<=1000;length+=(length<161?1:839)) {
        flipper_chat_rx_init(&rx);
        for(size_t i=0;i<length;++i) CHECK(!flipper_chat_rx_byte(&rx,'X',0,&message));
        CHECK(flipper_chat_rx_byte(&rx,'\n',1,&message));
        CHECK(strlen(message.text)==(length<160?length:160));
        CHECK(message.truncated==(length>160)); CHECK(rx.truncations==(length>160?1u:0u));
        if(length>160) CHECK(!strcmp(message.text+148," [truncated]"));
    }
    return true;
}
static bool tx_vectors(void) {
    FlipperChatTx tx; uint8_t packet[60],wire[174];
    flipper_chat_tx_init(&tx);
    CHECK(!flipper_chat_tx_queue(&tx,NULL,1,0)); CHECK(!flipper_chat_tx_queue(&tx,"",0,0));
    CHECK(!flipper_chat_tx_queue(&tx,"line\n",5,0)); CHECK(!flipper_chat_tx_queue(&tx,"\xFF",1,0));
    char text[161]; memset(text,'X',160); text[160]=0;
    CHECK(!flipper_chat_tx_queue(&tx,text,161,0));
    for(size_t length=1;length<=160;++length) {
        flipper_chat_tx_init(&tx); CHECK(flipper_chat_tx_queue(&tx,text,length,100));
        CHECK(!flipper_chat_tx_queue(&tx,"busy",4,100));
        CHECK(!flipper_chat_tx_poll(&tx,100,true,packet,sizeof packet));
        CHECK(!flipper_chat_tx_poll(&tx,100,false,NULL,sizeof packet));
        CHECK(!flipper_chat_tx_poll(&tx,100,false,packet,0));
        size_t total=0,chunks=0;
        while(tx.delivery!=FlipperChatSent) {
            const size_t size=flipper_chat_tx_poll(&tx,120,false,packet,sizeof packet);
            CHECK(size>0&&size<=60); CHECK(size==((length+12-total)>60?60:(length+12-total)));
            memcpy(wire+total,packet,size); total+=size; ++chunks;
            CHECK(!flipper_chat_tx_poll(&tx,120,false,packet,sizeof packet));
            flipper_chat_tx_result(&tx,true);
        }
        CHECK(chunks==(length+12+59)/60); CHECK(total==length+12);
        CHECK(!memcmp(wire,"ESP-HACK: ",10)); CHECK(!memcmp(wire+10,text,length));
        CHECK(wire[total-2]=='\r'&&wire[total-1]=='\n');
        CHECK(!flipper_chat_tx_poll(&tx,121,false,packet,sizeof packet)); // Sent locally, no ACK wait.
    }
    flipper_chat_tx_init(&tx); CHECK(flipper_chat_tx_queue(&tx,"lost",4,0));
    CHECK(flipper_chat_tx_poll(&tx,1,false,packet,sizeof packet)==16);
    flipper_chat_tx_result(&tx,false); CHECK(tx.delivery==FlipperChatFailed);
    for(unsigned i=0;i<10;++i) CHECK(!flipper_chat_tx_poll(&tx,i*100,false,packet,sizeof packet));
    flipper_chat_tx_result(&tx,true); CHECK(tx.delivery==FlipperChatFailed);
    CHECK(flipper_chat_tx_queue(&tx,"again",5,100));
    CHECK(!flipper_chat_tx_poll(&tx,10099,true,packet,sizeof packet));
    CHECK(tx.delivery==FlipperChatQueued);
    CHECK(!flipper_chat_tx_poll(&tx,10100,true,packet,sizeof packet)); CHECK(tx.delivery==FlipperChatFailed);
    CHECK(flipper_chat_tx_queue(&tx,"wrap",4,UINT32_MAX-5000));
    CHECK(!flipper_chat_tx_poll(&tx,4998,true,packet,sizeof packet)); CHECK(tx.delivery==FlipperChatQueued);
    CHECK(!flipper_chat_tx_poll(&tx,4999,true,packet,sizeof packet)); CHECK(tx.delivery==FlipperChatFailed);
    CHECK(flipper_chat_tx_queue(&tx,"late result",11,0));
    CHECK(flipper_chat_tx_poll(&tx,9999,false,packet,sizeof packet)==23);
    CHECK(!flipper_chat_tx_poll(&tx,10000,false,packet,sizeof packet)); CHECK(tx.delivery==FlipperChatFailed);
    flipper_chat_tx_result(&tx,true); CHECK(tx.delivery==FlipperChatFailed); CHECK(tx.pending==0);
    return true;
}
static bool mutation_bounds(void) {
    struct Guarded { uint8_t before[16]; FlipperChatRx rx; uint8_t after[16]; } guarded;
    FlipperChatMessage message; uint32_t seed=0x12345678,now=UINT32_MAX-10000;
    memset(&guarded,0xA5,sizeof guarded); flipper_chat_rx_init(&guarded.rx);
    for(unsigned i=0;i<100000;++i) {
        seed=seed*1664525u+1013904223u; now+=(seed>>28);
        (void)flipper_chat_rx_poll(&guarded.rx,now,&message);
        if(flipper_chat_rx_byte(&guarded.rx,(uint8_t)(seed>>16),now,&message)) {
            CHECK(strlen(message.text)<=160);
            for(size_t j=0;message.text[j];++j) CHECK((uint8_t)message.text[j]>=32&&(uint8_t)message.text[j]<=126);
        }
        CHECK(guarded.rx.length<=160); CHECK(guarded.rx.escape<=2);
    }
    for(unsigned i=0;i<16;++i) { CHECK(guarded.before[i]==0xA5); CHECK(guarded.after[i]==0xA5); }
    return true;
}
int main(void) {
    if(!profile_vectors()||!rx_vectors()||!rx_rejection()||!tx_vectors()||!mutation_bounds()) return 1;
    printf("Flipper plaintext compatibility: %u checks PASS, 100000 malformed-stream events\n",checks);
    return 0;
}
