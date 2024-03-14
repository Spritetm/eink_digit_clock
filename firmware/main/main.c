#include <stdio.h>
#include <inttypes.h>
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
#include "waittime.h"
#include "esp_wifi.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "soc/rtc.h"
#include "hal/clk_tree_ll.h"
#include "hal/lp_timer_ll.h"


#define TAG "main"

/*
How Does This Work?

On startup, each digit performs a sync of the current time via NTP. This gives an initial
idea of time without the other digits needing to be awake.

Every 24 hours, the digits will do an internal sync. The master (10s of hours) module
will wake up, do a NTP sync, then broadcast timestamps over esp-now every second for a
minute. The rest of the modules will wake up as well, listen for the timestamp data
and go back to sleep as soon as they're synced. (maybe: send ack before, so the master
can shut down as soon as it's seen three devices?)

*/

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

void cb_connection_ok(void *pvParameter){
	ip_event_got_ip_t* param = (ip_event_got_ip_t*)pvParameter;
	char str_ip[16];
	esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);
	ESP_LOGI(TAG, "I have a connection and my IP is %s!", str_ip);
}

SemaphoreHandle_t got_time_sem;

void time_sync_notification_cb(struct timeval *tv) {
	xSemaphoreGive(got_time_sem);
}

void do_sntp_sync() {
	got_time_sem=xSemaphoreCreateBinary();
	esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
	config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
	config.sync_cb = time_sync_notification_cb;
	esp_netif_sntp_init(&config);

	//start the wifi manager
	wifi_manager_start();
	wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);
	
	//wait until we get a time, or 3 minutes
	xSemaphoreTake(got_time_sem, pdMS_TO_TICKS(3*60*1000));
}


void app_main(void) {
	//Get ID from IO pins
	const gpio_config_t gpiocfg={
		.pin_bit_mask=(1<<IO_ID0)|(1<<IO_ID1)|(1<<IO_ID2),
		.mode=GPIO_MODE_INPUT,
		.pull_up_en=GPIO_PULLUP_ENABLE,
	};
	gpio_config(&gpiocfg);

	//Configure IO for LP CPU
	int pins[5]={IO_EPD_RESET, IO_EPD_BUSY, IO_EPD_CS, IO_EPD_SCLK, IO_EPD_MOSI};
	for (int i=0; i<5; i++) {
		rtc_gpio_init(pins[i]);
		rtc_gpio_set_direction(pins[i], RTC_GPIO_MODE_OUTPUT_ONLY);
		rtc_gpio_pulldown_dis(pins[i]);
		rtc_gpio_pullup_dis(pins[i]);
	}
	rtc_gpio_set_direction(IO_EPD_BUSY, RTC_GPIO_MODE_INPUT_ONLY);

	int id=0;
	if (!gpio_get_level(IO_ID0)) id|=1;
	if (!gpio_get_level(IO_ID1)) id|=2;
	if (!gpio_get_level(IO_ID2)) id|=4;
	ESP_LOGI(TAG, "Board ID %d", id);

	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
	/* not a wakeup from ULP, load the firmware */
	if (cause != ESP_SLEEP_WAKEUP_ULP) {
		ESP_LOGW(TAG, "Not a ULP wakeup, initializing it!");
		ESP_ERROR_CHECK(ulp_lp_core_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start)));

		//blocks until timeout or succesful sync
		do_sntp_sync();
	}

	setenv("TZ", "CST-8", 1);
	tzset();
	struct timeval tv;
	struct tm tm;
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	ulp_hours=tm.tm_hour;
	ulp_mins=tm.tm_min;
	ESP_LOGI(TAG, "Current time: %02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
	//With this set, the ULP will do a full erase and not increment to_wait_us as it knows
	//it's being called to show the data for the time *now*, not when the digit changes.
	ulp_first_run=1;
	ulp_id=id;

	//Check if RTC clk succesfully started
	//If we're at a weird frequency, or if we're not running off the 32KHz xtal (because
	//it doesn't work and ESP-IDF switched back to the 150KHz internal osc) we rather
	//not start at all than start with the wrong timing.
	vTaskDelay(1);
	uint32_t lpcur=lp_timer_hal_get_cycle_count();
	vTaskDelay(pdMS_TO_TICKS(1000));
	uint32_t lpnew=lp_timer_hal_get_cycle_count();
	ESP_LOGI(TAG, "One sec is %ld clock ticks", lpnew-lpcur);
	if (lpnew-lpcur>32770 || lpnew-lpcur<32760) {
		ESP_LOGE(TAG, "LP xtal not running!");
		vTaskDelay(pdMS_TO_TICKS(10));
		esp_deep_sleep_start();
	}


	//figure out how much time we need to wait
	int64_t to_wait_us=wait_time_us_for_digit(tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec, id);
	int64_t to_wait_cycle=(to_wait_us * (1 << RTC_CLK_CAL_FRACT) / clk_ll_rtc_slow_load_cal());

	uint64_t *wake_time=(uint64_t*)&ulp_wake_time;
	*wake_time=lp_timer_hal_get_cycle_count()+to_wait_cycle;
	ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup()); //barrier as well?
	//we want to wake up right now to show the current time
	ulp_lp_core_cfg_t cfg = {
		.wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
		.lp_timer_sleep_duration_us = 1
	};
	ESP_ERROR_CHECK(ulp_lp_core_run(&cfg));

/*
	ESP_LOGI(TAG, "Entering deep sleep for %lld sec", (to_wait_us/1000000));
	while(1) {
		printf("id %ld first %ld hour %ld min %ld\n", ulp_id, ulp_first_run, ulp_hours, ulp_mins);
		printf("wait until %lld cur %lld\n", *wake_time, lp_timer_hal_get_cycle_count());
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
*/

	/* Go back to sleep, only the ULP will run */
	ESP_LOGI(TAG, "Entering deep sleep for %lld sec", (to_wait_us/1000000));
	/* Small delay to ensure the messages are printed */
	vTaskDelay(pdMS_TO_TICKS(10));
	esp_deep_sleep_start();
}

