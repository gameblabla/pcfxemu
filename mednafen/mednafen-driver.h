#ifndef __MDFN_MEDNAFEN_DRIVER_H
#define __MDFN_MEDNAFEN_DRIVER_H

#include <stdio.h>

/* Indent stdout newlines +- "indent" amount */
void MDFN_indent(int indent);
void MDFN_printf(const char *format, ...);

#ifdef WANT_THREADING
/* Being threading support. */
// Mostly based off SDL's prototypes and semantics.
// Driver code should actually define MDFN_Thread and MDFN_Mutex.

struct MDFN_Thread;
struct MDFN_Mutex;
struct MDFN_Cond;

MDFN_Thread *MDFND_CreateThread(int (*fn)(void *), void *data);
void MDFND_WaitThread(MDFN_Thread *thread, int *status);
void MDFND_KillThread(MDFN_Thread *thread);

MDFN_Mutex *MDFND_CreateMutex(void);
MDFN_Cond *MDFND_CreateCond(void);
void MDFND_DestroyCond(MDFN_Cond *cond);
void MDFND_DestroyMutex(MDFN_Mutex *mutex);
int MDFND_WaitCond(MDFN_Cond *cond, MDFN_Mutex *mutex);
int MDFND_SignalCond(MDFN_Cond *cond);
int MDFND_LockMutex(MDFN_Mutex *mutex);
int MDFND_UnlockMutex(MDFN_Mutex *mutex);

/* End threading support. */
#endif

void MDFN_DispMessage(const char *format, ...);

#endif
