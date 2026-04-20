#include "App.h"
#include "TinyTimber.h"
#include "canTinyTimber.h"
#include "sciTinyTimber.h"

// custom incl
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// constant
const int brother_john_melodies[32] = {
    0, 2, 4, 0, 0, 2, 4, 0,
    4, 5, 7, 4, 5, 7, 7, 9,
    7, 5, 4, 0, 7, 9, 7, 5,
    4, 0, 0, -5, 0, 0, -5, 0};

const int periods[25] = {
    2024, 1911, 1803, 1702, 1607, // Indices -10 to -6
    1517, 1432, 1351, 1276, 1204, // Indices -5 to -1
    1136, 1073, 1012, 955, 902,   // Indices 0 to 4
    851, 803, 758, 716, 675,      // Indices 5 to 9
    638, 602, 568, 536, 506       // Indices 10 to 14
};

const int beat_lengths[32] = {
    2, 2, 2, 2, 2, 2, 2, 2, // a a a a a a a a
    2, 2, 4, 2, 2, 4,       // a a b a a b
    1, 1, 1, 1, 2, 2,       // c c c c a a
    1, 1, 1, 1, 2, 2,       // c c c c a a
    2, 2, 4, 2, 2, 4        // a a b a a b
};

const int MIN_SHIFT = -10;

#define DAC_PORT (*(char *)0x4000741C)
#define MIN_VOLUME 0
#define MAX_VOLUME 20
#define MIN_TEMPO 60
#define MAX_TEMPO 240
#define MIN_KEY -5
#define MAX_KEY 5
#define DEFAULT_VOLUME 8

#define CAN_MSG_CONDUCTOR_CMD      1
#define CAN_MSG_DISCOVERY_PING     2
#define CAN_MSG_DISCOVERY_REPLY    3
#define CAN_MSG_CONDUCTOR_ANNOUNCE 4
#define CAN_MSG_TOKEN              5
#define CAN_MSG_HEARTBEAT          6
#define CAN_SUB_START  1
#define CAN_SUB_STOP   2
#define CAN_SUB_KEY    3
#define CAN_SUB_TEMPO  4
#define CAN_NODE_BROADCAST 0x0F
#define F3_FAIL_THRESHOLD 3
#define PRINT_INTERVAL_S 10

extern App app;
extern ToneTask tone_task;
extern Can can0;
extern Serial sci0;

static int output_muted = 0;
static int volume_before_output_mute = DEFAULT_VOLUME;
static int can_print_enabled = 0;
static unsigned int rand_state = 12345u;

// internal helper function
void int_to_string(int n, char *buffer);
static char *can_msg_name(int msgId);
static void print_can_msg_with_prefix(const char *prefix, CANMsg *msg);

static unsigned int simple_rand(void)
{
  rand_state = rand_state * 1664525u + 1013904223u;
  return rand_state;
}

// Resets the program state to its initial values
static void reset_program_state(void)
{
  App fresh_app = initApp();
  ToneTask fresh_tone = initToneTask();
  Can fresh_can = initCan(CAN_PORT0, &app, receiver);
  Serial fresh_sci = initSerial(SCI_PORT0, &app, reader);

  app = fresh_app;
  tone_task = fresh_tone;
  can0 = fresh_can;
  sci0 = fresh_sci;

  output_muted = 0;
  volume_before_output_mute = DEFAULT_VOLUME;
  can_print_enabled = 0;
  rand_state = 12345u;
}

static int rand_range(int lo, int hi)
{
  return lo + (int)(simple_rand() % (unsigned int)(hi - lo + 1));
}

static int is_valid_node_id(int node_id)
{
  return node_id >= 0 && node_id <= 14;
}

static void arr_insert_sorted(int *arr, int *count, int val)
{
  // don't insert duplicates
  for (int i = 0; i < *count; i++)
    if (arr[i] == val) return;
  int i = *count;
  while (i > 0 && arr[i-1] > val) { arr[i] = arr[i-1]; i--; }
  arr[i] = val;
  (*count)++;
}

static void arr_remove(int *arr, int *count, int val)
{
  for (int i = 0; i < *count; i++)
  {
    if (arr[i] == val)
    {
      for (int j = i; j < *count - 1; j++) arr[j] = arr[j+1];
      (*count)--;
      return;
    }
  }
}

static int arr_contains(int *arr, int count, int val)
{
  for (int i = 0; i < count; i++)
    if (arr[i] == val) return 1;
  return 0;
}

static int arr_rank_of(int *arr, int count, int val)
{
  for (int i = 0; i < count; i++)
    if (arr[i] == val) return i;
  return -1;
}

static void add_active_node(App *self, int nid)
{
  arr_insert_sorted(self->active_nodes, &self->active_count, nid);
  arr_insert_sorted(self->known_nodes, &self->known_count, nid);
  self->rank = arr_rank_of(self->active_nodes, self->active_count, self->node_id);
}

static void remove_active_node(App *self, int nid)
{
  arr_remove(self->active_nodes, &self->active_count, nid);
  self->rank = arr_rank_of(self->active_nodes, self->active_count, self->node_id);
}

static void add_known_node(App *self, int nid)
{
  arr_insert_sorted(self->known_nodes, &self->known_count, nid);
}

static int lowest_active_nodeId(App *self)
{
  if (self->active_count == 0) return self->node_id;
  return self->active_nodes[0];
}

static int can_send_raw(int msgId, int nodeId, uchar *data, int len)
{
  CANMsg msg;
  msg.msgId = msgId;
  msg.nodeId = (uchar)nodeId;
  msg.length = (uchar)len;
  for (int i = 0; i < len; i++) msg.buff[i] = data[i];

  int status = CAN_SEND(&can0, &msg);
  if (can_print_enabled && status == 0)
    print_can_msg_with_prefix("Sent", &msg);
  return status;
}

static void send_discovery_ping(App *self)
{
  can_send_raw(CAN_MSG_DISCOVERY_PING, self->node_id, 0, 0);
}

static void send_discovery_reply(App *self, int to_node)
{
  (void)to_node;
  uchar d[1]; d[0] = (uchar)self->node_id;
  can_send_raw(CAN_MSG_DISCOVERY_REPLY, self->node_id, d, 1);
}

static void send_conductor_announce(App *self)
{
  uchar d[17];
  d[0] = (uchar)self->node_id;
  for (int i = 0; i < self->active_count && i < 16; i++)
    d[1+i] = (uchar)self->active_nodes[i];
  can_send_raw(CAN_MSG_CONDUCTOR_ANNOUNCE, self->node_id, d, 1 + self->active_count);
}

static void send_token(App *self, int next_note_index)
{
  int token_index = next_note_index & 0xFF;

  if (self->active_count == 1 && self->rank == 0)
  {
    self->last_token_index = token_index;
    self->last_token_node = self->node_id;
    play_one_note(self, token_index);
    return;
  }

  uchar d[1];
  d[0] = (uchar)token_index;
  can_send_raw(CAN_MSG_TOKEN, self->node_id, d, 1);
}

static void send_heartbeat_now(App *self)
{
  uchar d[1]; d[0] = (uchar)self->node_id;
  can_send_raw(CAN_MSG_HEARTBEAT, self->node_id, d, 1);
}

static void send_conductor_cmd(App *self, int sub, int val)
{
  uchar d[3];
  d[0] = (uchar)sub;

  if (sub == CAN_SUB_TEMPO)
  {
    d[1] = (uchar)((val >> 8) & 0xFF);
    d[2] = (uchar)(val & 0xFF);
    can_send_raw(CAN_MSG_CONDUCTOR_CMD, self->node_id, d, 3);
    return;
  }

  if (sub == CAN_SUB_START || sub == CAN_SUB_STOP)
  {
    can_send_raw(CAN_MSG_CONDUCTOR_CMD, self->node_id, d, 1);
    return;
  }

  d[1] = (uchar)val;
  can_send_raw(CAN_MSG_CONDUCTOR_CMD, self->node_id, d, 2);
}

static char *can_msg_name(int msgId)
{
  if (msgId == CAN_MSG_CONDUCTOR_CMD) return "CONDUCTOR_CMD";
  if (msgId == CAN_MSG_DISCOVERY_PING) return "DISCOVERY_PING";
  if (msgId == CAN_MSG_DISCOVERY_REPLY) return "DISCOVERY_REPLY";
  if (msgId == CAN_MSG_CONDUCTOR_ANNOUNCE) return "CONDUCTOR_ANNOUNCE";
  if (msgId == CAN_MSG_TOKEN) return "TOKEN";
  if (msgId == CAN_MSG_HEARTBEAT) return "HEARTBEAT";
  return "UNKNOWN";
}

static void print_can_msg_with_prefix(const char *prefix, CANMsg *msg)
{
  char tmp[12];

  SCI_WRITE(&sci0, "\n");
  SCI_WRITE(&sci0, prefix);
  SCI_WRITE(&sci0, " CAN: ");
  SCI_WRITE(&sci0, can_msg_name(msg->msgId));
  SCI_WRITE(&sci0, " [msgId=");
  int_to_string(msg->msgId, tmp);
  SCI_WRITE(&sci0, tmp);
  SCI_WRITE(&sci0, ", nodeId=");
  int_to_string(msg->nodeId, tmp);
  SCI_WRITE(&sci0, tmp);
  SCI_WRITE(&sci0, ", length=");
  int_to_string(msg->length, tmp);
  SCI_WRITE(&sci0, tmp);
  SCI_WRITE(&sci0, ", data=");

  if (msg->length == 0)
  {
    SCI_WRITE(&sci0, "[]\n");
    return;
  }

  SCI_WRITE(&sci0, "[");
  for (int i = 0; i < msg->length; i++)
  {
    if (i > 0) SCI_WRITE(&sci0, " ");
    int_to_string(msg->buff[i], tmp);
    SCI_WRITE(&sci0, tmp);
  }
  SCI_WRITE(&sci0, "]\n");
}

static void print_can_msg(CANMsg *msg)
{
  print_can_msg_with_prefix("Received", msg);
}

// forward declarations
static void become_conductor(App *self, int reason);
static void become_musician(App *self, int reason);
static void reset_watchdog(App *self);
static void reset_conductor_wd(App *self);

int clamp(int value, int min, int max)
{
  if (value < min)
    return min;
  if (value > max)
    return max;
  return value;
}

// convert int to string
void int_to_string(int n, char *buffer)
{
  int i = 0, is_negative = 0;
  if (n == 0)
  {
    buffer[i++] = '0';
    buffer[i] = '\0';
    return;
  }
  if (n < 0)
  {
    is_negative = 1;
    n = -n;
  }
  while (n != 0)
  {
    buffer[i++] = (n % 10) + '0';
    n = n / 10;
  }
  if (is_negative)
  {
    // add sign
    buffer[i++] = '-';
  }
  buffer[i] = '\0';

  for (int j = 0; j < i / 2; j++)
  {
    char temp = buffer[j];
    buffer[j] = buffer[i - j - 1];
    buffer[i - j - 1] = temp;
  }
}

// method for tone generator
void tone_set_mute(ToneTask *self, int value)
{
  self->mute = value;
}

void tone_set_volume(ToneTask *self, int value)
{
  self->val = value;
}

void tone_set_period(ToneTask *self, int value)
{
  self->period = value;
}

int tone_get_volume(ToneTask *self, int unused)
{
  (void)unused;
  return self->val;
}

void tone_generator(ToneTask *self, int state, int period)
{
  if (self->mute == 1)
  {
    DAC_PORT = 0;
    SEND(USEC(self->period), USEC(100), self, tone_generator, state);
    return;
  }

  int next_state = state ? 0 : 1;

  if (next_state == 1)
  {
    DAC_PORT = tone_get_volume(self, 0);
  }
  else
  {
    DAC_PORT = 0;
  }

  SEND(USEC(self->period), USEC(100), self, tone_generator, next_state);
}

// get the shifted value with input key
int get_period(int note_pos, int key_offset)
{
  int base = brother_john_melodies[note_pos]; // original frequent of the indicate note

  int shifted = base + key_offset;

  int actual_index = shifted - MIN_SHIFT;

  // error catch
  if (actual_index < 0)
    actual_index = 0;
  if (actual_index > 24)
    actual_index = 24;

  return periods[actual_index];
}

int apply_tempo(App *self, int tempo)
{
  self->tempo = clamp(tempo, MIN_TEMPO, MAX_TEMPO);
  return self->tempo;
}

int apply_key(App *self, int key)
{
  self->key = clamp(key, MIN_KEY, MAX_KEY);
  return self->key;
}

int apply_volume(int volume)
{
  int clamped = clamp(volume, MIN_VOLUME, MAX_VOLUME);
  volume_before_output_mute = clamped;
  if (!output_muted)
  {
    SYNC(&tone_task, tone_set_volume, clamped);
  }
  return clamped;
}

void apply_output_mute(void)
{
  if (output_muted)
  {
    return;
  }
  volume_before_output_mute = clamp(SYNC(&tone_task, tone_get_volume, 0), MIN_VOLUME, MAX_VOLUME);
  output_muted = 1;
  SYNC(&tone_task, tone_set_volume, 0);
}

void apply_output_unmute(void)
{
  if (!output_muted)
  {
    return;
  }
  output_muted = 0;
  SYNC(&tone_task, tone_set_volume, clamp(volume_before_output_mute, MIN_VOLUME, MAX_VOLUME));
}

void apply_play(App *self)
{
  self->play_session++;
  self->status = 1;
  send_conductor_cmd(self, CAN_SUB_START, 0);
  send_token(self, 0);
}

void apply_stop(App *self)
{
  self->play_session++;
  self->mute = 1;
  self->status = 0;
  SYNC(&tone_task, tone_set_mute, 1);
  send_conductor_cmd(self, CAN_SUB_STOP, 0);
}

void play_one_note(App *self, int note_index)
{
  if (self->is_silent) return;
  if (self->active_count == 0) return;
  // only play if it's our turn
  if ((note_index % self->active_count) != self->rank) return;

  int session = ++self->play_session;
  self->current_index = note_index & 31;
  self->status = 1;

  int period = get_period(self->current_index, self->key);
  int length = beat_lengths[self->current_index] * (30000 / self->tempo);
  int active_time = length - 50;
  if (active_time < 10) active_time = 10;

  SYNC(&tone_task, tone_set_period, period);
  SYNC(&tone_task, tone_set_mute, 0);

  // start 10ms heartbeat loop while playing
  self->note_hb_session++;
  SEND(MSEC(100), MSEC(2), self, send_note_hb, self->note_hb_session);

  SEND(MSEC(active_time), MSEC(2), self, finish_note, session);
}

void finish_note(App *self, int session)
{
  if (session != self->play_session) return;

  // stop heartbeat
  self->note_hb_session++;

  SYNC(&tone_task, tone_set_mute, 1);
  self->status = 0;

  int next = (self->current_index + 1) & 31;
  int next_abs = (self->last_token_index - (self->last_token_index & 31) + next);
  // actually track absolute note count
  next_abs = (self->last_token_index + 1) & 0xFF;
  self->last_token_index = next_abs;

  // send next token to all
  send_token(self, next_abs);
}

void send_note_hb(App *self, int session)
{
  if (session != self->note_hb_session) return;
  if (self->is_silent) return;
  if (self->active_count > 1)
    send_heartbeat_now(self);
  SEND(MSEC(10), MSEC(2), self, send_note_hb, session);
}

void send_cond_hb(App *self, int session)
{
  if (session != self->cond_hb_session) return;
  if (self->is_silent) return;
  if (self->role != CONDUCTOR_ROLE) return;
  if (self->active_count > 1)
    send_heartbeat_now(self);
  SEND(MSEC(100), MSEC(5), self, send_cond_hb, session);
}

void watchdog_fire(App *self, int session)
{
  if (session != self->watchdog_session) return;
  // the note-player missed its slot — skip it and send next token
  int next = (self->last_token_index + 1) & 0xFF;
  self->last_token_index = next;
  if (self->role == CONDUCTOR_ROLE)
    send_token(self, next);
}

void conductor_wd_fire(App *self, int session)
{
  if (session != self->cond_wd_session) return;
  // conductor gone — elect lowest active rank
  remove_active_node(self, self->conductor_id);
  if (lowest_active_nodeId(self) == self->node_id)
    become_conductor(self, COND_REASON_AUTO);
}

static void reset_watchdog(App *self)
{
  int session = ++self->watchdog_session;
  int delay = 200 + self->rank * 200;
  SEND(MSEC(delay), MSEC(10), self, watchdog_fire, session);
}

static void reset_conductor_wd(App *self)
{
  int session = ++self->cond_wd_session;
  SEND(MSEC(500), MSEC(20), self, conductor_wd_fire, session);
}

static void do_discovery(App *self)
{
  if (self->node_id < 0) return;
  self->discovery_done = 0;
  self->disc_session++;
  add_active_node(self, self->node_id);
  send_discovery_ping(self);
  SEND(MSEC(500), MSEC(20), self, discovery_timeout, self->disc_session);
}

void discovery_timeout(App *self, int session)
{
  if (session != self->disc_session) return;
  self->discovery_done = 1;
  // P4: if network is dead (only us responded) and no conductor, become conductor
  // if (self->active_count <= 1 && self->conductor_id == -1)
  // {
  //   become_conductor(self, COND_REASON_DEAD);
  //   send_conductor_announce(self);
  //   send_token(self, 0);
  // }
}

static void enter_silent_failure(App *self, int mode)
{
  if (self->is_silent) return;
  self->is_silent = 1;
  self->silent_mode = mode;
  self->was_conductor = (self->role == CONDUCTOR_ROLE);
  // stop all timers
  self->note_hb_session++;
  self->cond_hb_session++;
  self->watchdog_session++;
  self->cond_wd_session++;
  self->play_session++;
  self->status = 0;
  SYNC(&tone_task, tone_set_mute, 1);
  if (self->was_conductor)
    SCI_WRITE(&sci0, "\nConductorship Void Due To Failure\n");
  SCI_WRITE(&sci0, "\nSilent Failure\n");
  if (mode == SILENT_F2)
  {
    // schedule auto exit in 5-10s
    int delay = rand_range(5000, 10000);
    self->silent_session++;
    SEND(MSEC(delay), MSEC(100), self, f2_auto_exit, self->silent_session);
  }
}

static void leave_silent_failure(App *self)
{
  if (!self->is_silent) return;
  self->is_silent = 0;
  self->silent_session++;
  SCI_WRITE(&sci0, "\nLeave Silent Failure\n");
  // if we were conductor, rejoin as musician (P4 requirement)
  if (self->was_conductor)
  {
    self->was_conductor = 0;
    self->role = MUSICIAN_ROLE;
    SCI_WRITE(&sci0, "\nI Now Join As A Musician\n");
  }
  // re-run discovery; if network is dead (no other active nodes),
  // the node becomes conductor after the 500ms window (handled below)
  self->active_count = 0;
  do_discovery(self);
}


void f2_auto_exit(App *self, int session)
{
  if (session != self->silent_session) return;
  leave_silent_failure(self);
}

static void become_conductor(App *self, int reason)
{
  self->role = CONDUCTOR_ROLE;
  self->conductor_id = self->node_id;
  // start idle conductor heartbeat
  self->cond_hb_session++;
  SEND(MSEC(100), MSEC(5), self, send_cond_hb, self->cond_hb_session);
  // cancel musician watchdog
  self->watchdog_session++;
  self->cond_wd_session++;

  if (reason == COND_REASON_MANUAL)
    SCI_WRITE(&sci0, "\nClaimed Conductorship\n");
  else
    SCI_WRITE(&sci0, "\nI Am The New Conductor\n");
  // start periodic tempo printing if enabled
  if (self->print_enabled)
  {
    self->print_session++;
    SEND(MSEC(PRINT_INTERVAL_S * 1000), MSEC(100), self, periodic_print, self->print_session);
  }
}

static void become_musician(App *self, int reason)
{
  int was_cond = (self->role == CONDUCTOR_ROLE);
  self->role = MUSICIAN_ROLE;
  // stop conductor heartbeat and periodic print
  self->cond_hb_session++;
  self->print_session++;
  if (was_cond && reason == MUSICIAN_REASON_DISPLACED)
    SCI_WRITE(&sci0, "\nConductorship Void\n");
}

static void claim_conductorship(App *self)
{
  become_conductor(self, COND_REASON_MANUAL);
  send_conductor_announce(self);
}

static void handle_discovery_ping(App *self, CANMsg *msg)
{
  int sender = msg->nodeId;
  int was_active = arr_contains(self->active_nodes, self->active_count, sender);

  add_known_node(self, sender);
  if (!was_active)
    add_active_node(self, sender);

  // Always answer discovery while not silent so a fresh conductor scan keeps us active.
  if (!self->is_silent)
    send_discovery_reply(self, sender);
}

static void handle_discovery_reply(App *self, CANMsg *msg)
{
  int sender = msg->nodeId;

  // Keep compatibility with older reply frames that encoded the sender only in buff[0].
  if (msg->length >= 1 && is_valid_node_id((int)msg->buff[0]))
    sender = msg->buff[0];

  add_active_node(self, sender);
  add_known_node(self, sender);
}

static void handle_conductor_announce(App *self, CANMsg *msg)
{
  // if (msg->length < 1) return;
  int new_cond = msg->nodeId;
  // rebuild active list from announce
  self->active_count = 0;
  for (int i = 0; i < msg->length; i++)
    arr_insert_sorted(self->active_nodes, &self->active_count, msg->buff[i]);
  if (!self->is_silent)
    add_active_node(self, self->node_id);
  self->rank = arr_rank_of(self->active_nodes, self->active_count, self->node_id);
  self->conductor_id = new_cond;
  if (new_cond == self->node_id)
    become_conductor(self, COND_REASON_MANUAL);
  else
    become_musician(self, MUSICIAN_REASON_DISPLACED);
}

static void handle_token(App *self, CANMsg *msg)
{
  if (msg->length < 1) return;
  int idx = (int)msg->buff[0];
  self->last_token_index = idx;
  // reset our watchdog
  reset_watchdog(self);
  // reset conductor watchdog
  reset_conductor_wd(self);
  // play if it's our turn
  if (!self->is_silent && self->active_count > 0 && (idx % self->active_count) == self->rank)
    play_one_note(self, idx);
}

static void handle_heartbeat(App *self, CANMsg *msg)
{
  int sender = msg->nodeId;
  if (sender == self->conductor_id)
    reset_conductor_wd(self);
  else
    reset_watchdog(self);
}

static void handle_conductor_cmd(App *self, CANMsg *msg)
{
  if (msg->length < 1) return;
  int sub = msg->buff[0];
  int val = (msg->length > 1) ? (int)msg->buff[1] : 0;
  if (sub == CAN_SUB_START)
  {
    self->play_session++;
    self->status = 1;
    if (self->role == CONDUCTOR_ROLE)
      send_token(self, 0);
  }
  else if (sub == CAN_SUB_STOP)
  {
    self->play_session++;
    self->status = 0;
    SYNC(&tone_task, tone_set_mute, 1);
  }
  else if (sub == CAN_SUB_KEY)
  {
    apply_key(self, (int)(int8_t)val);
  }
  else if (sub == CAN_SUB_TEMPO)
  {
    if (msg->length < 3) return;
    apply_tempo(self, ((int)msg->buff[1] << 8) | (int)msg->buff[2]);
  }
}

void receiver(App *self, int unused)
{
  (void)unused;
  CANMsg msg;
  if (CAN_RECEIVE(&can0, &msg) != 0)
    return;
  if (can_print_enabled)
    print_can_msg(&msg);
  if (msg.msgId == CAN_MSG_DISCOVERY_PING)
  {

    handle_discovery_ping(self, &msg);
  }
  else if (msg.msgId == CAN_MSG_DISCOVERY_REPLY)
    handle_discovery_reply(self, &msg);
  else if (msg.msgId == CAN_MSG_CONDUCTOR_ANNOUNCE)
    handle_conductor_announce(self, &msg);
  else if (msg.msgId == CAN_MSG_TOKEN)
    handle_token(self, &msg);
  else if (msg.msgId == CAN_MSG_HEARTBEAT)
    handle_heartbeat(self, &msg);
  else if (msg.msgId == CAN_MSG_CONDUCTOR_CMD)
    handle_conductor_cmd(self, &msg);
}

// print the helper
void print_helper(App *self)
{
  char tmp[12];

  SCI_WRITE(&sci0, "\n=== Brother John Music Player ===\n");
  if (self->role == CONDUCTOR_ROLE)
  {
    SCI_WRITE(&sci0, "Board role: CONDUCTOR (keyboard controls local + CAN broadcast)\n");
  }
  else
  {
    SCI_WRITE(&sci0, "Board role: MUSICIAN (controlled by incoming CAN commands)\n");
  }
  SCI_WRITE(&sci0, "Node id: ");
  if (self->node_id < 0)
  {
    SCI_WRITE(&sci0, "UNSET (use 'node <id>' to start discovery)\n");
  }
  else
  {
    int_to_string(self->node_id, tmp);
    SCI_WRITE(&sci0, tmp);
    SCI_WRITE(&sci0, "\n");
  }
  SCI_WRITE(&sci0, "Enter a command and press Enter.\n");

  SCI_WRITE(&sci0, "\nRole Commands:\n");
  SCI_WRITE(&sci0, "  c | conductor         Switch to conductor mode\n");
  SCI_WRITE(&sci0, "  u | musician          Switch to musician mode\n");

  SCI_WRITE(&sci0, "\nPlayback Commands:\n");
  SCI_WRITE(&sci0, "  p | play              Play the melody\n");
  SCI_WRITE(&sci0, "  q | stop              Stop the melody\n");

  SCI_WRITE(&sci0, "\nSettings Commands:\n");
  SCI_WRITE(&sci0, "  t | tempo             Set tempo (60-240 BPM)\n");
  SCI_WRITE(&sci0, "  k | key               Set key offset (-5 to +5)\n");
  SCI_WRITE(&sci0, "  v | volume            Set volume (0-20)\n");

  SCI_WRITE(&sci0, "\nHardware Commands:\n");
  SCI_WRITE(&sci0, "  s | mute              Mute tone output\n");
  SCI_WRITE(&sci0, "  r | unmute            Resume tone output\n");
  SCI_WRITE(&sci0, "  h | help              Show this menu\n");

  SCI_WRITE(&sci0, "\nNetwork Commands:\n");
  SCI_WRITE(&sci0, "  node <id>             Set this board's node id and start discovery\n");
  SCI_WRITE(&sci0, "  claim                 Claim conductor role (P2)\n");
  SCI_WRITE(&sci0, "  R                     Reset key and tempo to defaults\n");
  SCI_WRITE(&sci0, "  m | member            Show all boards membership status\n");
  SCI_WRITE(&sci0, "  I                     Show local node/rank/role info\n");
  SCI_WRITE(&sci0, "  T                     Toggle local audio mute (musician)\n");
  SCI_WRITE(&sci0, "  P                     Toggle periodic tempo/MUTED printing\n");
  SCI_WRITE(&sci0, "  L | canlog            Toggle CAN RX/TX message printing\n");
  SCI_WRITE(&sci0, "  f1                    Toggle F1 silent failure\n");
  SCI_WRITE(&sci0, "  f2                    Enter F2 (auto-recover 5-10s)\n");
  SCI_WRITE(&sci0, "  f3                    Enter F3 (simulate CAN disconnect)\n");
  SCI_WRITE(&sci0, "  z                     Exit silent failure manually\n");

  SCI_WRITE(&sci0, "\nIn Settings Mode (tempo/key/volume):\n");
  SCI_WRITE(&sci0, "  Type a number and press Enter\n");
  SCI_WRITE(&sci0, "  e                     Cancel and return to main menu\n");

  SCI_WRITE(&sci0, "\nChoice: ");
}

// handle "parameter"
void parameter_control_handler(App *self, char controL_character)
{
  if (controL_character == 'e')
  {
    self->mode = CONTROL_MODE;
    self->buffer_pos = 0;
    SCI_WRITE(&sci0, "\nReturn to main menu...\n");
    print_helper(self);
    return;
  }

  if (controL_character == '\n' || controL_character == '\r')
  {
    self->buffer[self->buffer_pos] = '\0';
    int value = atoi(self->buffer);

    char out_buf[12];

    if (self->mode == VOLUME_MODE)
    {
      int current_val = clamp(value, MIN_VOLUME, MAX_VOLUME);
      current_val = apply_volume(current_val);
      int_to_string(current_val, out_buf);
      SCI_WRITE(&sci0, "\nVolume set to: ");
    }
    else if (self->mode == TEMPO_MODE)
    {
      int tempo = clamp(value, MIN_TEMPO, MAX_TEMPO);
      if (self->role == CONDUCTOR_ROLE)
      {
        tempo = apply_tempo(self, tempo);
      }
      send_conductor_cmd(self, CAN_SUB_TEMPO, tempo);
      int_to_string(tempo, out_buf);
      SCI_WRITE(&sci0, "\nTempo set to: ");
    }
    else if (self->mode == KEY_MODE)
    {
      int key = clamp(value, MIN_KEY, MAX_KEY);
      if (self->role == CONDUCTOR_ROLE)
      {
        key = apply_key(self, key);
      }
      send_conductor_cmd(self, CAN_SUB_KEY, key);
      int_to_string(key, out_buf);
      SCI_WRITE(&sci0, "\nKey offset set to: ");
    }

    SCI_WRITE(&sci0, out_buf);
    SCI_WRITE(&sci0, "\nChoice: ");
    self->buffer_pos = 0;
  }
  else if (self->buffer_pos < (int)(sizeof(self->buffer) - 1))
  {
    self->buffer[self->buffer_pos++] = controL_character;
    SCI_WRITECHAR(&sci0, controL_character);
  }
}

void command_handler(App *self, char c)
{
  if (c == '\n' || c == '\r')
  {
    self->buffer[self->buffer_pos] = '\0';

    // if we reached the begining of buffer
    if (self->buffer_pos == 0)
    {
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "h") == 0 || strcmp(self->buffer, "help") == 0)
    {
      self->buffer_pos = 0;
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "L") == 0 || strcmp(self->buffer, "canlog") == 0)
    {
      self->buffer_pos = 0;
      can_print_enabled = !can_print_enabled;
      if (can_print_enabled)
        SCI_WRITE(&sci0, "\nCAN RX/TX message printing enabled.\n");
      else
        SCI_WRITE(&sci0, "\nCAN RX/TX message printing disabled.\n");
      return;
    }

    if (strncmp(self->buffer, "node", 4) == 0)
    {
      char *arg = self->buffer + 4;
      char *end = NULL;
      long id;

      while (*arg == ' ') arg++;
      id = strtol(arg, &end, 10);
      while (end != NULL && *end == ' ') end++;

      self->buffer_pos = 0;
      if (self->node_id >= 0)
      {
        SCI_WRITE(&sci0, "\nNode id already configured. Restart to change it.\n");
        print_helper(self);
        return;
      }
      if (arg == end || end == NULL || *end != '\0' || !is_valid_node_id((int)id))
      {
        SCI_WRITE(&sci0, "\nInvalid node id. Use 'node <id>' with 0-14.\n");
        print_helper(self);
        return;
      }

      self->node_id = (int)id;
      SCI_WRITE(&sci0, "\nNode id set. Starting discovery.\n");
      do_discovery(self);
      print_helper(self);
      return;
    }

    if (self->node_id < 0)
    {
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nSet node <id> before using network controls.\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "c") == 0 || strcmp(self->buffer, "conductor") == 0)
    {
      self->role = CONDUCTOR_ROLE;
      self->mode = CONTROL_MODE;
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nSwitched to CONDUCTOR mode.\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "u") == 0 || strcmp(self->buffer, "musician") == 0)
    {
      self->role = MUSICIAN_ROLE;
      self->mode = CONTROL_MODE;
      self->buffer_pos = 0;
      apply_stop(self);
      SCI_WRITE(&sci0, "\nSwitched to MUSICIAN mode.\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "claim") == 0)
    {
      self->buffer_pos = 0;
      claim_conductorship(self);
      return;
    }
    if (strcmp(self->buffer, "f1") == 0)
    {
      self->buffer_pos = 0;
      if (self->is_silent && self->silent_mode == SILENT_F1)
        leave_silent_failure(self);
      else
        enter_silent_failure(self, SILENT_F1);
      return;
    }
    if (strcmp(self->buffer, "f2") == 0)
    {
      self->buffer_pos = 0;
      enter_silent_failure(self, SILENT_F2);
      return;
    }
    if (strcmp(self->buffer, "f3") == 0)
    {
      self->buffer_pos = 0;
      enter_silent_failure(self, SILENT_F3);
      return;
    }
    if (strcmp(self->buffer, "z") == 0)
    {
      self->buffer_pos = 0;
      leave_silent_failure(self);
      return;
    }
    if (strcmp(self->buffer, "m") == 0 || strcmp(self->buffer, "member") == 0 || strcmp(self->buffer, "membership") == 0)
    {
      // show all known nodes and whether they are active or silent
      self->buffer_pos = 0;
      char tmp[8];
      SCI_WRITE(&sci0, "\nMembership:\n");
      for (int i = 0; i < self->known_count; i++)
      {
        int nid = self->known_nodes[i];
        SCI_WRITE(&sci0, "  node ");
        int_to_string(nid, tmp);
        SCI_WRITE(&sci0, tmp);
        if (arr_contains(self->active_nodes, self->active_count, nid))
          SCI_WRITE(&sci0, " [active]");
        else
          SCI_WRITE(&sci0, " [silent]");
        if (nid == self->conductor_id)
          SCI_WRITE(&sci0, " <conductor>");
        SCI_WRITE(&sci0, "\n");
      }
      return;
    }
    if (strcmp(self->buffer, "R") == 0)
    {
      // R resets key and tempo to defaults (P1 requirement)
      self->buffer_pos = 0;
      self->key = 0;
      self->tempo = 120;
      if (self->role == CONDUCTOR_ROLE)
      {
        send_conductor_cmd(self, CAN_SUB_KEY, 0);
        send_conductor_cmd(self, CAN_SUB_TEMPO, 120);
      }
      SCI_WRITE(&sci0, "\nKey and tempo reset to defaults.\n");
      return;
    }
    if (strcmp(self->buffer, "I") == 0)
    {
      // show local info
      self->buffer_pos = 0;
      char tmp[8];
      SCI_WRITE(&sci0, "\nNode: ");
      int_to_string(self->node_id, tmp);
      SCI_WRITE(&sci0, tmp);
      SCI_WRITE(&sci0, "  Rank: ");
      int_to_string(self->rank, tmp);
      SCI_WRITE(&sci0, tmp);
      SCI_WRITE(&sci0, "  Role: ");
      if (self->role == CONDUCTOR_ROLE)
        SCI_WRITE(&sci0, "CONDUCTOR");
      else
        SCI_WRITE(&sci0, "MUSICIAN");
      SCI_WRITE(&sci0, "\n");
      return;
    }
    if (strcmp(self->buffer, "T") == 0)
    {
      // toggle local audio mute (musician requirement)
      self->buffer_pos = 0;
      if (output_muted)
      {
        apply_output_unmute();
        SCI_WRITE(&sci0, "\nUnmuted.\n");
      }
      else
      {
        apply_output_mute();
        SCI_WRITE(&sci0, "\nMuted.\n");
      }
      return;
    }
    if (strcmp(self->buffer, "P") == 0)
    {
      // toggle periodic printing enable/disable
      self->buffer_pos = 0;
      self->print_enabled = !self->print_enabled;
      if (self->print_enabled)
      {
        self->print_session++;
        SEND(MSEC(PRINT_INTERVAL_S * 1000), MSEC(100), self, periodic_print, self->print_session);
        SCI_WRITE(&sci0, "\nPeriodic print enabled.\n");
      }
      else
      {
        self->print_session++;
        SCI_WRITE(&sci0, "\nPeriodic print disabled.\n");
      }
      return;
    }

    if (self->role != CONDUCTOR_ROLE)
    {
      if (strcmp(self->buffer, "q") == 0 || strcmp(self->buffer, "stop") == 0)
      {
        self->buffer_pos = 0;
        send_conductor_cmd(self, CAN_SUB_STOP, 0);
        SCI_WRITE(&sci0, "\nCAN stop command sent (musician mode).\n");
        print_helper(self);
        return;
      }

      if (strcmp(self->buffer, "p") == 0 || strcmp(self->buffer, "play") == 0)
      {
        self->buffer_pos = 0;
        send_conductor_cmd(self, CAN_SUB_START, 0);
        SCI_WRITE(&sci0, "\nCAN play command sent (musician mode).\n");
        print_helper(self);
        return;
      }

      if (strcmp(self->buffer, "s") == 0 || strcmp(self->buffer, "mute") == 0)
      {
        self->buffer_pos = 0;
        apply_output_mute();
        SCI_WRITE(&sci0, "\nLocal output muted.\n");
        print_helper(self);
        return;
      }

      if (strcmp(self->buffer, "r") == 0 || strcmp(self->buffer, "unmute") == 0)
      {
        self->buffer_pos = 0;
        apply_output_unmute();
        SCI_WRITE(&sci0, "\nLocal output unmuted.\n");
        print_helper(self);
        return;
      }

      if (strcmp(self->buffer, "v") == 0 || strcmp(self->buffer, "volume") == 0)
      {
        self->mode = VOLUME_MODE;
        self->buffer_pos = 0;
        SCI_WRITE(&sci0, "\nInput the volume, end with enter: ");
        return;
      }

      if (strcmp(self->buffer, "t") == 0 || strcmp(self->buffer, "tempo") == 0)
      {
        self->mode = TEMPO_MODE;
        self->buffer_pos = 0;
        SCI_WRITE(&sci0, "\nInput tempo (60-240), end with enter: ");
        return;
      }

      if (strcmp(self->buffer, "k") == 0 || strcmp(self->buffer, "key") == 0)
      {
        self->mode = KEY_MODE;
        self->buffer_pos = 0;
        SCI_WRITE(&sci0, "\nInput key offset (-5 to 5), end with enter: ");
        return;
      }

      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nIn musician mode, keyboard only sends CAN commands (no direct local control).\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "v") == 0 || strcmp(self->buffer, "volume") == 0)
    {
      self->mode = VOLUME_MODE;
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nInput the volume, end with enter: ");
      return;
    }

    if (strcmp(self->buffer, "q") == 0 || strcmp(self->buffer, "stop") == 0)
    {
      apply_stop(self);
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nMelody stopped.\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "t") == 0 || strcmp(self->buffer, "tempo") == 0)
    {
      self->mode = TEMPO_MODE;
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nInput tempo (60-240), end with enter: ");
      return;
    }

    if (strcmp(self->buffer, "k") == 0 || strcmp(self->buffer, "key") == 0)
    {
      self->mode = KEY_MODE;
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nInput key offset (-5 to 5), end with enter: ");
      return;
    }

    if (strcmp(self->buffer, "p") == 0 || strcmp(self->buffer, "play") == 0)
    {
      self->buffer_pos = 0;
      apply_play(self);
      SCI_WRITE(&sci0, "\nPlaying...\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "s") == 0 || strcmp(self->buffer, "mute") == 0)
    {
      self->buffer_pos = 0;
      apply_output_mute();
      SCI_WRITE(&sci0, "\nTone output muted.\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "r") == 0 || strcmp(self->buffer, "unmute") == 0)
    {
      self->buffer_pos = 0;
      apply_output_unmute();
      SCI_WRITE(&sci0, "\nTone output unmuted.\n");
      print_helper(self);
      return;
    }

    self->buffer_pos = 0;
    SCI_WRITE(&sci0, "\nUnknown command. Type 'help' for valid commands.\n");
    print_helper(self);
    return;
  }
  else if (self->buffer_pos < (int)(sizeof(self->buffer) - 1))
  {
    self->buffer[self->buffer_pos++] = c;
    SCI_WRITECHAR(&sci0, c);
  }
}

void reader(App *self, int c)
{
  if (self->mode != CONTROL_MODE)
  {
    parameter_control_handler(self, (char)c);
    return;
  }
  command_handler(self, (char)c);
}

void periodic_print(App *self, int session)
{
  if (session != self->print_session) return;
  if (!self->print_enabled) return;
  char tmp[8];
  if (self->role == CONDUCTOR_ROLE)
  {
    SCI_WRITE(&sci0, "\nTempo: ");
    int_to_string(self->tempo, tmp);
    SCI_WRITE(&sci0, tmp);
    SCI_WRITE(&sci0, " bpm\n");
  }
  else if (output_muted)
  {
    SCI_WRITE(&sci0, "\nMUTED\n");
  }
  SEND(MSEC(PRINT_INTERVAL_S * 1000), MSEC(100), self, periodic_print, session);
}

void startApp(App *self, int arg)
{
  (void)arg;

  CAN_INIT(&can0);
  SCI_INIT(&sci0);

  SYNC(&tone_task, tone_set_period, 1136);
  SYNC(&tone_task, tone_set_mute, 1);

  print_helper(self);

  ASYNC(&tone_task, tone_generator, 1);
}

int main()
{
  reset_program_state();

  INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
  INSTALL(&can0, can_interrupt, CAN_IRQ0);
  TINYTIMBER(&app, startApp, 0);
  return 0;
}
