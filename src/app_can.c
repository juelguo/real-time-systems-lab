/* Encodes, logs, and applies CAN-based player commands. */

#include "App.h"
#include "TinyTimber.h"

static BoardInfo *find_board(App *self, int node_id)
{
  int i;
  for (i = 0; i < BOARD_COUNT; i++)
  {
    if (self->boards[i].node_id == node_id)
    {
      return &self->boards[i];
    }
  }
  return 0;
}

int has_active_conductor(App *self)
{
  BoardInfo *conductor;

  if (self->conductor_id < 1 || self->conductor_id > BOARD_COUNT)
  {
    return 0;
  }

  conductor = find_board(self, self->conductor_id);
  if (conductor == 0)
  {
    return 0;
  }

  return conductor->status == BOARD_STATUS_UP;
}

void set_known_conductor(App *self, int conductor_id)
{
  if (conductor_id < CONDUCTOR_NONE || conductor_id > BOARD_COUNT)
  {
    return;
  }

  self->conductor_id = conductor_id;
  if (conductor_id == self->node_id)
  {
    self->role = CONDUCTOR_ROLE;
    return;
  }

  self->role = MUSICIAN_ROLE;
}

static int should_accept_conductor_id(App *self, int announced_conductor_id)
{
  if (announced_conductor_id < 1 || announced_conductor_id > BOARD_COUNT)
  {
    return 0;
  }

  if (!has_active_conductor(self) || self->conductor_id == CONDUCTOR_NONE)
  {
    return 1;
  }

  if (announced_conductor_id == self->conductor_id)
  {
    return 1;
  }

  /* Dynamic rank follows ascending node id, so lower node id wins ties. */
  // return announced_conductor_id < self->conductor_id;
  return 1;
}

static void refresh_sync_from_heartbeat(App *self, CANMsg *msg)
{
  int advertised_conductor_id;

  if (msg->length < 2)
  {
    return;
  }

  advertised_conductor_id = (int)msg->buff[1];

  /*
   * Only the conductor's own heartbeat is allowed to move conductor ownership.
   * A musician heartbeat may carry stale metadata and must not override authority.
   */
  if (advertised_conductor_id == msg->nodeId && should_accept_conductor_id(self, advertised_conductor_id))
  {
    set_known_conductor(self, advertised_conductor_id);
  }

  if (msg->nodeId != self->conductor_id)
  {
    return;
  }

  if (msg->length > 0)
  {
    self->current_index = ((int)msg->buff[0]) % 32;
  }

  if (msg->length > 2)
  {
    self->tempo = clamp((int)msg->buff[2], MIN_TEMPO, MAX_TEMPO);
  }

  if (msg->length > 3)
  {
    self->key = clamp((int)((int8_t)msg->buff[3]), MIN_KEY, MAX_KEY);
  }
}

void recompute_board_ranks(App *self)
{
  int i;
  int next_rank = 0;

  for (i = 0; i < BOARD_COUNT; i++)
  {
    if (self->boards[i].status == BOARD_STATUS_UP)
    {
      self->boards[i].rank = next_rank;
      next_rank++;
    }
    else
    {
      self->boards[i].rank = BOARD_RANK_UNKNOWN;
    }
  }
}

void mark_board_up(App *self, int node_id)
{
  BoardInfo *board = find_board(self, node_id);
  if (board == 0)
  {
    return;
  }

  board->status = BOARD_STATUS_UP;
  board->last_heartbeat_ms = (int)(CURRENT_OFFSET() / 100);
  recompute_board_ranks(self);
}

void mark_board_down(App *self, int node_id)
{
  int now_ms = (int)(CURRENT_OFFSET() / 100);
  BoardInfo *board = find_board(self, node_id);
  if (board == 0)
  {
    return;
  }

  if (board->status == BOARD_STATUS_DOWN)
  {
    return;
  }

  if ((now_ms - board->last_heartbeat_ms) < BOARD_DOWN_TIMEOUT_MS)
  {
    return;
  }

  board->status = BOARD_STATUS_DOWN;
  board->rank = BOARD_RANK_UNKNOWN;
  if (node_id == self->conductor_id)
  {
    set_known_conductor(self, CONDUCTOR_NONE);
  }
  recompute_board_ranks(self);
}

void periodic_heartbeat(App *self, int unused)
{
  (void)unused;

  send_heartbeat(self);
  SEND(MSEC(HEARTBEAT_PERIOD_MS), MSEC(2), self, periodic_heartbeat, 0);
}

void check_board_timeouts(App *self, int unused)
{
  int i;
  (void)unused;

  for (i = 0; i < BOARD_COUNT; i++)
  {
    if (self->boards[i].node_id == self->node_id)
    {
      continue;
    }

    if (self->boards[i].status == BOARD_STATUS_UP)
    {
      mark_board_down(self, self->boards[i].node_id);
    }
  }

  SEND(MSEC(BOARD_TIMEOUT_CHECK_PERIOD_MS), MSEC(10), self, check_board_timeouts, 0);
}

static void send_can_frame(App *self, CanMessageType msg_id, int length, uchar b0, uchar b1, uchar b2, uchar b3)
{
  CANMsg msg;
  msg.msgId = msg_id;
  msg.nodeId = self->node_id;
  msg.length = length;
  msg.buff[0] = b0;
  msg.buff[1] = b1;
  msg.buff[2] = b2;
  msg.buff[3] = b3;
  // print_can_message("TX", &msg);
  CAN_SEND(&can0, &msg);
}

void print_can_command_name(uchar command)
{
  if (command == CAN_CMD_PLAY)
  {
    SCI_WRITE(&sci0, "PLAY");
    return;
  }
  if (command == CAN_CMD_STOP)
  {
    SCI_WRITE(&sci0, "STOP");
    return;
  }
  if (command == CAN_CMD_SET_TEMPO)
  {
    SCI_WRITE(&sci0, "SET_TEMPO");
    return;
  }
  if (command == CAN_CMD_SET_KEY)
  {
    SCI_WRITE(&sci0, "SET_KEY");
    return;
  }
  if (command == CAN_CMD_SET_VOLUME)
  {
    SCI_WRITE(&sci0, "SET_VOLUME");
    return;
  }
  if (command == CAN_CMD_MUTE_OUTPUT)
  {
    SCI_WRITE(&sci0, "MUTE_OUTPUT");
    return;
  }
  if (command == CAN_CMD_UNMUTE_OUTPUT)
  {
    SCI_WRITE(&sci0, "UNMUTE_OUTPUT");
    return;
  }
  SCI_WRITE(&sci0, "UNKNOWN");
}

void send_can_player_command(App *self, CanCommand command, int value)
{
  if (self->role != CONDUCTOR_ROLE && self->role != MUSICIAN_ROLE)
  {
    return;
  }

  /* Pack command + value into the shared player-control frame format. */
  send_can_frame(self, CAN_MSG_CONDUCTOR_CMD, 2, (uchar)command, (uchar)value, 0, 0);
}

void send_discovery_ping(App *self)
{
  send_can_frame(self, CAN_MSG_DISCOVERY_PING, 0, 0, 0, 0, 0);
}

void send_discovery_reply(App *self)
{
  send_can_frame(self, CAN_MSG_DISCOVERY_REPLY, 1, (uchar)self->node_id, 0, 0, 0);
}

void send_heartbeat(App *self)
{
  send_can_frame(
    self,
    CAN_MSG_HEARTBEAT,
    4,
    (uchar)self->current_index,
    (uchar)self->conductor_id,
    (uchar)self->tempo,
    (uchar)((int8_t)self->key));
}

void send_conductor_announce(App *self)
{
  send_can_frame(self, CAN_MSG_CONDUCTOR_ANNOUNCE, 1, (uchar)self->conductor_id, 0, 0, 0);
}

void send_token(App *self, int note_index)
{
  send_can_frame(self, CAN_MSG_TOKEN, 1, (uchar)note_index, 0, 0, 0);
}

int decode_command_value(uchar command, uchar raw)
{
  if (command == CAN_CMD_SET_KEY)
  {
    return (int)((int8_t)raw);
  }
  return (int)raw;
}

void print_can_message(char *direction, CANMsg *msg)
{
  char value[16];

  SCI_WRITE(&sci0, "\nCAN ");
  SCI_WRITE(&sci0, direction);
  SCI_WRITE(&sci0, " -> msgId=");
  int_to_string(msg->msgId, value);
  SCI_WRITE(&sci0, value);

  SCI_WRITE(&sci0, ", nodeId=");
  int_to_string(msg->nodeId, value);
  SCI_WRITE(&sci0, value);

  SCI_WRITE(&sci0, ", len=");
  int_to_string(msg->length, value);
  SCI_WRITE(&sci0, value);

  if (msg->length > 0)
  {
    SCI_WRITE(&sci0, ", cmd=");
    print_can_command_name(msg->buff[0]);
    SCI_WRITE(&sci0, "(");
    int_to_string(msg->buff[0], value);
    SCI_WRITE(&sci0, value);
    SCI_WRITE(&sci0, ")");
  }

  if (msg->length > 1)
  {
    SCI_WRITE(&sci0, ", value=");
    int_to_string(decode_command_value(msg->buff[0], msg->buff[1]), value);
    SCI_WRITE(&sci0, value);
  }

  SCI_WRITE(&sci0, "\n");
}

// this function is for can message receiver
void receiver(App *self, int unused)
{
  (void)unused;
  CANMsg msg;
  if (CAN_RECEIVE(&can0, &msg) != 0)
  {
    return;
  }

  if (msg.msgId != CAN_MSG_HEARTBEAT)
  {
    print_can_message("RX", &msg);
  }

  if (msg.msgId == CAN_MSG_DISCOVERY_PING)
  {
    mark_board_up(self, msg.nodeId);
    send_discovery_reply(self);
    return;
  }

  if (msg.msgId == CAN_MSG_DISCOVERY_REPLY)
  {
    int discovered_node_id = msg.nodeId;
    if (msg.length > 0)
    {
      discovered_node_id = (int)msg.buff[0];
    }

    mark_board_up(self, discovered_node_id);
    return;
  }

  if (msg.msgId == CAN_MSG_CONDUCTOR_ANNOUNCE && msg.length > 0)
  {
    int announced_conductor_id = (int)msg.buff[0];

    mark_board_up(self, msg.nodeId);
    if (should_accept_conductor_id(self, announced_conductor_id))
    {
      set_known_conductor(self, announced_conductor_id);
    }
    else if (self->role == CONDUCTOR_ROLE)
    {
      send_conductor_announce(self);
    }
    return;
  }

  if (msg.msgId == CAN_MSG_HEARTBEAT)
  {
    mark_board_up(self, msg.nodeId);
    refresh_sync_from_heartbeat(self, &msg);
    return;
  }

  if (msg.msgId == CAN_MSG_TOKEN && msg.length > 0)
  {
    int received_note_index = ((int)msg.buff[0]) % 32;
    int next_note_index = (received_note_index + 1) % 32;

    // synchorize the note index to avoid time drift
    if (self->current_index != received_note_index)
    {
      self->current_index = received_note_index;
    }

    sync_output_for_note(self, next_note_index);
    return;
  }

  if (self->role != MUSICIAN_ROLE)
  {
    /* Only musician boards follow incoming control traffic. */
    return;
  }

  if (msg.msgId != CAN_MSG_CONDUCTOR_CMD || msg.length < 1)
  {
    return;
  }

  /* Key is signed; the remaining settings are encoded as unsigned bytes. */
  int signed_value = (msg.length > 1) ? (int)((int8_t)msg.buff[1]) : 0;
  int unsigned_value = (msg.length > 1) ? (int)msg.buff[1] : 0;

  if (msg.buff[0] == CAN_CMD_PLAY)
  {
    apply_play(self);
    return;
  }

  if (msg.buff[0] == CAN_CMD_STOP)
  {
    apply_stop(self);
    return;
  }

  if (msg.buff[0] == CAN_CMD_SET_TEMPO)
  {
    apply_tempo(self, unsigned_value);
    return;
  }

  if (msg.buff[0] == CAN_CMD_SET_KEY)
  {
    apply_key(self, signed_value);
    return;
  }

  if (msg.buff[0] == CAN_CMD_SET_VOLUME)
  {
    apply_volume(unsigned_value);
    return;
  }

  if (msg.buff[0] == CAN_CMD_MUTE_OUTPUT)
  {
    apply_output_mute();
    return;
  }

  if (msg.buff[0] == CAN_CMD_UNMUTE_OUTPUT)
  {
    apply_output_unmute();
  }
}
