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
#define DEFAULT_KEY 0
#define DEFAULT_TEMPO 120
#define DEFAULT_VOLUME 8
#define BOARD_COUNT 3
#define BOARD_RANK_UNKNOWN -1
#define CONDUCTOR_NONE 0
#define BOARD_DOWN_TIMEOUT_MS 1000
#define BOARD_TIMEOUT_CHECK_PERIOD_MS 100
#define HEARTBEAT_PERIOD_MS 10
#define TEMPO_PRINT_PERIOD_SEC 10
#define MUTED_PRINT_PERIOD_SEC 5
#define DEFAULT_NODE_ID 3
#define CAN_NODE_BROADCAST 0x0F

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
  BOARD_STATUS_DOWN = 0,
  BOARD_STATUS_UP = 1
} BoardStatus;

typedef enum
{
  CAN_MSG_CONDUCTOR_CMD = 1,
  CAN_MSG_DISCOVERY_PING = 2,
  CAN_MSG_DISCOVERY_REPLY = 3,
  CAN_MSG_CONDUCTOR_ANNOUNCE = 4,
  CAN_MSG_TOKEN = 5,
  CAN_MSG_HEARTBEAT = 6
} CanMessageType;

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
  int node_id;             // Fixed board identity.
  int rank;                // Dynamic rank among currently UP boards, in range 0..2.
  int status;              // BoardStatus: UP or DOWN.
  int last_heartbeat_ms;   // Last observed heartbeat time in milliseconds.
} BoardInfo;

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
  int node_id;       // local board identity
  int conductor_id;  // current known conductor node id
  int tempo_print_enabled;
  int muted_print_enabled;
  BoardInfo boards[BOARD_COUNT];
} App;

#define initBoardInfo(nodeIdValue) {(nodeIdValue), BOARD_RANK_UNKNOWN, BOARD_STATUS_DOWN, 0}
#define initApp() {initObject(), {0}, 0, CONTROL_MODE, MUSICIAN_ROLE, 0, DEFAULT_KEY, DEFAULT_TEMPO, 1, 0, 0, DEFAULT_NODE_ID, CONDUCTOR_NONE, 0, 0, {initBoardInfo(1), initBoardInfo(2), initBoardInfo(3)}}

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
void reset_conductor_settings(App *);
void set_tempo_print_enabled(App *, int);
void periodic_tempo_report(App *, int);
int is_output_muted(void);
void toggle_output_mute(void);
void set_muted_print_enabled(App *, int);
void periodic_muted_report(App *, int);
int get_alive_board_count(App *);
int get_local_board_rank(App *);
int note_belongs_to_self(App *, int);
void sync_output_for_note(App *, int);
int apply_volume(int);
void apply_output_mute(void);
void apply_output_unmute(void);
void send_can_player_command(App *, CanCommand, int);
void send_discovery_ping(App *);
void send_discovery_reply(App *);
void send_heartbeat(App *);
void send_conductor_announce(App *);
void send_token(App *, int);
void periodic_heartbeat(App *, int);
int has_active_conductor(App *);
void set_known_conductor(App *, int);
void mark_board_up(App *, int);
void mark_board_down(App *, int);
void recompute_board_ranks(App *);
void check_board_timeouts(App *, int);
void print_membership(App *);
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
