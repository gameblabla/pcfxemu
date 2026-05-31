/* C11 PC-FX input router. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pcfx.h"
#include "interrupt.h"
#include "input.h"
#include "input/gamepad.h"
#include "input/mouse.h"
#include "../state_helpers.h"
#include "../mednafen-endian.h"

#define PCFX_PORTS 2
#define TOTAL_PORTS 8
#define TAP_PORTS 4

struct PCFX_Input_Device
{
 uint32 (*read_transfer_time)(PCFX_Input_Device *dev);
 uint32 (*write_transfer_time)(PCFX_Input_Device *dev);
 uint32 (*read)(PCFX_Input_Device *dev);
 void (*write)(PCFX_Input_Device *dev, uint32 data);
 void (*power)(PCFX_Input_Device *dev);
 void (*frame)(PCFX_Input_Device *dev, const void *data);
 int (*state_action)(PCFX_Input_Device *dev, StateMem *sm, int load, int data_only, const char *section_name);
 void (*destroy)(PCFX_Input_Device *dev);
 int type;
 int which;
 union
 {
  struct { uint16 buttons, old_raw_buttons; bool mode1, mode2; } gamepad;
  struct { int32 x, y; uint8 button; } mouse;
 } u;
};

static uint32 input_default_read_transfer_time(PCFX_Input_Device *dev) { (void)dev; return 1536; }
static uint32 input_default_write_transfer_time(PCFX_Input_Device *dev) { (void)dev; return 1536; }
static uint32 input_none_read(PCFX_Input_Device *dev) { (void)dev; return 0; }
static void input_none_write(PCFX_Input_Device *dev, uint32 data) { (void)dev; (void)data; }
static void input_none_power(PCFX_Input_Device *dev) { (void)dev; }
static void input_none_frame(PCFX_Input_Device *dev, const void *data) { (void)dev; (void)data; }
static int input_none_state_action(PCFX_Input_Device *dev, StateMem *sm, int load, int data_only, const char *section_name)
{ (void)dev; (void)sm; (void)load; (void)data_only; (void)section_name; return 1; }
static void input_default_destroy(PCFX_Input_Device *dev) { free(dev); }

static PCFX_Input_Device *input_alloc_none(int which)
{
 PCFX_Input_Device *dev = (PCFX_Input_Device*)calloc(1, sizeof(*dev));
 if(!dev) return NULL;
 dev->read_transfer_time = input_default_read_transfer_time;
 dev->write_transfer_time = input_default_write_transfer_time;
 dev->read = input_none_read;
 dev->write = input_none_write;
 dev->power = input_none_power;
 dev->frame = input_none_frame;
 dev->state_action = input_none_state_action;
 dev->destroy = input_default_destroy;
 dev->type = 0;
 dev->which = which;
 return dev;
}

PCFX_Input_Device *PCFX_Input_Device_CreateNone(void) { return input_alloc_none(-1); }
uint32 PCFX_Input_Device_ReadTransferTime(PCFX_Input_Device *dev) { return dev && dev->read_transfer_time ? dev->read_transfer_time(dev) : 1536; }
uint32 PCFX_Input_Device_WriteTransferTime(PCFX_Input_Device *dev) { return dev && dev->write_transfer_time ? dev->write_transfer_time(dev) : 1536; }
uint32 PCFX_Input_Device_Read(PCFX_Input_Device *dev) { return dev && dev->read ? dev->read(dev) : 0; }
void PCFX_Input_Device_Write(PCFX_Input_Device *dev, uint32 data) { if(dev && dev->write) dev->write(dev, data); }
void PCFX_Input_Device_Power(PCFX_Input_Device *dev) { if(dev && dev->power) dev->power(dev); }
void PCFX_Input_Device_Frame(PCFX_Input_Device *dev, const void *data) { if(dev && dev->frame) dev->frame(dev, data); }
int PCFX_Input_Device_StateAction(PCFX_Input_Device *dev, StateMem *sm, int load, int data_only, const char *section_name)
{ return (dev && dev->state_action) ? dev->state_action(dev, sm, load, data_only, section_name) : 1; }
void PCFX_Input_Device_Destroy(PCFX_Input_Device *dev) { if(dev && dev->destroy) dev->destroy(dev); }

/* Gamepad implementation is in this C translation unit's public factory companion. */
static uint32 gamepad_read(PCFX_Input_Device *dev) { return dev->u.gamepad.buttons | (FX_SIG_PAD << 28); }
static void gamepad_power(PCFX_Input_Device *dev) { dev->u.gamepad.buttons = 0; }
static void gamepad_frame(PCFX_Input_Device *dev, const void *data)
{
 uint16 new_buttons = 0;
 if(data) new_buttons = MDFN_de16lsb((const uint8*)data);
 if((dev->u.gamepad.old_raw_buttons ^ new_buttons) & (1 << 12) & new_buttons) dev->u.gamepad.mode1 = !dev->u.gamepad.mode1;
 if((dev->u.gamepad.old_raw_buttons ^ new_buttons) & (1 << 14) & new_buttons) dev->u.gamepad.mode2 = !dev->u.gamepad.mode2;
 dev->u.gamepad.buttons = new_buttons & ~((1 << 12) | (1 << 14));
 dev->u.gamepad.buttons |= (uint16)(dev->u.gamepad.mode1 << 12);
 dev->u.gamepad.buttons |= (uint16)(dev->u.gamepad.mode2 << 14);
 dev->u.gamepad.old_raw_buttons = new_buttons;
}
static int gamepad_state_action(PCFX_Input_Device *dev, StateMem *sm, int load, int data_only, const char *section_name)
{
 SFORMAT StateRegs[] = { SFVAR(dev->u.gamepad.buttons), SFVAR(dev->u.gamepad.old_raw_buttons), SFVAR(dev->u.gamepad.mode1), SFVAR(dev->u.gamepad.mode2), SFEND };
 return MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name, false);
}
PCFX_Input_Device *PCFXINPUT_MakeGamepad(int which)
{
 PCFX_Input_Device *dev = input_alloc_none(which);
 if(!dev) return NULL;
 dev->type = 1;
 dev->read = gamepad_read;
 dev->power = gamepad_power;
 dev->frame = gamepad_frame;
 dev->state_action = gamepad_state_action;
 return dev;
}

/* Mouse implementation. */
static uint32 mouse_read(PCFX_Input_Device *dev)
{
 uint32 moo = FX_SIG_MOUSE << 28;
 int32 rel_x = dev->u.mouse.x;
 int32 rel_y = dev->u.mouse.y;
 if(rel_x < -127) rel_x = -127;
 if(rel_x > 127) rel_x = 127;
 if(rel_y < -127) rel_y = -127;
 if(rel_y > 127) rel_y = 127;
 moo |= ((rel_x & 0xFF) << 8) | ((rel_y & 0xFF) << 0);
 dev->u.mouse.x -= rel_x;
 dev->u.mouse.y -= rel_y;
 moo |= dev->u.mouse.button << 16;
 return moo;
}
static void mouse_power(PCFX_Input_Device *dev) { dev->u.mouse.button = 0; dev->u.mouse.x = 0; dev->u.mouse.y = 0; }
static void mouse_frame(PCFX_Input_Device *dev, const void *data)
{
 if(!data) return;
 dev->u.mouse.x += (int32)MDFN_de32lsb((const uint8*)data + 0);
 dev->u.mouse.y += (int32)MDFN_de32lsb((const uint8*)data + 4);
 dev->u.mouse.button = *((const uint8*)data + 8);
}
static int mouse_state_action(PCFX_Input_Device *dev, StateMem *sm, int load, int data_only, const char *section_name)
{
 SFORMAT StateRegs[] = { SFVAR(dev->u.mouse.x), SFVAR(dev->u.mouse.y), SFVAR(dev->u.mouse.button), SFEND };
 return MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name, false);
}
PCFX_Input_Device *PCFXINPUT_MakeMouse(int which)
{
 PCFX_Input_Device *dev = input_alloc_none(which);
 if(!dev) return NULL;
 dev->type = 2;
 dev->read = mouse_read;
 dev->power = mouse_power;
 dev->frame = mouse_frame;
 dev->state_action = mouse_state_action;
 return dev;
}

static const int TapMap[2][TAP_PORTS] = { { 0, 2, 3, 4 }, { 1, 5, 6, 7 } };
static void RemakeDevices(int which);

/* Mednafen-specific input type numerics */
enum { FXIT_NONE = 0, FXIT_GAMEPAD = 1, FXIT_MOUSE = 2 };

static PCFX_Input_Device *devices[TOTAL_PORTS] = { NULL };
static uint8 TapCounter[PCFX_PORTS];
static uint8 control[PCFX_PORTS];
static bool latched[PCFX_PORTS];
static int32 LatchPending[PCFX_PORTS];
static int InputTypes[TOTAL_PORTS];
static void *data_ptr[TOTAL_PORTS];
static uint32 data_latch[TOTAL_PORTS];
static v810_timestamp_t lastts;

static void SyncSettings(void);

void FXINPUT_Init(void)
{
#ifdef PCFX_WASM
 /* The WASM frontend resets its bump allocator between media loads.  If a
  * previous load path did not get a normal shutdown, device pointers may now
  * reference reused linear-memory bytes rather than valid structs, so do not
  * call through their destroy function pointers here. */
 for(int i = 0; i < TOTAL_PORTS; i++)
 {
  devices[i] = NULL;
  InputTypes[i] = FXIT_NONE;
 }
#else
 FXINPUT_Kill();
#endif
 memset(TapCounter, 0, sizeof(TapCounter));
 memset(control, 0, sizeof(control));
 memset(latched, 0, sizeof(latched));
 memset(LatchPending, 0, sizeof(LatchPending));
 memset(data_ptr, 0, sizeof(data_ptr));
 memset(data_latch, 0, sizeof(data_latch));
 lastts = 0;
 SyncSettings();
 RemakeDevices(-1);
}

void FXINPUT_Kill(void)
{
 for(int i = 0; i < TOTAL_PORTS; i++)
 {
  PCFX_Input_Device_Destroy(devices[i]);
  devices[i] = NULL;
  InputTypes[i] = FXIT_NONE;
  data_ptr[i] = NULL;
  data_latch[i] = 0;
 }
 memset(TapCounter, 0, sizeof(TapCounter));
 memset(control, 0, sizeof(control));
 memset(latched, 0, sizeof(latched));
 memset(LatchPending, 0, sizeof(LatchPending));
 lastts = 0;
}

void FXINPUT_SettingChanged(void) { SyncSettings(); }

static inline int32 min3(int32 a, int32 b, int32 c)
{
 int32 ret = a;
 if(b < ret) ret = b;
 if(c < ret) ret = c;
 return ret;
}

static inline int32 CalcNextEventTS(const v810_timestamp_t timestamp)
{
 return min3(LatchPending[0] > 0 ? (timestamp + LatchPending[0]) : PCFX_EVENT_NONONO, LatchPending[1] > 0 ? (timestamp + LatchPending[1]) : PCFX_EVENT_NONONO, PCFX_EVENT_NONONO);
}

static void RemakeDevices(int which)
{
 int s = 0, e = TOTAL_PORTS;
 if(which != -1) { s = which; e = which + 1; }
 for(int i = s; i < e; i++)
 {
  PCFX_Input_Device_Destroy(devices[i]);
  devices[i] = NULL;
  switch(InputTypes[i])
  {
   default:
   case FXIT_NONE: devices[i] = input_alloc_none(i); break;
   case FXIT_GAMEPAD: devices[i] = PCFXINPUT_MakeGamepad(i); break;
   case FXIT_MOUSE: devices[i] = PCFXINPUT_MakeMouse(i); break;
  }
  if(!devices[i]) devices[i] = input_alloc_none(i);
 }
}

void FXINPUT_SetInput(int port, uint_fast8_t type, void *ptr)
{
 if(port < 0 || port >= TOTAL_PORTS) return;
 data_ptr[port] = ptr;
 switch(type)
 {
  case 0: InputTypes[port] = FXIT_GAMEPAD; break;
  case 1: InputTypes[port] = FXIT_MOUSE; break;
  default: InputTypes[port] = FXIT_NONE; break;
 }
 RemakeDevices(port);
}

uint8 FXINPUT_Read8(uint32 A, const v810_timestamp_t timestamp)
{
 return (uint8)(FXINPUT_Read16(A & ~1U, timestamp) >> ((A & 1) * 8));
}

uint16 FXINPUT_Read16(uint32 A, const v810_timestamp_t timestamp)
{
 FXINPUT_Update(timestamp);
 uint16 ret = 0;
 A &= 0xC2;
 if(A == 0x00 || A == 0x80)
 {
  int w = (A & 0x80) >> 7;
  int scanning = (LatchPending[w] > 0) ? 1 : 0;
  ret = (latched[w] ? 0x8 : 0x0) | scanning;
 }
 else
 {
  int which = (A >> 7) & 1;
  ret = (uint16)(data_latch[which] >> ((A & 2) ? 16 : 0));
  if(!(A & 0x2)) latched[which] = false;
 }
 if(!latched[0] && !latched[1]) PCFXIRQ_Assert(PCFXIRQ_SOURCE_INPUT, false);
 return ret;
}

void FXINPUT_Write16(uint32 A, uint16 V, const v810_timestamp_t timestamp)
{
 FXINPUT_Update(timestamp);
 switch(A & 0xC0)
 {
  case 0x80:
  case 0x00:
  {
   int w = (A & 0x80) >> 7;
   if((V & 0x1) && !(control[w] & 0x1))
   {
    LatchPending[w] = 1536;
    latched[w] = false;
    PCFX_SetEvent(PCFX_EVENT_PAD, CalcNextEventTS(timestamp));
   }
   control[w] = V & 0x7;
   break;
  }
 }
}

void FXINPUT_Write8(uint32 A, uint8 V, const v810_timestamp_t timestamp) { FXINPUT_Write16(A, V, timestamp); }

void FXINPUT_Frame(void)
{
 for(uint_fast8_t i = 0; i < TOTAL_PORTS; i++) PCFX_Input_Device_Frame(devices[i], data_ptr[i]);
}

v810_timestamp_t FXINPUT_Update(const v810_timestamp_t timestamp)
{
 int32 run_time = timestamp - lastts;
 for(uint_fast8_t i = 0; i < 2; i++)
 {
  if(LatchPending[i] > 0)
  {
   LatchPending[i] -= run_time;
   if(LatchPending[i] <= 0)
   {
    data_latch[i] = PCFX_Input_Device_Read(devices[i]);
    latched[i] = true;
    control[i] &= ~1;
    PCFXIRQ_Assert(PCFXIRQ_SOURCE_INPUT, true);
   }
  }
 }
 lastts = timestamp;
 return CalcNextEventTS(timestamp);
}

void FXINPUT_ResetTS(int32 ts_base) { lastts = ts_base; }

int FXINPUT_StateAction(StateMem *sm, int load, int data_only)
{
 SFORMAT StateRegs[] = { SFARRAY(TapCounter, 2), SFARRAY32(LatchPending, 2), SFARRAY(control, 2), SFARRAYB(latched, 2), SFARRAY32(data_latch, 2), SFEND };
 int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "INPUT", false);
 for(uint_fast8_t i = 0; i < TOTAL_PORTS; i++)
 {
  char sname[256];
  snprintf(sname, sizeof(sname), "INPUT%d:%d", (int)i, InputTypes[i]);
  ret &= PCFX_Input_Device_StateAction(devices[i], sm, load, data_only, sname);
 }
 return ret;
}

static void SyncSettings(void)
{
 (void)TapMap;
}
