#pragma once
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=0x103,ESP_INTR_FLAG_IRAM=1,
       PCNT_PIN_NOT_USED=-1,PCNT_UNIT_7=7,PCNT_CHANNEL_0=0,PCNT_CHANNEL_1=1,
       PCNT_COUNT_DIS=0,PCNT_COUNT_INC=1,PCNT_MODE_KEEP=0,PCNT_EVT_H_LIM=32 };
typedef int pcnt_unit_t;
struct pcnt_config_t {
    int pulse_gpio_num,ctrl_gpio_num,lctrl_mode,hctrl_mode,pos_mode,neg_mode;
    int16_t counter_h_lim,counter_l_lim;
    int unit,channel;
};
esp_err_t pcnt_unit_config(const pcnt_config_t*);
esp_err_t pcnt_counter_pause(int);
esp_err_t pcnt_counter_resume(int);
esp_err_t pcnt_counter_clear(int);
esp_err_t pcnt_get_counter_value(int,int16_t*);
esp_err_t pcnt_intr_disable(int);
esp_err_t pcnt_intr_enable(int);
esp_err_t pcnt_filter_disable(int);
esp_err_t pcnt_event_disable(int,int);
esp_err_t pcnt_event_enable(int,int);
esp_err_t pcnt_set_mode(int,int,int,int,int,int);
esp_err_t pcnt_isr_handler_add(int,void(*)(void*),void*);
esp_err_t pcnt_isr_handler_remove(int);
esp_err_t pcnt_isr_service_install(int);
void pcnt_isr_service_uninstall();
