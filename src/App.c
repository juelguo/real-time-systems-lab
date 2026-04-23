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

#define DAC_PORT (*(volatile unsigned char *)0x4000741C)
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
#define CAN_SUB_SET_KEY   CAN_SUB_KEY
#define CAN_SUB_SET_TEMPO CAN_SUB_TEMPO
#define CAN_NODE_BROADCAST 0x0F
#define MELODY_NOTE_COUNT 32
#define TOKEN_NO_FAILED_NODE 0
#define F3_FAIL_THRESHOLD 3
#define PRINT_INTERVAL_S 10
#define HEARTBEAT_LOG_SUMMARY_EVERY 20

// Problem 3/4 failure recovery is enabled.
#define ENABLE_PROBLEM_3 1
#define ENABLE_PROBLEM_4 1

extern App app;
extern ToneTask tone_task;
extern Can can0;
extern Serial sci0;

static int output_muted = 0;
static int volume_before_output_mute = DEFAULT_VOLUME;
static int can_print_enabled = 0;
static int can_heartbeat_print_enabled = 0;
static int suppressed_heartbeat_tx_count = 0;
static int suppressed_heartbeat_rx_count = 0;
static int last_suppressed_heartbeat_tx_node = -1;
static int last_suppressed_heartbeat_rx_node = -1;
static unsigned int rand_state = 12345u;

// internal helper function
void int_to_string(int n, char *buffer);
static char *can_msg_name(int msgId);
static void print_hex_fixed(unsigned int value, int digits);
static void print_dec_hex_field(char *label, unsigned int value, int hex_digits);
static void print_can_buffer(CANMsg *msg);
static void print_can_msg_with_prefix(const char *prefix, CANMsg *msg);
static void print_can_msg_filtered(const char *prefix, CANMsg *msg, int is_tx);
static void print_can_heartbeat_summary(void);
static void cancel_pending_msg(Msg *slot);
static void tone_schedule_next(ToneTask *self, int state);
static void enter_silent_failure(App *self, int mode);

static void cancel_pending_msg(Msg *slot)
{
  if (*slot == NULL) return;
  ABORT(*slot);
  *slot = NULL;
}

static void tone_schedule_next(ToneTask *self, int state)
{
  int period = self->period;
  if (period < 100) period = 100;
  self->tone_msg = SEND(CURRENT_OFFSET() + USEC(period), USEC(100), self, tone_generator, state);
}

#if ENABLE_PROBLEM_3
static unsigned int simple_rand(void)
{
  rand_state = rand_state * 1664525u + 1013904223u;
  return rand_state;
}
#endif

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
  can_heartbeat_print_enabled = 0;
  suppressed_heartbeat_tx_count = 0;
  suppressed_heartbeat_rx_count = 0;
  last_suppressed_heartbeat_tx_node = -1;
  last_suppressed_heartbeat_rx_node = -1;
  rand_state = 12345u;
}

static int is_valid_node_id(int node_id)
{
  return node_id >= 1 && node_id <= 14;
}

#if ENABLE_PROBLEM_3
static int rand_range(int lo, int hi)
{
  return lo + (int)(simple_rand() % (unsigned int)(hi - lo + 1));
}
#endif

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

static int arr_contains(int *arr, int count, int val)
{
  for (int i = 0; i < count; i++)
    if (arr[i] == val) return 1;
  return 0;
}

#if ENABLE_PROBLEM_3 || ENABLE_PROBLEM_4
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
#endif

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
#if ENABLE_PROBLEM_3 || ENABLE_PROBLEM_4
  arr_remove(self->pending_join_nodes, &self->pending_join_count, nid);
#endif
  self->rank = arr_rank_of(self->active_nodes, self->active_count, self->node_id);
}

static void add_known_node(App *self, int nid)
{
  arr_insert_sorted(self->known_nodes, &self->known_count, nid);
}

#if ENABLE_PROBLEM_3
static void stage_join_node(App *self, int nid)
{
  if (!is_valid_node_id(nid)) return;
  add_known_node(self, nid);
  if (arr_contains(self->active_nodes, self->active_count, nid)) return;
  arr_insert_sorted(self->pending_join_nodes, &self->pending_join_count, nid);
}

static void apply_pending_joins(App *self)
{
  if (self->is_silent || self->pending_join_count == 0) return;
  for (int i = 0; i < self->pending_join_count; i++)
    add_active_node(self, self->pending_join_nodes[i]);
  self->pending_join_count = 0;
}
#endif

#if ENABLE_PROBLEM_3 || ENABLE_PROBLEM_4
static void remove_active_node(App *self, int nid)
{
  arr_remove(self->active_nodes, &self->active_count, nid);
  self->rank = arr_rank_of(self->active_nodes, self->active_count, self->node_id);
}

static void mark_node_failed(App *self, int failed_node)
{
  if (!is_valid_node_id(failed_node)) return;
  add_known_node(self, failed_node);
  arr_remove(self->pending_join_nodes, &self->pending_join_count, failed_node);
  remove_active_node(self, failed_node);
}
#endif

#if ENABLE_PROBLEM_3
// this function is designed for CAN message sending failures.
static void handle_can_send_result(App *self, int status)
{
  if (self == NULL || self->is_silent) return;
  if (status == 0)
  {
    self->can_err_count = 0;
    return;
  }

  self->can_err_count++;
  if (self->can_err_count < F3_FAIL_THRESHOLD) return;
  self->can_err_count = 0;

  // if only have two board, the condutor should not stop if the connection is lost
  if (self->role == CONDUCTOR_ROLE && self->active_count <= 2)
  {
    for (int i = self->active_count - 1; i >= 0; i--)
    {
      int nid = self->active_nodes[i];
      if (nid != self->node_id)
        mark_node_failed(self, nid);
    }
    if (!arr_contains(self->active_nodes, self->active_count, self->node_id))
      add_active_node(self, self->node_id);
    if (self->song_active && self->rank == 0 && self->status == 0)
      play_one_note(self, self->last_token_index);
    return;
  }

  enter_silent_failure(self, SILENT_F3);
}
#endif

#if ENABLE_PROBLEM_4
static int lowest_active_nodeId(App *self)
{
  if (self->active_count == 0) return self->node_id;
  return self->active_nodes[0];
}

static void replace_active_nodes_from_announce(App *self, CANMsg *msg, int new_cond)
{
  int old_active_count = self->active_count;
  int old_active_nodes[16];
  for (int i = 0; i < old_active_count; i++)
    old_active_nodes[i] = self->active_nodes[i];

  self->active_count = 0;
  if (is_valid_node_id(new_cond))
    add_active_node(self, new_cond);

  for (int i = 1; i < msg->length; i++)
  {
    int nid = (int)msg->buff[i];
    if (is_valid_node_id(nid))
      add_active_node(self, nid);
  }
  if (!self->is_silent)
    add_active_node(self, self->node_id);

  for (int i = 0; i < old_active_count; i++)
  {
    if (!arr_contains(self->active_nodes, self->active_count, old_active_nodes[i]))
      add_known_node(self, old_active_nodes[i]);
  }
  self->rank = arr_rank_of(self->active_nodes, self->active_count, self->node_id);
}
#endif

static int can_send_raw(App *self, int msgId, int nodeId, uchar *data, int len)
{
  if (self != NULL && self->is_silent) return 1;
  if (len > 8) len = 8;

  CANMsg msg;
  msg.msgId = msgId;
  msg.nodeId = (uchar)nodeId;
  msg.length = (uchar)len;
  for (int i = 0; i < len; i++) msg.buff[i] = data[i];

  int status = CAN_SEND(&can0, &msg);
  if (can_print_enabled && status == 0)
    print_can_msg_filtered("Sent", &msg, 1);
#if ENABLE_PROBLEM_3
  handle_can_send_result(self, status);
#endif
  return status;
}

static void send_discovery_ping(App *self)
{
  can_send_raw(self, CAN_MSG_DISCOVERY_PING, self->node_id, 0, 0);
}

static void send_discovery_reply(App *self, int to_node)
{
  (void)to_node;
  uchar d[1]; d[0] = (uchar)self->node_id;
  can_send_raw(self, CAN_MSG_DISCOVERY_REPLY, self->node_id, d, 1);
}

static void send_conductor_announce(App *self)
{
  uchar d[8];
  int count = self->active_count;
  if (count > 7) count = 7;

  d[0] = (uchar)self->node_id;
  for (int i = 0; i < count; i++)
    d[1+i] = (uchar)self->active_nodes[i];
  can_send_raw(self, CAN_MSG_CONDUCTOR_ANNOUNCE, self->node_id, d, 1 + count);
}

static void send_token_with_failure(App *self, int next_note_index, int failed_node)
{
  if (self->is_silent) return;

  int token_index = next_note_index % MELODY_NOTE_COUNT;
  if (token_index < 0) token_index += MELODY_NOTE_COUNT;
  int token_failed = is_valid_node_id(failed_node) ? failed_node : TOKEN_NO_FAILED_NODE;

#if ENABLE_PROBLEM_3
  if (is_valid_node_id(token_failed))
    mark_node_failed(self, token_failed);
  apply_pending_joins(self);
#endif

  if (self->active_count == 1 && self->rank == 0)
  {
    self->last_token_index = token_index;
    self->last_token_node = self->node_id;
    play_one_note(self, token_index);
    return;
  }

  uchar d[2];
  d[0] = (uchar)token_index;
  d[1] = (uchar)token_failed;
  can_send_raw(self, CAN_MSG_TOKEN, self->node_id, d, 2);

  if (self->active_count > 0 &&
      self->rank >= 0 &&
      (token_index % self->active_count) == self->rank)
  {
    self->last_token_index = token_index;
    self->last_token_node = self->node_id;
    play_one_note(self, token_index);
  }
}

static void send_token(App *self, int next_note_index)
{
  send_token_with_failure(self, next_note_index, TOKEN_NO_FAILED_NODE);
}

#if ENABLE_PROBLEM_3 || ENABLE_PROBLEM_4
static void send_heartbeat_now(App *self)
{
  uchar d[1]; d[0] = (uchar)self->node_id;
  can_send_raw(self, CAN_MSG_HEARTBEAT, self->node_id, d, 1);
}
#endif

static void send_conductor_cmd(App *self, int sub, int val)
{
  if (self->is_silent) return;

  uchar d[3];
  d[0] = (uchar)sub;

  if (sub == CAN_SUB_TEMPO)
  {
    d[1] = (uchar)((val >> 8) & 0xFF);
    d[2] = (uchar)(val & 0xFF);
    can_send_raw(self, CAN_MSG_CONDUCTOR_CMD, self->node_id, d, 3);
    return;
  }

  if (sub == CAN_SUB_START || sub == CAN_SUB_STOP)
  {
    can_send_raw(self, CAN_MSG_CONDUCTOR_CMD, self->node_id, d, 1);
    return;
  }

  d[1] = (uchar)val;
  can_send_raw(self, CAN_MSG_CONDUCTOR_CMD, self->node_id, d, 2);
}

static void send_conductor_discovery_state(App *self)
{
  if (self->role != CONDUCTOR_ROLE) return;

  send_conductor_announce(self);
  send_conductor_cmd(self, CAN_SUB_SET_KEY, self->key);
  send_conductor_cmd(self, CAN_SUB_SET_TEMPO, self->tempo);
  send_conductor_cmd(self, self->song_active ? CAN_SUB_START : CAN_SUB_STOP, 0);
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

static void print_hex_fixed(unsigned int value, int digits)
{
  char out[2];
  out[1] = '\0';
  SCI_WRITE(&sci0, "0x");
  for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
  {
    int nibble = (value >> shift) & 0x0F;
    out[0] = (char)(nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
    SCI_WRITE(&sci0, out);
  }
}

static void print_dec_hex_field(char *label, unsigned int value, int hex_digits)
{
  char tmp[12];
  SCI_WRITE(&sci0, label);
  int_to_string((int)value, tmp);
  SCI_WRITE(&sci0, tmp);
  SCI_WRITE(&sci0, " (");
  print_hex_fixed(value, hex_digits);
  SCI_WRITE(&sci0, ")");
}

static void print_can_buffer(CANMsg *msg)
{
  char tmp[12];
  int len = msg->length;

  SCI_WRITE(&sci0, "[");
  for (int i = 0; i < len && i < 8; i++)
  {
    if (i > 0) SCI_WRITE(&sci0, " ");
    print_hex_fixed(msg->buff[i], 2);
    SCI_WRITE(&sci0, "(");
    int_to_string(msg->buff[i], tmp);
    SCI_WRITE(&sci0, tmp);
    SCI_WRITE(&sci0, ")");
  }
  if (len > 8)
    SCI_WRITE(&sci0, " ...");
  SCI_WRITE(&sci0, "]");
}

static void print_can_msg_with_prefix(const char *prefix, CANMsg *msg)
{
  char tmp[12];
  unsigned int std_id = (((unsigned int)msg->msgId & 0x7F) << 4) |
                        ((unsigned int)msg->nodeId & 0x0F);

  SCI_WRITE(&sci0, "\n");
  SCI_WRITE(&sci0, prefix);
  SCI_WRITE(&sci0, " CAN ");
  SCI_WRITE(&sci0, can_msg_name(msg->msgId));
  SCI_WRITE(&sci0, ": ");
  print_dec_hex_field("msgId=", msg->msgId, 2);
  SCI_WRITE(&sci0, ", ");
  print_dec_hex_field("nodeId=", msg->nodeId, 1);
  SCI_WRITE(&sci0, ", stdId=");
  print_hex_fixed(std_id, 3);
  SCI_WRITE(&sci0, ", bufLen=");
  int_to_string(msg->length, tmp);
  SCI_WRITE(&sci0, tmp);
  SCI_WRITE(&sci0, ", buf=");
  print_can_buffer(msg);
  SCI_WRITE(&sci0, "\n");
}

static void reset_can_heartbeat_summary(void)
{
  suppressed_heartbeat_tx_count = 0;
  suppressed_heartbeat_rx_count = 0;
  last_suppressed_heartbeat_tx_node = -1;
  last_suppressed_heartbeat_rx_node = -1;
}

static void print_can_heartbeat_summary(void)
{
  char tmp[12];
  int total = suppressed_heartbeat_tx_count + suppressed_heartbeat_rx_count;
  if (total <= 0) return;

  SCI_WRITE(&sci0, "\nHeartbeat summary: suppressed ");
  int_to_string(total, tmp);
  SCI_WRITE(&sci0, tmp);
  SCI_WRITE(&sci0, " messages");

  if (suppressed_heartbeat_tx_count > 0)
  {
    SCI_WRITE(&sci0, ", TX=");
    int_to_string(suppressed_heartbeat_tx_count, tmp);
    SCI_WRITE(&sci0, tmp);
    SCI_WRITE(&sci0, " lastTxNode=");
    int_to_string(last_suppressed_heartbeat_tx_node, tmp);
    SCI_WRITE(&sci0, tmp);
  }

  if (suppressed_heartbeat_rx_count > 0)
  {
    SCI_WRITE(&sci0, ", RX=");
    int_to_string(suppressed_heartbeat_rx_count, tmp);
    SCI_WRITE(&sci0, tmp);
    SCI_WRITE(&sci0, " lastRxNode=");
    int_to_string(last_suppressed_heartbeat_rx_node, tmp);
    SCI_WRITE(&sci0, tmp);
  }

  SCI_WRITE(&sci0, "\n");
  reset_can_heartbeat_summary();
}

static void record_suppressed_heartbeat(CANMsg *msg, int is_tx)
{
  if (is_tx)
  {
    suppressed_heartbeat_tx_count++;
    last_suppressed_heartbeat_tx_node = msg->nodeId;
  }
  else
  {
    suppressed_heartbeat_rx_count++;
    last_suppressed_heartbeat_rx_node = msg->nodeId;
  }

  if (suppressed_heartbeat_tx_count + suppressed_heartbeat_rx_count >= HEARTBEAT_LOG_SUMMARY_EVERY)
    print_can_heartbeat_summary();
}

static void print_can_msg_filtered(const char *prefix, CANMsg *msg, int is_tx)
{
  if (msg->msgId == CAN_MSG_HEARTBEAT && !can_heartbeat_print_enabled)
  {
    record_suppressed_heartbeat(msg, is_tx);
    return;
  }

  print_can_msg_with_prefix(prefix, msg);
}

static void print_can_msg(CANMsg *msg)
{
  print_can_msg_filtered("Received", msg, 0);
}

// forward declarations
static void become_conductor(App *self, int reason);
static void become_musician(App *self, int reason);
static void do_discovery(App *self);
#if ENABLE_PROBLEM_3
static void reset_watchdog(App *self);
#endif
#if ENABLE_PROBLEM_4
static void reset_conductor_wd(App *self);
#endif

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
int tone_set_mute(ToneTask *self, int value)
{
  self->mute = (value != 0);
  if (self->mute)
  {
    cancel_pending_msg(&self->tone_msg);
    DAC_PORT = 0;
    return 0;
  }

  if (self->tone_msg == NULL)
    self->tone_msg = SEND(CURRENT_OFFSET(), USEC(100), self, tone_generator, 1);
  return 0;
}

int tone_set_volume(ToneTask *self, int value)
{
  self->val = value;
  return self->val;
}

int tone_set_period(ToneTask *self, int value)
{
  self->period = value;
  return self->period;
}

int tone_get_volume(ToneTask *self, int unused)
{
  (void)unused;
  return self->val;
}

int tone_get_mute(ToneTask *self, int unused)
{
  (void)unused;
  return self->mute;
}

int tone_generator(ToneTask *self, int state)
{
  self->tone_msg = NULL;

  if (self->mute == 1)
  {
    DAC_PORT = 0;
    return 0;
  }

  if (state == 1)
  {
    DAC_PORT = tone_get_volume(self, 0);
  }
  else
  {
    DAC_PORT = 0;
  }

  tone_schedule_next(self, state ? 0 : 1);
  return 0;
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

static void stop_local_playback(App *self)
{
  self->play_session++;
  self->song_active = 0;
  self->mute = 1;
  self->status = 0;
  self->pending_failed_node = -1;
  self->start_after_discovery = 0;

#if ENABLE_PROBLEM_3
  self->note_hb_session++;
  self->watchdog_session++;
  cancel_pending_msg(&self->watchdog_msg);
#endif

  self->cond_wd_session++;
  cancel_pending_msg(&self->cond_wd_msg);
  SYNC(&tone_task, tone_set_mute, 1);
}

void apply_play(App *self)
{
  self->play_session++;
  self->song_active = 1;
  self->status = 1;
  SYNC(&tone_task, tone_set_mute, 1);
  send_conductor_cmd(self, CAN_SUB_START, 0);
  self->start_after_discovery = 1;
  do_discovery(self);
}

void apply_stop(App *self)
{
  stop_local_playback(self);
  send_conductor_cmd(self, CAN_SUB_STOP, 0);
}

void play_one_note(App *self, int note_index)
{
  if (self->is_silent) return;
  if (self->active_count == 0) return;
  // only play if it's our turn
  if ((note_index % self->active_count) != self->rank) return;

  int session = ++self->play_session;
  self->song_active = 1;
  // self->current_index = note_index & 31;
  self->current_index = note_index % MELODY_NOTE_COUNT;
  self->status = 1;

  int period = get_period(self->current_index, self->key);
  int length = beat_lengths[self->current_index] * (30000 / self->tempo);
  int active_time = length - 50;
  if (active_time < 10) active_time = 10;

  // Without P4 conductor heartbeats, the conductor is legitimately idle while we play.
#if !ENABLE_PROBLEM_4
  {
    int session_cwd = ++self->cond_wd_session;
    cancel_pending_msg(&self->cond_wd_msg);
    self->cond_wd_msg = SEND(MSEC(length + 500), MSEC(20), self, conductor_wd_fire, session_cwd);
  }
#endif

  SYNC(&tone_task, tone_set_period, period);
  SYNC(&tone_task, tone_set_mute, 0);

#if ENABLE_PROBLEM_3
  // start heartbeat loop while playing
  self->note_hb_session++;
  SEND(MSEC(100), MSEC(2), self, send_note_hb, self->note_hb_session);
#endif

  SEND(MSEC(active_time), MSEC(2), self, finish_note, session);
}

void finish_note(App *self, int session)
{
  if (session != self->play_session)
  {
    if (can_print_enabled)
      SCI_WRITE(&sci0, "\nIgnored stale finish_note.\n");
    return;
  }

#if ENABLE_PROBLEM_3
  // stop heartbeat
  self->note_hb_session++;
#endif

  SYNC(&tone_task, tone_set_mute, 1);
  self->status = 0;
  if (!self->song_active)
  {
    self->pending_failed_node = -1;
    return;
  }

  int next_abs = (self->last_token_index + 1) % MELODY_NOTE_COUNT;
  int failed_node = self->pending_failed_node;
  self->pending_failed_node = -1;
  self->last_token_index = next_abs;

  // send next token to all
  send_token_with_failure(self, next_abs, failed_node);
}

void send_note_hb(App *self, int session)
{
#if ENABLE_PROBLEM_3
  if (session != self->note_hb_session) return;
  if (self->is_silent) return;
  if (self->active_count > 1)
    send_heartbeat_now(self);
  SEND(MSEC(100), MSEC(2), self, send_note_hb, session);
#else
  (void)self;
  (void)session;
#endif
}

void send_cond_hb(App *self, int session)
{
#if ENABLE_PROBLEM_4
  (void)self;
  (void)session;
#else
  (void)self;
  (void)session;
#endif
}

void watchdog_fire(App *self, int session)
{
#if ENABLE_PROBLEM_3
  if (session != self->watchdog_session) return;
  self->watchdog_msg = NULL;
  if (self->is_silent || self->active_count <= 0) return;

  int failed_rank = self->last_token_index % self->active_count;
  int failed_node = self->active_nodes[failed_rank];
  if (failed_node == self->node_id) return;

  mark_node_failed(self, failed_node);

  if (self->active_count > 0 &&
      self->rank >= 0 &&
      (self->last_token_index % self->active_count) == self->rank)
  {
    if (self->pending_failed_node < 0)
      self->pending_failed_node = failed_node;
    play_one_note(self, self->last_token_index);
  }
  else if (self->active_count > 0 && self->rank >= 0)
  {
    if (self->pending_failed_node < 0)
      self->pending_failed_node = failed_node;
    reset_watchdog(self);
  }
#else
  (void)self;
  (void)session;
#endif
}

void conductor_wd_fire(App *self, int session)
{
#if ENABLE_PROBLEM_4
  if (session != self->cond_wd_session) return;
  self->cond_wd_msg = NULL;
  if (self->is_silent || self->role == CONDUCTOR_ROLE) return;
  if (!is_valid_node_id(self->conductor_id)) return;

  int failed_conductor = self->conductor_id;
  self->conductor_id = -1;
  mark_node_failed(self, failed_conductor);

  if (!arr_contains(self->active_nodes, self->active_count, self->node_id))
    add_active_node(self, self->node_id);

  if (lowest_active_nodeId(self) == self->node_id)
  {
    become_conductor(self, COND_REASON_AUTO);
    send_conductor_announce(self);
    if (self->song_active &&
        self->status == 0 &&
        self->active_count > 0 &&
        self->rank >= 0 &&
        (self->last_token_index % self->active_count) == self->rank)
    {
      play_one_note(self, self->last_token_index);
    }
  }
#else
  (void)self;
  (void)session;
#endif
}

#if ENABLE_PROBLEM_3
static void reset_watchdog(App *self)
{
  if (self->is_silent || self->rank < 0) return;
  int session = ++self->watchdog_session;
  int delay = 200 + self->rank * 200;
  cancel_pending_msg(&self->watchdog_msg);
  self->watchdog_msg = SEND(MSEC(delay), MSEC(10), self, watchdog_fire, session);
}
#endif

#if ENABLE_PROBLEM_4
static void reset_conductor_wd(App *self)
{
  if (self->is_silent) return;
  if (self->role == CONDUCTOR_ROLE) return;
  if (!is_valid_node_id(self->conductor_id)) return;
  if (!arr_contains(self->active_nodes, self->active_count, self->node_id)) return;
  if (!arr_contains(self->active_nodes, self->active_count, self->conductor_id)) return;

  int session = ++self->cond_wd_session;
  cancel_pending_msg(&self->cond_wd_msg);
  self->cond_wd_msg = SEND(MSEC(500), MSEC(20), self, conductor_wd_fire, session);
}
#endif

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
  if (self->role == CONDUCTOR_ROLE)
    send_conductor_announce(self);

  if (self->start_after_discovery)
  {
    self->start_after_discovery = 0;
    if (self->role == CONDUCTOR_ROLE && self->song_active)
      send_token(self, 0);
  }
#if ENABLE_PROBLEM_4
  if (self->rejoin_after_silent &&
      self->active_count <= 1 &&
      (self->conductor_id < 0 ||
       !arr_contains(self->active_nodes, self->active_count, self->conductor_id)))
  {
    self->rejoin_after_silent = 0;
    become_conductor(self, COND_REASON_DEAD);
    send_conductor_announce(self);
    self->song_active = 1;
    self->last_token_index = 0;
    send_token(self, 0);
  }
  else
  {
    self->rejoin_after_silent = 0;
  }
#endif
}

static void enter_silent_failure(App *self, int mode)
{
#if ENABLE_PROBLEM_3
  if (self->is_silent) return;
  self->is_silent = 1;
  self->silent_mode = mode;
  self->was_conductor = (self->role == CONDUCTOR_ROLE);
  if (self->was_conductor)
    self->conductor_id = -1;
  // stop all timers
  self->note_hb_session++;
  self->cond_hb_session++;
  self->watchdog_session++;
  self->cond_wd_session++;
  self->pending_failed_node = -1;
  cancel_pending_msg(&self->watchdog_msg);
  cancel_pending_msg(&self->cond_wd_msg);
  self->play_session++;
  self->song_active = 0;
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
#else
  (void)self;
  (void)mode;
  SCI_WRITE(&sci0, "\nProblem 3 silent failure mode is disabled.\n");
#endif
}

static void leave_silent_failure(App *self)
{
#if ENABLE_PROBLEM_3
  if (!self->is_silent) return;
  self->is_silent = 0;
  self->can_err_count = 0;
  self->silent_session++;
  self->pending_failed_node = -1;
  SCI_WRITE(&sci0, "\nLeave Silent Failure\n");
  // if we were conductor, rejoin as musician (P4 requirement)
  if (self->was_conductor)
  {
    self->was_conductor = 0;
    self->role = MUSICIAN_ROLE;
    SCI_WRITE(&sci0, "\nI Now Join As A Musician\n");
  }
  self->rejoin_after_silent = 1;
  // re-run discovery; if network is dead (no other active nodes),
  // the node becomes conductor after the 500ms window (handled below)
  self->active_count = 0;
  do_discovery(self);
#else
  (void)self;
  SCI_WRITE(&sci0, "\nProblem 3 silent failure mode is disabled.\n");
#endif
}


void f2_auto_exit(App *self, int session)
{
#if ENABLE_PROBLEM_3
  if (session != self->silent_session) return;
  leave_silent_failure(self);
#else
  (void)self;
  (void)session;
#endif
}

static void become_conductor(App *self, int reason)
{
  self->role = CONDUCTOR_ROLE;
  self->conductor_id = self->node_id;
#if ENABLE_PROBLEM_4
  // Heartbeats are only sent by send_note_hb() while this node is playing a note.
  self->cond_hb_session++;
#endif
  // cancel musician watchdog
  self->watchdog_session++;
  self->cond_wd_session++;
  cancel_pending_msg(&self->watchdog_msg);
  cancel_pending_msg(&self->cond_wd_msg);

  if (reason == COND_REASON_MANUAL)
    SCI_WRITE(&sci0, "\nClaimed Conductorship\n");
  else if (reason == COND_REASON_AUTO || reason == COND_REASON_DEAD)
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
#if ENABLE_PROBLEM_4
  reset_conductor_wd(self);
#endif
}

static void claim_conductorship(App *self)
{
  become_conductor(self, COND_REASON_MANUAL);
  send_conductor_announce(self);
}

static void handle_discovery_ping(App *self, CANMsg *msg)
{
  int sender = msg->nodeId;
  if (!is_valid_node_id(sender)) return;
  int was_active = arr_contains(self->active_nodes, self->active_count, sender);

  add_known_node(self, sender);
  if (!was_active)
  {
#if ENABLE_PROBLEM_3
    if (self->song_active)
      stage_join_node(self, sender);
    else
      add_active_node(self, sender);
#else
    add_active_node(self, sender);
#endif
  }

#if ENABLE_PROBLEM_3
  // Always answer discovery while not silent so a fresh conductor scan keeps us active.
  if (!self->is_silent)
    send_discovery_reply(self, sender);
#else
  send_discovery_reply(self, sender);
#endif

  send_conductor_discovery_state(self);
}

static void handle_discovery_reply(App *self, CANMsg *msg)
{
  int sender = msg->nodeId;

  // Keep compatibility with older reply frames that encoded the sender only in buff[0].
  if (msg->length >= 1 && is_valid_node_id((int)msg->buff[0]))
    sender = msg->buff[0];
  if (!is_valid_node_id(sender)) return;

#if ENABLE_PROBLEM_3
  if (self->song_active)
    stage_join_node(self, sender);
  else
    add_active_node(self, sender);
#else
  add_active_node(self, sender);
#endif
  add_known_node(self, sender);
}

static void handle_conductor_announce(App *self, CANMsg *msg)
{
  int new_cond = msg->nodeId;

  if (msg->length >= 1 && is_valid_node_id((int)msg->buff[0]))
    new_cond = msg->buff[0];
  if (!is_valid_node_id(new_cond)) return;

  add_known_node(self, new_cond);

  if (msg->length > 1)
  {
#if ENABLE_PROBLEM_4
    replace_active_nodes_from_announce(self, msg, new_cond);
#else
    add_active_node(self, new_cond);
    for (int i = 0; i < msg->length; i++)
    {
      if (is_valid_node_id((int)msg->buff[i]))
      {
        add_active_node(self, msg->buff[i]);
        add_known_node(self, msg->buff[i]);
      }
    }
#if ENABLE_PROBLEM_3
    if (!self->is_silent)
      add_active_node(self, self->node_id);
#else
    add_active_node(self, self->node_id);
#endif
#endif
  }
  else
  {
    add_active_node(self, new_cond);
    for (int i = 0; i < msg->length; i++)
    {
      if (is_valid_node_id((int)msg->buff[i]))
      {
        add_active_node(self, msg->buff[i]);
        add_known_node(self, msg->buff[i]);
      }
    }
#if ENABLE_PROBLEM_3
    if (!self->is_silent)
      add_active_node(self, self->node_id);
#else
    add_active_node(self, self->node_id);
#endif
  }
  self->rank = arr_rank_of(self->active_nodes, self->active_count, self->node_id);
  self->conductor_id = new_cond;
  self->discovery_done = 1;
  if (new_cond == self->node_id)
  {
    if (self->role != CONDUCTOR_ROLE)
      become_conductor(self, COND_REASON_MANUAL);
  }
  else
  {
    become_musician(self, MUSICIAN_REASON_DISPLACED);
  }
}

static void handle_token(App *self, CANMsg *msg)
{
  if (msg->length < 1) return;
  int idx = (int)msg->buff[0];
  int failed_node = -1;
#if ENABLE_PROBLEM_3
  if (msg->length >= 2 && is_valid_node_id((int)msg->buff[1]))
    failed_node = (int)msg->buff[1];
#endif
  if (!self->song_active)
  {
    if (idx != 0)
    {
      if (can_print_enabled)
        SCI_WRITE(&sci0, "\nIgnored TOKEN while stopped.\n");
      return;
    }
    self->song_active = 1;
  }
  self->last_token_index = idx;
  self->last_token_node = msg->nodeId;
#if ENABLE_PROBLEM_3
  if (failed_node >= 0)
    mark_node_failed(self, failed_node);
  apply_pending_joins(self);
  // reset our watchdog
  reset_watchdog(self);
#endif
#if ENABLE_PROBLEM_4
  if (msg->nodeId == self->conductor_id)
    reset_conductor_wd(self);
#endif
  // play if it's our turn
  if (!self->is_silent && self->active_count > 0 && (idx % self->active_count) == self->rank)
    play_one_note(self, idx);
}

#if ENABLE_PROBLEM_3 || ENABLE_PROBLEM_4
static void handle_heartbeat(App *self, CANMsg *msg)
{
  int sender = msg->nodeId;
  if (!is_valid_node_id(sender)) return;
#if ENABLE_PROBLEM_4
  if (sender == self->conductor_id)
    reset_conductor_wd(self);
#endif
#if ENABLE_PROBLEM_3
  if (self->song_active && self->active_count > 0 &&
      sender == self->active_nodes[self->last_token_index % self->active_count])
    reset_watchdog(self);
#endif
}
#endif

static void handle_conductor_cmd(App *self, CANMsg *msg)
{
  if (msg->length < 1) return;
  if (self->conductor_id < 0 || msg->nodeId != self->conductor_id)
    return;

  int sub = msg->buff[0];
  int val = (msg->length > 1) ? (int)msg->buff[1] : 0;
  if (sub == CAN_SUB_START)
  {
    if (self->song_active)
    {
      if (can_print_enabled)
        SCI_WRITE(&sci0, "\nIgnored START while already playing.\n");
      return;
    }

    self->play_session++;
    self->song_active = 1;
    self->status = 1;
    SYNC(&tone_task, tone_set_mute, 1);
  }
  else if (sub == CAN_SUB_STOP)
  {
    stop_local_playback(self);
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
#if ENABLE_PROBLEM_4
  reset_conductor_wd(self);
#endif
}

void receiver(App *self, int unused)
{
  (void)unused;
  CANMsg msg;
  if (CAN_RECEIVE(&can0, &msg) != 0)
    return;
  self->can_err_count = 0;
  if (self->is_silent && self->silent_mode == SILENT_F3)
  {
    leave_silent_failure(self);
    return;
  }
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
#if ENABLE_PROBLEM_3 || ENABLE_PROBLEM_4
  else if (msg.msgId == CAN_MSG_HEARTBEAT)
    handle_heartbeat(self, &msg);
#endif
  else if (msg.msgId == CAN_MSG_CONDUCTOR_CMD)
    handle_conductor_cmd(self, &msg);
}

// print the helper
void print_helper(App *self)
{
  char tmp[12];

  SCI_WRITE(&sci0, "\n=== Brother John ===\n");
  if (self->role == CONDUCTOR_ROLE)
  {
    SCI_WRITE(&sci0, "Role: CONDUCTOR (local + CAN)\n");
  }
  else
  {
    SCI_WRITE(&sci0, "Role: MUSICIAN (CAN controlled)\n");
  }
  SCI_WRITE(&sci0, "Node: ");
  if (self->node_id < 0)
  {
    SCI_WRITE(&sci0, "UNSET (node <id> starts discovery)\n");
  }
  else
  {
    int_to_string(self->node_id, tmp);
    SCI_WRITE(&sci0, tmp);
    SCI_WRITE(&sci0, "\n");
  }
  SCI_WRITE(&sci0, "Commands:\n");
  // SCI_WRITE(&sci0, "  h/help\n");
  SCI_WRITE(&sci0, "  p/play, q/stop, s/mute, r/unmute\n");
  SCI_WRITE(&sci0, "  t/tempo 60-240, k/key -5..5, v/volume 0-20\n");
  SCI_WRITE(&sci0, "  node <id> 1-14, claim, R reset key+tempo\n");
  SCI_WRITE(&sci0, "  m/member, I info, T audio mute\n");
  SCI_WRITE(&sci0, "  P periodic print, L/canlog, H/canloghb\n");
#if ENABLE_PROBLEM_3
  SCI_WRITE(&sci0, "  f1 silent, f2 recover 5-10s, f3 CAN off, z leave silent\n");
#else
  SCI_WRITE(&sci0, "  Problem 3/4 silent-failure recovery disabled\n");
#endif
  SCI_WRITE(&sci0, "Settings input: number+Enter, e cancel.\n");
  SCI_WRITE(&sci0, "Choice: ");
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
      {
        if (can_heartbeat_print_enabled)
          SCI_WRITE(&sci0, "\nCAN RX/TX message printing enabled (including heartbeat details).\n");
        else
          SCI_WRITE(&sci0, "\nCAN RX/TX message printing enabled (heartbeats summarized; use H for details).\n");
      }
      else
      {
        print_can_heartbeat_summary();
        SCI_WRITE(&sci0, "\nCAN RX/TX message printing disabled.\n");
      }
      return;
    }

    if (strcmp(self->buffer, "H") == 0 || strcmp(self->buffer, "canloghb") == 0)
    {
      self->buffer_pos = 0;
      can_heartbeat_print_enabled = !can_heartbeat_print_enabled;
      if (can_heartbeat_print_enabled)
      {
        print_can_heartbeat_summary();
        SCI_WRITE(&sci0, "\nCAN heartbeat detail enabled (effective while canlog is on).\n");
      }
      else
      {
        SCI_WRITE(&sci0, "\nCAN heartbeat detail disabled; heartbeats will be summarized.\n");
      }
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
        SCI_WRITE(&sci0, "\nInvalid node id. Use 'node <id>' with 1-14.\n");
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
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nDirect conductor command is disabled. Use 'claim'.\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "u") == 0 || strcmp(self->buffer, "musician") == 0)
    {
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nDirect musician command is disabled. Conductorship changes are handled by announce messages.\n");
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
#if ENABLE_PROBLEM_3
        else if (arr_contains(self->pending_join_nodes, self->pending_join_count, nid))
          SCI_WRITE(&sci0, " [joining]");
#endif
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
      char tmp[12];
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
      SCI_WRITE(&sci0, "  Active: ");
      int_to_string(self->active_count, tmp);
      SCI_WRITE(&sci0, tmp);
      SCI_WRITE(&sci0, "  Song: ");
      if (self->song_active)
        SCI_WRITE(&sci0, "ON");
      else
        SCI_WRITE(&sci0, "OFF");
      SCI_WRITE(&sci0, "\nLast token: ");
      int_to_string(self->last_token_index, tmp);
      SCI_WRITE(&sci0, tmp);
      SCI_WRITE(&sci0, "  Audio: ");
      if (output_muted)
        SCI_WRITE(&sci0, "volume-muted");
      else if (SYNC(&tone_task, tone_get_mute, 0))
        SCI_WRITE(&sci0, "note-muted");
      else
        SCI_WRITE(&sci0, "on");
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
  if (self->print_enabled)
    SEND(MSEC(PRINT_INTERVAL_S * 1000), MSEC(100), self, periodic_print, self->print_session);

  // The oscillator is started by tone_set_mute(0) when a note begins.
}

int main()
{
  reset_program_state();

  INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
  INSTALL(&can0, can_interrupt, CAN_IRQ0);
  TINYTIMBER(&app, startApp, 0);
  return 0;
}
