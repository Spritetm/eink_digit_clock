#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../waittime.h"

int main() {
	for (int h=0; h<24; h++) {
		for (int m=0; m<60; m++) {
			for (int s=0; s<60; s++) {
				for (int id=0; id<4; id++) {
					int us=10;
					int64_t t=wait_time_us_for_digit(h, m, s, us, id);
					int sec=((t+us)/1000000);
					sec=sec+(s)+(m*60)+(h*60*60);
					int r=0;
					if (id==0 && ((sec%60)!=0)) r=1;
					if (id==1 && ((sec%600)!=0)) r=1;
					if (id==2 && ((sec%3600)!=0)) r=1;
					if (id==3) {
						int tgth=(sec%3600);
						if (tgth!=24 && (tgth%10)!=0) r=1;
					}
					if (r) {
						printf("Error for %02d:%02d:%02d digit %d: calc %d hour %d min %d sec\n",
							h,m,s,id, (sec/3600), (sec/60)%60, sec%60);
						printf("sec=%d\n", sec);
						exit(1);
					}
				}
			}
		}
	}
}
