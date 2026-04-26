#ifndef _APP_H
#define _APP_H

#include "TinyTimber.h"
#include "canTinyTimber.h"
#include "sciTinyTimber.h"

#define BURST_BUF_SIZE 10

typedef enum
{
  CONTROL_MODE = 0,
  DELTA_SELECT_MODE = 1,
} Mode;

typedef struct
{
  Object super;
  char buffer[32];
  int buffer_pos;
  int mode;
  /* regulator state */
  Timer startup_timer;      /* reset at startApp; T_SAMPLE gives absolute time */
  Time  last_delivery_time; /* absolute timestamp of previous scheduled delivery */
  int   delta_sec;
  int   seq_tx;
  int   burst_mode;
  /* circular buffer — stores sequence numbers only (avoids CANMsg alignment) */
  int reg_buf[BURST_BUF_SIZE];
  int reg_head;
  int reg_tail;
  int reg_count;
} App;

#define initApp() {initObject(), {0}, 0, CONTROL_MODE, initTimer(), 0, 1, 0, 0, {}, 0, 0, 0}

void reader(App *, int);
void receiver(App *, int);
void startApp(App *, int);
void command_handler(App *, char);
void parameter_control_handler(App *, char);
void print_helper(App *);

void send_one_message(App *, int);
void burst_tick(App *, int);
void deliver_message(App *, int);

typedef struct
{
  Object super;
  int val;    // value for the volume
  int mute;   // mute or not
  int period; // period
} ToneTask;

#define initToneTask() {initObject(), 8, 0, 0};

// set method
void tone_set_mute(ToneTask *, int);
void tone_set_period(ToneTask *, int);
void tone_set_volume(ToneTask *, int);

// get method
int tone_get_volume(ToneTask *, int);
int tone_generator(ToneTask *, int);

extern App app;
extern ToneTask tone_task;
extern Can can0;
extern Serial sci0;

int clamp(int, int, int);
int get_period(int, int);
int get_beat_length(int);
void int_to_string(int, char *);

#endif
