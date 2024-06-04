//Initialize EPD
void EPD_Init(void);
//Initialize EPD, fast LUT
void EPD_Init_Fast(void);
//Initialize EPD, 2nd fast LUT
void EPD_init_Fast2(void);
//Initialize EPD, partial refresh LUT
void EPD_Init_Part(void);
//Put EPD to sleep
void EPD_DeepSleep(void);
//Power off EPD (but do not put to sleep)
void Power_off(void);
//Send display refresh
void EPD_Update(void);

//Note that all of the following routines really want oldData, that is
//the bitmap of the data that is already on screen, as an argument.

//Write a full frame of data to the screen for a full refresh
void EPD_WriteScreen_ALL(const unsigned char *datas, const unsigned char *oldData);
//Clear screen to white
void EPD_WriteScreen_White(const unsigned char *oldData);
//Clear screen to black
void EPD_WriteScreen_Black(const unsigned char *oldData);
//Send a full frame to the screen to do a partial refresh
void EPD_Dis_PartAll(const unsigned char * datas, const unsigned char *oldData);

//Send a digit from font_digits.h to the screen
void EPD_Digit(int oldDigit, int newDigit);
