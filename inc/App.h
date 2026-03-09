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
} App;

#define initApp() {initObject(), {0}, 0, CONTROL_MODE, CONDUCTOR_ROLE, 0, 0, 120, 1, 0}

void reader(App *, int);
void receiver(App *, int);
void startApp(App *, int);

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
int tone_get_volume(ToneTask *, int);

#endif
