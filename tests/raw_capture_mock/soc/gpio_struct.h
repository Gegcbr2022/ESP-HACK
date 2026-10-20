#pragma once
#include <stdint.h>
struct MockGpio {
    uint32_t in;
    struct { uint32_t val; } in1;
};
extern MockGpio GPIO;
