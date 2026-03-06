#ifndef _APP_H
#define _APP_H

#include "TinyTimber.h"

// for media player
typedef enum
{
  CONTROL_MODE = 0,
  VOLUME_MODE = 1,
  KEY_MODE = 2,
  TEMPO_MODE = 3
} Mode;

typedef enum
{
  CONDUCTOR_ROLE = 0,
  MUSICIAN_ROLE = 1
} Role;

typedef struct
{
  Object super;
  char buffer[32]; // receive command
  int buffer_pos;  // pointer to buffer position
  int mode;
  int role;
  int current_index; // current tone index to play
  int key;           // shifted key
  int tempo;         // length to play
  int mute;          // indicate mute or not
  int status;        // indicate if the tone generator is already running

  // USER button state
  Timer button_clock;
  Timer button_callback_timer;
  Timer button_press_timer;
  Msg button_hold_msg;
  Time button_press_start;
  Time button_last_momentary_start;
  int button_has_last_callback;
  int button_has_last_momentary;
  int button_down;
  int button_hold_mode;
} App;

#define initApp()                                                              \
  {                                                                            \
    initObject(), {0}, 0, CONTROL_MODE, CONDUCTOR_ROLE, 0, 0, 120, 1, 0,      \
        initTimer(), initTimer(), initTimer(), NULL, 0, 0, 0, 0, 0, 0         \
  }

void reader(App *, int);
void receiver(App *, int);
void startApp(App *, int);
void user_button_event(App *, int);
void user_button_hold_timeout(App *, int);

void play_note(App *, int);
void stop_note(App *, int);

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
int tone_get_volume(ToneTask *);

#endif
