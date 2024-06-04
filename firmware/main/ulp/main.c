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

//This is the hours, mins that matches the *coming* wakeup
int32_t hours, mins;
static int old_digit; //for partial erase
uint64_t wake_time;
int32_t id;
int32_t first_run;
int32_t woke_ntp_sync;
int32_t show_error; //1 if we need to show the battery empty icon and then sleep

static uint64_t x_lp_timer_hal_get_cycle_count(void) {
    lp_timer_ll_counter_snapshot(&LP_TIMER);
    uint32_t lo = lp_timer_ll_get_counter_value_low(&LP_TIMER, 0);
    uint32_t hi = lp_timer_ll_get_counter_value_high(&LP_TIMER, 0);
    lp_timer_counter_value_t result = {
        .lo = lo,
        .hi = hi
    };
    return result.val;
}


int main(void) {
	if (show_error) {
		//Show error warning and never wake up.
		EPD_Init();
		EPD_Digit(old_digit, 9+show_error);
		EPD_DeepSleep();
		lp_timer_ll_set_target_enable(&LP_TIMER, 1, false);
		ulp_lp_core_halt();
		while(1);
	}

	int digit=0;
	if (id==0) digit=mins%10;
	if (id==1) digit=mins/10;
	if (id==2) digit=hours%10;
	if (id==3) digit=hours/10;

	if (digit==0 || first_run) {
		EPD_Init();
	} else {
		EPD_Init_Part();
	}
	EPD_Digit(old_digit, digit);
	EPD_DeepSleep();
	old_digit=digit;
	

	if (hours==0 && mins==0 && !first_run) {
		//midnight: wake up for espnow sync
		ulp_lp_core_wakeup_main_processor();
	}
	
	//Note: for some reason, any sleep duration <22 sec will be rounded up to 22 sec.
	int64_t sleep_duration_us;
	if (id==0) {
		sleep_duration_us=60*1000*1000ULL;
		mins++;
	} else if (id==1) {
		sleep_duration_us=10*60*1000*1000ULL;
		mins=(mins/10)*10;
		mins+=10;
	} else if (id==2) {
		sleep_duration_us=60*60*1000*1000ULL;
		hours++;
		mins=0;
	} else { //id==3
		mins=0;
		if (hours<10) {
			sleep_duration_us=10*60*60*1000*1000ULL;
			hours+=10;
		} else if (hours<20) {
			sleep_duration_us=10*60*60*1000*1000ULL;
			hours+=10;
			if (!first_run) {
				//we wake up at 20:00 hours and need to do an ntp sync
				woke_ntp_sync=1;
				ulp_lp_core_wakeup_main_processor();
			}
		} else {
			sleep_duration_us=4*60*60*1000*1000ULL;
			hours=0;
		}
	}
	if (mins>=60) {
		hours+=1;
		mins-=60;
	}
	if (hours>=24) {
		hours-=24;
	}


	if (!first_run) {
		wake_time += (sleep_duration_us * (1 << RTC_CLK_CAL_FRACT) / clk_ll_rtc_slow_load_cal());
	}
	
	first_run=0;
	uint64_t min_sleep_time=((25*1000000LL) * (1 << RTC_CLK_CAL_FRACT) / clk_ll_rtc_slow_load_cal());
	if (wake_time<(x_lp_timer_hal_get_cycle_count()+min_sleep_time)) {
		//too short sleep time, simply run again
		main();
	} else {
		lp_timer_ll_clear_lp_alarm_intr_status(&LP_TIMER);
		lp_timer_ll_set_alarm_target(&LP_TIMER, 1, wake_time);
		lp_timer_ll_set_target_enable(&LP_TIMER, 1, true);

		ulp_lp_core_halt();
		while(1);
	}
}
