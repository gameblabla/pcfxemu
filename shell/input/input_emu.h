#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
uint16_t Read_Pad_Input(void);
int32_t Read_Mouse_X(void);
int32_t Read_Mouse_Y(void);
uint16_t Read_Mouse_buttons(void);
void Read_General_Input(void);

#endif
