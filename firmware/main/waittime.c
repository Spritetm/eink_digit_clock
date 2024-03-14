#include <stdio.h>
#include <stdlib.h>
#include <time.h>


int64_t wait_time_us_for_digit(int cur_h, int cur_m, int cur_s, int cur_us, int digit) {
	//figure out how much time we need to wait
	int64_t to_wait_sec;
	//time until next minute
	to_wait_sec=(60-cur_s);
	if (digit==1) {
		//time until next 10-min
		int min=(10-(cur_m%10));
		min--; //because we already wait till the next minute
		to_wait_sec+=(60*min);
	}
	if (digit==2) {
		//time to wait until next hour
		int min=(60-(cur_m));
		min--; //because we already wait till the next minute
		to_wait_sec+=(60*min);
	}
	if (digit==3) {
		//time to wait until next 10-hour
		int min=(60-(cur_m));
		if (cur_h<10) {
			min+=(9-cur_h)*60;
		} else if (cur_h<20) {
			min+=(19-cur_h)*60;
		} else {
			min+=(23-cur_h)*60;
		}
		min--; //because we already wait till the next minute
		to_wait_sec+=(60*min);
	}
	return (to_wait_sec*1000000LL)-cur_us;
}
