#ifndef _APP_H
#define _APP_H

#include "TinyTimber.h"
#include "canTinyTimber.h"
#include "sciTinyTimber.h"

#define MIN_VOLUME 0
#define MAX_VOLUME 20
#define MIN_TEMPO 60
#define MAX_TEMPO 240
#define MIN_KEY -5
#define MAX_KEY 5
#define DEFAULT_VOLUME 8

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
  CAN_CMD_PLAY = 1,
  CAN_CMD_STOP = 2,
  CAN_CMD_SET_TEMPO = 3,
  CAN_CMD_SET_KEY = 4,
  CAN_CMD_SET_VOLUME = 5,
  CAN_CMD_MUTE_OUTPUT = 6,
  CAN_CMD_UNMUTE_OUTPUT = 7
} CanCommand;

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
} App;

#define initApp() {initObject(), {0}, 0, CONTROL_MODE, CONDUCTOR_ROLE, 0, 0, 120, 1, 0, 0}

void reader(App *, int);
void receiver(App *, int);
void startApp(App *, int);
void command_handler(App *, char);
void parameter_control_handler(App *, char);
void print_helper(App *);

void play_note(App *, int);
void stop_note(App *, int);
void apply_play(App *);
void apply_stop(App *);
int apply_tempo(App *, int);
int apply_key(App *, int);
int apply_volume(int);
void apply_output_mute(void);
void apply_output_unmute(void);
void send_can_player_command(App *, CanCommand, int);
void print_can_command_name(uchar);
int decode_command_value(uchar, uchar);
void print_can_message(char *, CANMsg *);

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
void tone_generator(ToneTask *, int, int);

extern App app;
extern ToneTask tone_task;
extern Can can0;
extern Serial sci0;

int clamp(int, int, int);
int get_period(int, int);
int get_beat_length(int);
void int_to_string(int, char *);

#endif
