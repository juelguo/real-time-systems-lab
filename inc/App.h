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

typedef enum
{
  SILENT_NONE = 0,
  SILENT_F1 = 1,
  SILENT_F2 = 2,
  SILENT_F3 = 3
} SilentMode;

#define COND_REASON_MANUAL 0
#define COND_REASON_AUTO 1
#define COND_REASON_DEAD 2
#define MUSICIAN_REASON_DISPLACED 0
#define MUSICIAN_REASON_FAILURE 1

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
  int play_session;  // monotonic playback session id
  int node_id;       // network node id
  int rank;          // node rank in active_nodes
  int conductor_id;  // current conductor
  int active_nodes[16];  // sorted active nodes
  int active_count;
  int known_nodes[16];   // all known nodes
  int known_count;
  int discovery_done;
  int disc_session;
  int last_token_index;
  int last_token_node;
  int watchdog_session;
  int cond_wd_session;
  int note_hb_session;
  int cond_hb_session;
  int print_session;
  int press_session;
  int is_silent;
  int silent_mode;
  int silent_session;
  int was_conductor;
  int can_err_count;
  int print_enabled;  // enable/disable periodic tempo/MUTED printing
} App;

#define initApp() {initObject(), {0}, 0, CONTROL_MODE, MUSICIAN_ROLE, 0, 0, 120, 1, 0, 0, -1, -1, -1, {0}, 0, {0}, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}

void reader(App *, int);
void receiver(App *, int);
void startApp(App *, int);

void play_one_note(App *, int);
void finish_note(App *, int);
void send_note_hb(App *, int);
void send_cond_hb(App *, int);
void watchdog_fire(App *, int);
void conductor_wd_fire(App *, int);
void periodic_print(App *, int);
void discovery_timeout(App *, int);
void f2_auto_exit(App *, int);

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
