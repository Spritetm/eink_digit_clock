//ULP code to control the E-ink screen.
//Note that this is originally cribbed from what I assume is manufacturers code.
//For my changes:
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */


#include "epd_w21.h"
#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_gpio.h"
#include "hal/rtc_io_ll.h"
#include "../io.h"
#include "font_decode.h"
#include "font_digits.h"

#define EPD_ARRAY ((240*416)/8)

//Main program can set this to indicate some sort of error that still keeps the
//clock working
int credit_dot;
static int old_credit_dot;

static inline void EPD_W21_Rst(int v) {
	ulp_lp_core_gpio_set_level(IO_EPD_RESET, v);
}

static void lcd_chkstatus(void) {
	int t=0;
	while(!ulp_lp_core_gpio_get_level(IO_EPD_BUSY)) { //0:BUSY, 1:FREE
		ulp_lp_core_delay_us(1000);
		t++;
		if (t>5000) break;
	}
}

static inline void delay(int ms) {
	ulp_lp_core_delay_us(ms*1000);
}

static void inline spi_send(int cmd) {
	ulp_lp_core_gpio_set_level(IO_EPD_CS, 0);
#if 0
	//straightforward version of the code below
	for (int msk=0x100; msk!=0; msk>>=1) {
		ulp_lp_core_gpio_set_level(IO_EPD_MOSI, (cmd&msk)?1:0);
		ulp_lp_core_gpio_set_level(IO_EPD_SCLK, 0);
		ulp_lp_core_gpio_set_level(IO_EPD_SCLK, 1);
	}
#else
	//unrolled and poke things directly, about a 30% speedup
	rtcio_ll_set_level(IO_EPD_MOSI, (cmd&0x100));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1tc, out_data_w1tc, BIT(IO_EPD_SCLK));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1ts, out_data_w1ts, BIT(IO_EPD_SCLK));
	ulp_lp_core_gpio_set_level(IO_EPD_MOSI, (cmd&0x80));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1tc, out_data_w1tc, BIT(IO_EPD_SCLK));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1ts, out_data_w1ts, BIT(IO_EPD_SCLK));
	ulp_lp_core_gpio_set_level(IO_EPD_MOSI, (cmd&0x40));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1tc, out_data_w1tc, BIT(IO_EPD_SCLK));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1ts, out_data_w1ts, BIT(IO_EPD_SCLK));
	ulp_lp_core_gpio_set_level(IO_EPD_MOSI, (cmd&0x20));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1tc, out_data_w1tc, BIT(IO_EPD_SCLK));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1ts, out_data_w1ts, BIT(IO_EPD_SCLK));
	ulp_lp_core_gpio_set_level(IO_EPD_MOSI, (cmd&0x10));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1tc, out_data_w1tc, BIT(IO_EPD_SCLK));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1ts, out_data_w1ts, BIT(IO_EPD_SCLK));
	ulp_lp_core_gpio_set_level(IO_EPD_MOSI, (cmd&0x8));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1tc, out_data_w1tc, BIT(IO_EPD_SCLK));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1ts, out_data_w1ts, BIT(IO_EPD_SCLK));
	ulp_lp_core_gpio_set_level(IO_EPD_MOSI, (cmd&0x4));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1tc, out_data_w1tc, BIT(IO_EPD_SCLK));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1ts, out_data_w1ts, BIT(IO_EPD_SCLK));
	ulp_lp_core_gpio_set_level(IO_EPD_MOSI, (cmd&0x2));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1tc, out_data_w1tc, BIT(IO_EPD_SCLK));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1ts, out_data_w1ts, BIT(IO_EPD_SCLK));
	ulp_lp_core_gpio_set_level(IO_EPD_MOSI, (cmd&0x1));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1tc, out_data_w1tc, BIT(IO_EPD_SCLK));
	HAL_FORCE_MODIFY_U32_REG_FIELD(LP_IO.out_data_w1ts, out_data_w1ts, BIT(IO_EPD_SCLK));
#endif
	ulp_lp_core_gpio_set_level(IO_EPD_CS, 1);
}

static void EPD_W21_WriteDATA(uint8_t cmd) {
	spi_send((int)cmd|0x100);
}

static void EPD_W21_WriteCMD(uint8_t cmd) {
	spi_send(cmd);
}

//UC8253
void EPD_Init(void) {
	EPD_W21_Rst(0);	   // Module reset
	delay(10);//At least 10ms delay 
	EPD_W21_Rst(1);
	delay(10);//At least 10ms delay 

	EPD_W21_WriteCMD(0x00);
	EPD_W21_WriteDATA(0x1F); //rotate 180

	EPD_W21_WriteCMD(0x04);	 //Power on
	lcd_chkstatus();		//waiting for the electronic paper IC to release the idle signal

	EPD_W21_WriteCMD(0X50);	 //VCOM AND DATA INTERVAL SETTING	  
	EPD_W21_WriteDATA(0x97); //WBmode:VBDF 17|D7 VBDW 97 VBDB 57	WBRmode:VBDF F7 VBDW 77 VBDB 37	 VBDR B7  
}

void EPD_Init_Fast(void) {//1.0s
	EPD_W21_Rst(0);	   // Module reset
	delay(10);//At least 10ms delay 
	EPD_W21_Rst(1);
	delay(10);//At least 10ms delay 

	EPD_W21_WriteCMD(0x00);
	EPD_W21_WriteDATA(0x1F); //180 mirror
  
	EPD_W21_WriteCMD(0x04);	 //Power on
	lcd_chkstatus();		//waiting for the electronic paper IC to release the idle signal

	EPD_W21_WriteCMD(0xE0);
	EPD_W21_WriteDATA(0x02); 

	EPD_W21_WriteCMD(0xE5);
	EPD_W21_WriteDATA(0x5F);  //0x5A--1.5s, 0x5F--1s
}

void EPD_init_Fast2(void) { //1.5s
	EPD_W21_Rst(0);	   // Module reset
	delay(10);//At least 10ms delay 
	EPD_W21_Rst(1);
	delay(10);//At least 10ms delay 

	EPD_W21_WriteCMD(0x00);
	EPD_W21_WriteDATA(0x1F); //180 mirror

	EPD_W21_WriteCMD(0x04);	 //Power on
	lcd_chkstatus();		//waiting for the electronic paper IC to release the idle signal

	EPD_W21_WriteCMD(0xE0);
	EPD_W21_WriteDATA(0x02); 

	EPD_W21_WriteCMD(0xE5);
	EPD_W21_WriteDATA(0x5A);  //0x5A--1.5s, 0x5F--1s
}

void EPD_Init_Part(void) {
	EPD_W21_Rst(0);	   // Module reset
	delay(10);//At least 10ms delay 
	EPD_W21_Rst(1);
	delay(10);//At least 10ms delay 

	EPD_W21_WriteCMD(0x00);
	EPD_W21_WriteDATA(0x1F); //180 mirror

	EPD_W21_WriteCMD(0x04);	 //Power on
	lcd_chkstatus();		//waiting for the electronic paper IC to release the idle signal

	EPD_W21_WriteCMD(0xE0);
	EPD_W21_WriteDATA(0x02); 

	EPD_W21_WriteCMD(0xE5);
	EPD_W21_WriteDATA(0x6E);

	EPD_W21_WriteCMD(0x50); 
	EPD_W21_WriteDATA(0xD7); 
}

void EPD_DeepSleep(void) {
	EPD_W21_WriteCMD(0X02);	  //power off
	lcd_chkstatus();		  //waiting for the electronic paper IC to release the idle signal
	delay(100);//At least 100ms delay 
	EPD_W21_WriteCMD(0X07);	  //deep sleep
	EPD_W21_WriteDATA(0xA5); 
}

void Power_off(void) { 
	EPD_W21_WriteCMD(0x02); //POWER OFF
	lcd_chkstatus();
}

//Full screen refresh update function
void EPD_Update(void) {
	//Refresh
	EPD_W21_WriteCMD(0x12);	  //DISPLAY REFRESH	  
	delay(1);			   //!!!The delay here is necessary, 200uS at least!!!	   
	lcd_chkstatus();		  //waiting for the electronic paper IC to release the idle signal
}

void EPD_Digit(int oldDigit, int newDigit) {
	//Write Data
	font_digit_reset(font_digits[oldDigit]);
	EPD_W21_WriteCMD(0x10);		   //Transfer old data
	int n=font_digit_get_byte();
	if (old_credit_dot) n=old_credit_dot;
	EPD_W21_WriteDATA(n);
	for(int i=1;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(font_digit_get_byte());  //Transfer the actual displayed data
	}
	font_digit_reset(font_digits[newDigit]);
	EPD_W21_WriteCMD(0x13);		   //Transfer new data
	n=font_digit_get_byte();
	if (credit_dot) n=credit_dot;
	EPD_W21_WriteDATA(n);
	for(int i=1;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(font_digit_get_byte());  //Transfer the actual displayed data
	}
	EPD_Update();
	old_credit_dot=credit_dot;
	Power_off();
}


void EPD_WriteScreen_ALL(const unsigned char *datas, const unsigned char *oldData) {
	unsigned int i;
	//Write Data
	EPD_W21_WriteCMD(0x10);		   //Transfer old data
	for(i=0;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(oldData[i]);	//Transfer the actual displayed data
	}
	EPD_W21_WriteCMD(0x13);		   //Transfer new data
	for(i=0;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(datas[i]);  //Transfer the actual displayed data
	}
	EPD_Update();
	Power_off();
}

//Clear screen display
void EPD_WriteScreen_White(const unsigned char *oldData) {
	unsigned int i;
	//Write Data
	EPD_W21_WriteCMD(0x10);
	for(i=0;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(oldData[i]);
	}
	EPD_W21_WriteCMD(0x13);
	for(i=0;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(0xff);
	}
	EPD_Update();
	Power_off();
}

//Display all black
void EPD_WriteScreen_Black(const unsigned char *oldData) {
	unsigned int i;
	//Write Data
	EPD_W21_WriteCMD(0x10);
	for(i=0;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(oldData[i]);
	}
	EPD_W21_WriteCMD(0x13);
	for(i=0;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(0x00);
	}
	EPD_Update();
	Power_off();
}


//Full screen partial refresh display
void EPD_Dis_PartAll(const unsigned char * datas, const unsigned char *oldData)
{
	unsigned int i;
	EPD_Init_Part();
	//Write Data
	EPD_W21_WriteCMD(0x10);				//Transfer old data
	for(i=0;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(oldData[i]);  //Transfer the actual displayed data
	}
	EPD_W21_WriteCMD(0x13);				//Transfer new data
	for(i=0;i<EPD_ARRAY;i++) {
		EPD_W21_WriteDATA(datas[i]);	//Transfer the actual displayed data
	}
	
	EPD_Update();
	Power_off();
}


