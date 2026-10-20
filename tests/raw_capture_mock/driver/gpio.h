#pragma once
#define GPIO_IS_VALID_GPIO(pin) ((pin) < 40 && (pin) != 20 && (pin) != 24 && \
                                (pin) != 28 && (pin) != 29 && (pin) != 30 && (pin) != 31)
