#include <stdio.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "ulp_lp_core.h"
#include "ulp_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"
#include "ulp_main.h"
#include "hal/lp_timer_ll.h"


extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]	  asm("_binary_ulp_main_bin_end");

static uint64_t lp_timer_hal_get_cycle_count(void) {
    lp_timer_ll_counter_snapshot(&LP_TIMER);
    uint32_t lo = lp_timer_ll_get_counter_value_low(&LP_TIMER, 0);
    uint32_t hi = lp_timer_ll_get_counter_value_high(&LP_TIMER, 0);
    lp_timer_counter_value_t result = {
        .lo = lo,
        .hi = hi
    };
    return result.val;
}


static void init_ulp_program(void) {
	esp_err_t err = ulp_lp_core_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
	ESP_ERROR_CHECK(err);

	/* Start the program */
	ulp_lp_core_cfg_t cfg = {
		.wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
		.lp_timer_sleep_duration_us = 1 //ASAP; ULP program will handle actual timing.
	};

	err = ulp_lp_core_run(&cfg);
	ESP_ERROR_CHECK(err);
}

void app_main(void) {
	int pins[5]={IO_EPD_RESET, IO_EPD_BUSY, IO_EPD_CS, IO_EPD_SCLK, IO_EPD_MOSI};
	for (int i=0; i<5; i++) {
		printf("Configuring pin %d (%d)\n", pins[i], i);
		rtc_gpio_init(pins[i]);
		rtc_gpio_set_direction(pins[i], RTC_GPIO_MODE_OUTPUT_ONLY);
		rtc_gpio_pulldown_dis(pins[i]);
		rtc_gpio_pullup_dis(pins[i]);
		printf("Configuring pin %d done\n", pins[i]);
	}
	rtc_gpio_set_direction(IO_EPD_BUSY, RTC_GPIO_MODE_INPUT_ONLY);

	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
	/* not a wakeup from ULP, load the firmware */
	if (cause != ESP_SLEEP_WAKEUP_ULP) {
		printf("Not a ULP wakeup, initializing it! \n");
		init_ulp_program();
	}

	ulp_wake_time=lp_timer_hal_get_cycle_count();
	ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
	/* Go back to sleep, only the ULP will run */
	printf("Entering deep sleep\n\n");
	/* Small delay to ensure the messages are printed */
	vTaskDelay(pdMS_TO_TICKS(1000));
	esp_deep_sleep_start();
}

