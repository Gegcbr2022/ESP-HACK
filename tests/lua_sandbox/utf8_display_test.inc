#ifndef ESP_HACK_UTF8_DISPLAY_TEST_INC
#define ESP_HACK_UTF8_DISPLAY_TEST_INC
static bool utf8_display_tests() {
    size_t glyphs=999;
    CHECK(ui_text::validate(nullptr,0,&glyphs)&&glyphs==0);
    CHECK(!ui_text::validate(nullptr,1,&glyphs));
    const char alphabet[]="АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя";
    CHECK(ui_text::validate(alphabet,sizeof alphabet-1,&glyphs)); CHECK(glyphs==66);
    for(size_t n=0;n<=66;++n) {
        CHECK(ui_text::prefixBytes(alphabet,n)==n*2);
        CHECK(ui_text::validate(alphabet,n*2,&glyphs)&&glyphs==n);
        if(n<66) CHECK(!ui_text::validate(alphabet,n*2+1));
    }
    CHECK(ui_text::validate("OK: Ёж, съешь ещё!",strlen("OK: Ёж, съешь ещё!"),&glyphs));
    CHECK(glyphs==18); CHECK(ui_text::prefixBytes("OK Ёж",4)==5);
    const char* invalid[]={"\xD0","\xD1","\x80","\xD0\x20","\xC0\xAF","\xC1\x81",
        "\xD0\x84","\xD1\x96","\xC3\xA9","\xE2\x80\x94","\xF0\x9F\x98\x80","\n","\t","\x7F"};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;++i) {
        CHECK(!ui_text::validate(invalid[i],strlen(invalid[i])));
        const std::string prefixed=std::string("A")+invalid[i];
        CHECK(ui_text::prefixBytes(prefixed.c_str(),100)==1);
    }
    const char nul[]={'A',0,'B'}; CHECK(!ui_text::validate(nul,sizeof nul));
    std::string bytes160; for(unsigned i=0;i<80;++i) bytes160+="Я";
    CHECK(bytes160.size()==160); CHECK(ui_text::validate(bytes160.data(),bytes160.size(),&glyphs)&&glyphs==80);

    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    std::string row; for(unsigned i=0;i<21;++i) row+="Я";
    CHECK(load(vm,("ui.text(0,56,'"+row+"')").c_str())); CHECK(mock.lastText==row);
    row+="Я"; CHECK(!load(vm,("ui.text(0,0,'"+row+"')").c_str()));
    CHECK(strstr(vm.error(),"outside visible"));
    CHECK(load(vm,"ui.text(0,0,'Ёжик',2)"));
    CHECK(!load(vm,"ui.text(0,0,'Одиннадцать',2)")); CHECK(strstr(vm.error(),"outside visible"));
    // The API limit counts bytes, independently of the viewport's glyph width.
    CHECK(!load(vm,("ui.text(0,0,'"+bytes160+"')").c_str())); CHECK(strstr(vm.error(),"outside visible"));
    bytes160+='A'; CHECK(!load(vm,("ui.text(0,0,'"+bytes160+"')").c_str()));
    CHECK(strstr(vm.error(),"160 bytes"));
    const char* escaped[]={"\\xD0","\\xD1","\\x80","\\xD0\\x20","\\xC0\\xAF","\\xD0\\x84",
        "\\xD1\\x96","\\xC3\\xA9","\\xE2\\x80\\x94","\\xF0\\x9F\\x98\\x80","\\x00","\\x0A"};
    for(size_t i=0;i<sizeof escaped/sizeof *escaped;++i) {
        mock.drawCalls=0;
        CHECK(!load(vm,(std::string("ui.text(0,0,'")+escaped[i]+"')").c_str()));
        CHECK(strstr(vm.error(),"UTF-8"));
    }
    CHECK(vm.peak()<=sizeof bank); vm.close(); CHECK(vm.used()==0);
    return true;
}
#endif
