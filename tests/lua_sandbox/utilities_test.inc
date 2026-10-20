#ifndef ESP_HACK_UTILITIES_TEST_INC
#define ESP_HACK_UTILITIES_TEST_INC
// Include after the actual sandbox harness's MockSdk/read_file/draw_frame helpers.
static bool utility_press(LuaSandbox& vm,const char* button,unsigned times=1) {
    for(unsigned i=0;i<times;++i) {
        CHECK(vm.input(button,"press")); CHECK(vm.input(button,"release"));
        CHECK(vm.instructions()<=LuaSandbox::kInstructions);
    }
    return true;
}
static bool utility_text(LuaSandbox& vm,MockSdk& mock,const char* expected) {
    CHECK(draw_frame(vm,mock)); CHECK(has_text(mock,expected));
    return true;
}
static bool utility_reset(LuaSandbox& vm,const std::string& source,const char* path) {
    vm.close(); CHECK(vm.used()==0); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path));
    return true;
}
static bool countdown_utility_test(const char* path) {
    std::string source; CHECK(read_file(path,source));
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path)); CHECK(utility_text(vm,mock,"05:00"));
    CHECK(utility_press(vm,"down",5)); CHECK(utility_press(vm,"ok"));
    CHECK(utility_press(vm,"up")); CHECK(utility_press(vm,"ok")); CHECK(utility_text(vm,mock,"ИДЁТ"));
    mock.millis=400; CHECK(vm.tick(33)); CHECK(utility_press(vm,"ok")); CHECK(utility_text(vm,mock,"ПАУЗА"));
    mock.millis=900; CHECK(vm.tick(33)); CHECK(utility_text(vm,mock,"00:01"));
    CHECK(utility_press(vm,"ok")); mock.millis=1499; CHECK(vm.tick(33)); CHECK(utility_text(vm,mock,"00:01"));
    mock.millis=1500; CHECK(vm.tick(33)); CHECK(utility_text(vm,mock,"ВРЕМЯ ВЫШЛО!"));
    CHECK(utility_text(vm,mock,"00:00")); CHECK(utility_press(vm,"ok")); CHECK(utility_text(vm,mock,"ИДЁТ"));
    mock.millis=2500; CHECK(vm.tick(33)); CHECK(utility_text(vm,mock,"ВРЕМЯ ВЫШЛО!"));
    CHECK(utility_press(vm,"back")); CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down"));
    CHECK(utility_press(vm,"ok")); CHECK(utility_text(vm,mock,"Задайте время > 0"));
    CHECK(utility_press(vm,"back")); CHECK(utility_press(vm,"up",200));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"up",200)); CHECK(utility_text(vm,mock,"99:59"));
    CHECK(utility_press(vm,"back")); CHECK(utility_press(vm,"down",200));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down",200)); CHECK(utility_text(vm,mock,"00:00"));
    mock.millis=UINT32_MAX-100; CHECK(utility_reset(vm,source,path));
    CHECK(utility_press(vm,"down",5)); CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"up"));
    CHECK(utility_press(vm,"ok")); mock.millis+=999; CHECK(vm.tick(33)); CHECK(utility_text(vm,mock,"00:01"));
    ++mock.millis; CHECK(vm.tick(33)); CHECK(utility_text(vm,mock,"ВРЕМЯ ВЫШЛО!"));
    CHECK(!mock.quit&&mock.starts==0&&mock.stops==0); CHECK(vm.peak()<=sizeof bank);
    printf("Lua utility countdown: peak %u/%u bytes\n",unsigned(vm.peak()),unsigned(sizeof bank));
    vm.close(); CHECK(vm.used()==0); return true;
}
static bool calculator_utility_test(const char* path) {
    std::string source; CHECK(read_file(path,source));
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path));
    CHECK(utility_press(vm,"up",12)); CHECK(utility_press(vm,"ok",2)); CHECK(utility_press(vm,"up",3));
    CHECK(utility_text(vm,mock,"= 15.00")); CHECK(utility_press(vm,"ok",2));
    CHECK(utility_press(vm,"up")); CHECK(utility_text(vm,mock,"= 9.00"));
    CHECK(utility_press(vm,"up")); CHECK(utility_text(vm,mock,"= 36.00"));
    CHECK(utility_press(vm,"up")); CHECK(utility_text(vm,mock,"= 4.00"));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down",3)); CHECK(utility_text(vm,mock,"ДЕЛЕНИЕ НА НОЛЬ"));
    CHECK(utility_reset(vm,source,path)); CHECK(utility_press(vm,"back",6));
    CHECK(utility_press(vm,"up")); CHECK(utility_press(vm,"ok",2)); CHECK(utility_press(vm,"up",2));
    CHECK(utility_text(vm,mock,"= 0.03"));
    CHECK(utility_reset(vm,source,path)); CHECK(utility_press(vm,"back",5)); CHECK(utility_press(vm,"up",10));
    CHECK(utility_text(vm,mock,">A 999999.99")); CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"up",2));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"up",10)); CHECK(utility_text(vm,mock,"= 999999980000.00"));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down",20)); CHECK(utility_text(vm,mock,">A -999999.99"));
    CHECK(utility_text(vm,mock,"= -999999980000.00"));
    CHECK(!mock.quit&&mock.starts==0&&mock.stops==0); CHECK(vm.peak()<=sizeof bank);
    printf("Lua utility calculator: peak %u/%u bytes\n",unsigned(vm.peak()),unsigned(sizeof bank));
    vm.close(); CHECK(vm.used()==0); return true;
}
static bool converter_utility_test(const char* path) {
    std::string source; CHECK(read_file(path,source));
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path)); CHECK(utility_press(vm,"up"));
    const char* expected[]={"Вых 3.28 фут","Вых 0.30 м","Вых 0.62 миля/ч","Вых 1.61 км/ч",
        "Вых 33.80 F","Вых -17.22 C","Вых 2.20 фунт","Вых 0.45 кг",
        "Вых 0.26 галСША","Вых 3.79 л","Вых 0.00 psi","Вых 6894.76 Па"};
    for(unsigned i=0;i<12;++i) { CHECK(utility_text(vm,mock,expected[i])); CHECK(utility_press(vm,"ok")); }
    CHECK(utility_text(vm,mock,"м -> фут (1/12)"));
    CHECK(utility_press(vm,"ok",4)); CHECK(utility_press(vm,"down")); CHECK(utility_text(vm,mock,"Вых 32.00 F"));
    CHECK(utility_press(vm,"up",100)); CHECK(utility_text(vm,mock,"Вых 212.00 F"));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down",68)); CHECK(utility_text(vm,mock,"Вых 0.00 C"));
    CHECK(utility_press(vm,"back",5)); CHECK(utility_press(vm,"down")); CHECK(utility_text(vm,mock,"Ниже абсолютного нуля"));
    CHECK(utility_press(vm,"ok",6)); CHECK(utility_press(vm,"up",20));
    CHECK(utility_text(vm,mock,"Вх  999999.99 psi")); CHECK(utility_text(vm,mock,"Вых 6894757224.22 Па"));
    CHECK(utility_press(vm,"down",20)); CHECK(utility_text(vm,mock,"Вх  -999999.99 psi"));
    CHECK(utility_text(vm,mock,"Вых -6894757224.22 Па"));
    CHECK(!mock.quit&&mock.starts==0&&mock.stops==0); CHECK(vm.peak()<=sizeof bank);
    printf("Lua utility converter: peak %u/%u bytes\n",unsigned(vm.peak()),unsigned(sizeof bank));
    vm.close(); CHECK(vm.used()==0); return true;
}
static bool resistor_utility_test(const char* path) {
    std::string source; CHECK(read_file(path,source));
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path)); CHECK(utility_text(vm,mock,"1.00 кОм +/-5%"));
    CHECK(utility_press(vm,"back")); CHECK(utility_text(vm,mock,"10.00 кОм +/-5%"));
    CHECK(utility_press(vm,"up")); CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down"));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"up")); CHECK(utility_text(vm,mock,"29.10 кОм +/-5%"));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down",3)); CHECK(utility_text(vm,mock,"29.10 Ом +/-5%"));
    CHECK(utility_text(vm,mock,"Полоса 4: ЗОЛОТОЙ")); CHECK(utility_press(vm,"down"));
    CHECK(utility_text(vm,mock,"2.91 Ом +/-5%")); CHECK(utility_text(vm,mock,"Полоса 4: СЕРЕБРЯНЫЙ"));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"up")); CHECK(utility_text(vm,mock,"2.91 Ом +/-10%"));
    CHECK(utility_press(vm,"up")); CHECK(utility_text(vm,mock,"2.91 Ом +/-1%"));
    CHECK(utility_reset(vm,source,path)); CHECK(utility_press(vm,"ok",2)); CHECK(utility_press(vm,"down",4));
    CHECK(utility_text(vm,mock,"0.10 Ом +/-5%"));
    CHECK(utility_reset(vm,source,path)); CHECK(utility_press(vm,"back"));
    CHECK(utility_press(vm,"down")); CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down"));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down")); CHECK(utility_press(vm,"ok"));
    CHECK(utility_press(vm,"up",7)); CHECK(utility_text(vm,mock,"999.00 ГОм +/-5%"));
    CHECK(utility_press(vm,"ok")); CHECK(utility_press(vm,"down")); CHECK(utility_text(vm,mock,"999.00 ГОм +/-0.05%"));
    CHECK(!mock.quit&&mock.starts==0&&mock.stops==0); CHECK(vm.peak()<=sizeof bank);
    printf("Lua utility resistor: peak %u/%u bytes\n",unsigned(vm.peak()),unsigned(sizeof bank));
    vm.close(); CHECK(vm.used()==0); return true;
}
static bool utility_tests(const char* countdownPath,const char* calculatorPath,const char* converterPath,const char* resistorPath) {
    return countdown_utility_test(countdownPath)&&calculator_utility_test(calculatorPath)&&
        converter_utility_test(converterPath)&&resistor_utility_test(resistorPath);
}
#endif
