/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdint.h>
#include <stdbool.h>
#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_gpio.h"
#include "../io.h"
#include "epd_w21.h"
#include "esp32c6/rom/rtc.h"
#include "hal/misc.h"
#include "hal/lp_timer_hal.h"
#include "soc/lp_clkrst_struct.h"
#include "hal/clk_tree_ll.h"
#include "hal/lp_timer_ll.h"
#include "soc/rtc.h"

#define DEBOUNCE_SAMPLES 5
#define WAKEUP_PIN LP_IO_NUM_0

static int digit;

uint64_t wake_time;

int main (void) {
	if (digit==0) {
		EPD_Init();
	} else {
		EPD_Init_Part();
	}
	int new_digit=digit+1;
	if (new_digit==10) new_digit=0;
	EPD_Digit(digit, new_digit);
	digit=new_digit;
	EPD_DeepSleep();

	int64_t sleep_duration_us=10*1000*1000;
	wake_time += sleep_duration_us * (1 << RTC_CLK_CAL_FRACT) / clk_ll_rtc_slow_load_cal();

    lp_timer_ll_clear_lp_alarm_intr_status(&LP_TIMER);
    lp_timer_ll_set_alarm_target(&LP_TIMER, 1, wake_time);
    lp_timer_ll_set_target_enable(&LP_TIMER, 1, true);

	ulp_lp_core_halt();
}
