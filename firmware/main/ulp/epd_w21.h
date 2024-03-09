void EPD_Init(void);
void EPD_Init_Fast(void);
void EPD_init_Fast2(void);
void EPD_Init_Part(void);
void EPD_DeepSleep(void);
void Power_off(void);
void EPD_Update(void);
void EPD_WhiteScreen_ALL(const unsigned char *datas, const unsigned char *oldData);
void EPD_WhiteScreen_White(const unsigned char *oldData);
void EPD_WhiteScreen_Black(const unsigned char *oldData);
void EPD_Dis_PartAll(const unsigned char * datas, const unsigned char *oldData);

void EPD_Digit(int oldDigit, int newDigit);
