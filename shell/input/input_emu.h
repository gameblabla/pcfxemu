#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t Read_Pad_Input(void);
uint16_t Read_Pad_Input_Player(unsigned player);
int32_t Read_Mouse_X(void);
int32_t Read_Mouse_Y(void);
uint16_t Read_Mouse_buttons(void);
void Read_General_Input(void);

#ifdef __cplusplus
}
#endif

#endif
