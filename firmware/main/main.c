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
#include "esp_now.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_adc/adc_cali.h"
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"

#define TAG "main"

/*
How Does This Work?

On startup, each digit performs a sync of the current time via NTP. This gives an initial
idea of time without the other digits needing to be awake.

Every 24 hours, the digits will do an internal sync. The master (10s of hours) module
will wake up, do a NTP sync, then broadcast timestamps over esp-now every second for a
minute. The rest of the modules will wake up as well, listen for the timestamp data, send
an ack if received and go back to sleep as soon as they're synced. The master will go
back to sleep a soon as it received all acks, or after 10 seconds. If a module can't sync,
it'll fallback to NTP sync.
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

#define CHANNEL 5

typedef struct {
	struct timeval tv;
	uint8_t id;
} pdata_t;

QueueHandle_t req_q;

void espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
	if (data_len==sizeof(pdata_t)) {
		xQueueSend(req_q, data, 0);
	}
}

#define SEND_TIME_SEC 10

int do_espnow_sync(int id) {
	int success=0;
	req_q=xQueueCreate(8, sizeof(pdata_t));
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK( nvs_flash_erase() );
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK( ret );
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE));
	ESP_ERROR_CHECK(esp_now_init());
//	ESP_ERROR_CHECK(esp_now_register_send_cb(example_espnow_send_cb));
	ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
	esp_now_peer_info_t peer={
		.channel = CHANNEL,
		.ifidx = ESP_IF_WIFI_AP,
		.encrypt = false,
		.peer_addr={0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
	};
	ESP_ERROR_CHECK(esp_now_add_peer(&peer));
	
	pdata_t packet;
	if (id==3) {
		int seen[4]={0,0,0,1};
		//tens hours digit, this sends time
		ESP_LOGI(TAG, "espnow sync; sending time");
		for (int sec=0; sec<SEND_TIME_SEC; sec++) {
			packet.id=3;
			gettimeofday(&packet.tv, NULL);
			ESP_ERROR_CHECK(esp_now_send(peer.peer_addr, (uint8_t*)&packet, sizeof(packet)));
			vTaskDelay(pdMS_TO_TICKS(1000));
			//Watch for acks. If we got an ack for all packets, we can bail out 
			//early and save some power.
			while(xQueueReceive(req_q, &packet, 0)) {
				ESP_LOGI(TAG, "Recved ack from id=%d", packet.id);
				if (packet.id<3) seen[packet.id]=1;
			}
			if (seen[0] && seen[1] && seen[2]) {
				success=1;
				break;
			}
		}
	} else {
		//other digit, this receives time.
		ESP_LOGI(TAG, "espnow sync; receiving time");
		uint64_t start=esp_timer_get_time();
		while (esp_timer_get_time()-start<SEND_TIME_SEC*1000000LL) {
			if (xQueueReceive(req_q, &packet, pdMS_TO_TICKS(1000))) {
				if (packet.id==3) {
				ESP_LOGI(TAG, "espnow sync; received time, sending ack");
					settimeofday(&packet.tv, NULL);
					//send ack
					packet.id=id;
					ESP_ERROR_CHECK(esp_now_send(peer.peer_addr, (uint8_t*)&packet, sizeof(packet)));
					vTaskDelay(pdMS_TO_TICKS(100));
					ESP_ERROR_CHECK(esp_now_send(peer.peer_addr, (uint8_t*)&packet, sizeof(packet)));
					vTaskDelay(pdMS_TO_TICKS(100));
					//and exit
					success=1;
					break;
				}
			}
		}
	}
	esp_now_deinit();
	vQueueDelete(req_q);
	ESP_ERROR_CHECK(esp_wifi_stop());
	ESP_ERROR_CHECK(esp_wifi_deinit());
	ESP_ERROR_CHECK(esp_event_loop_delete_default());
	ESP_LOGI(TAG, "espnow sync end; success=%d", success);
	return success;
}

void lp_init() {
	//Configure IO for LP CPU
	int pins[5]={IO_EPD_RESET, IO_EPD_BUSY, IO_EPD_CS, IO_EPD_SCLK, IO_EPD_MOSI};
	for (int i=0; i<5; i++) {
		rtc_gpio_init(pins[i]);
		rtc_gpio_set_direction(pins[i], RTC_GPIO_MODE_OUTPUT_ONLY);
		rtc_gpio_pulldown_dis(pins[i]);
		rtc_gpio_pullup_dis(pins[i]);
	}
	rtc_gpio_set_direction(IO_EPD_BUSY, RTC_GPIO_MODE_INPUT_ONLY);
	ESP_ERROR_CHECK(ulp_lp_core_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start)));
}

int battery_empty() {
	adc_oneshot_unit_handle_t adc1_handle;
	adc_oneshot_unit_init_cfg_t init_config1 = {
		.unit_id = ADC_UNIT_1,
		.ulp_mode = ADC_ULP_MODE_DISABLE,
	};
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
	adc_oneshot_chan_cfg_t config = {
		.bitwidth = ADC_BITWIDTH_DEFAULT,
		.atten = ADC_ATTEN_DB_12,
	};
	ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_5, &config));
	adc_cali_handle_t cal_handle;
	adc_cali_curve_fitting_config_t cali_config = {
		.unit_id = ADC_UNIT_1,
		.atten = ADC_ATTEN_DB_12,
		.bitwidth = ADC_BITWIDTH_DEFAULT,
	};
	ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cal_handle));
	int mv;
	ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(adc1_handle, cal_handle, ADC_CHANNEL_5, &mv));
	mv=mv*2; //because divided
	ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(cal_handle));
	ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
	ESP_LOGI(TAG, "Battery %d mv", mv);
	return (mv<2800);
}

#define ERROR_BAT 1
#define ERROR_RTC 2

void show_error(int err) {
	ulp_show_error=err;
	ulp_lp_core_cfg_t cfg = {
		.wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
		.lp_timer_sleep_duration_us = 1
	};
	ESP_ERROR_CHECK(ulp_lp_core_run(&cfg));
	esp_deep_sleep_start();
}


//This forces the time to 64 seconds to midnight if set to 1.
#define TEST_SYNC 0

void app_main(void) {
	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
	//Get ID from IO pins
	const gpio_config_t gpiocfg={
		.pin_bit_mask=(1<<IO_ID0)|(1<<IO_ID1)|(1<<IO_ID2),
		.mode=GPIO_MODE_INPUT,
		.pull_up_en=GPIO_PULLUP_ENABLE,
	};
	gpio_config(&gpiocfg);

//	enable for usb-serial-jtag to catch all messages
//	vTaskDelay(pdMS_TO_TICKS(2000));

	if (battery_empty()) {
		ESP_LOGW(TAG, "Battery empty! Showing and going to hibernate.");
		if (cause != ESP_SLEEP_WAKEUP_ULP) lp_init();
		show_error(ERROR_BAT);
	}

	int id=0;
	if (!gpio_get_level(IO_ID0)) id|=1;
	if (!gpio_get_level(IO_ID1)) id|=2;
	if (!gpio_get_level(IO_ID2)) id|=4;
	ESP_LOGI(TAG, "Board ID %d", id);

	/* not a wakeup from ULP, load the firmware */
	if (cause != ESP_SLEEP_WAKEUP_ULP) {
		ESP_LOGW(TAG, "Not a ULP wakeup, initializing the LP subsystem!");
		lp_init();
#if !TEST_SYNC
		//get the initial time over wifi
		//blocks until timeout or succesful sync
		do_sntp_sync();
#endif
		ulp_credit_dot=1;//indicate we just rebooted
	} else {
		//LP has waken the main processor. This can be for two reasons: either we are the tens-hours
		//digit and we need to do an NTP sync, or we are any board and we need an ESPNow sync.
		if (id==3 && ulp_woke_ntp_sync) {
			ESP_LOGI(TAG, "Woke from ULP to do NTP sync");
			do_sntp_sync();
			ulp_woke_ntp_sync=0;
			//Back to sleep. We'll sync the digits on ESPNow sync.
			//Disabled for now: esp_deep_sleep_start without lp init kills the lp :/
//			esp_deep_sleep_start();
		} else {
			ESP_LOGI(TAG, "Woke from ULP to do ESPNow sync");
			int r=do_espnow_sync(id);
			//Set credit dot if sync was unsuccesful
			ulp_credit_dot=r?0:0x55;
			if (id==3) {
				//Time didn't change; just go back to sleep.
				//Disabled for now: esp_deep_sleep_start without lp init kills the lp :/
//				esp_deep_sleep_start();
			} else {
				//If somehow we failed, fall back to ntp sync.
				if (!r) {
					//hack: wait for wifi stack deinit (do we still need this?)
					vTaskDelay(pdMS_TO_TICKS(2000));
					do_sntp_sync();
				}
			}
		}
	}

	setenv("TZ", "CST-8", 1);
	tzset();
	struct timeval tv;
	struct tm tm;
	gettimeofday(&tv, NULL);
#if TEST_SYNC
	tv.tv_sec=1710431935; //TEST: set time to slightly before midnight
#endif
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
		show_error(ERROR_RTC);
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
	//Rather than sleep, print LP variables
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

