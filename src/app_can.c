/* Encodes, logs, and applies CAN-based player commands. */

#include "App.h"
#include "TinyTimber.h"

#define CAN_MSG_PLAYER_CONTROL 1
#define CAN_NODE_BROADCAST 0x0F

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
  CANMsg msg;
  msg.msgId = CAN_MSG_PLAYER_CONTROL;
  msg.nodeId = CAN_NODE_BROADCAST;
  msg.length = 2;
  msg.buff[0] = (uchar)command;
  msg.buff[1] = (uchar)value;
  // print_can_message("TX", &msg);
  CAN_SEND(&can0, &msg);
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

// this function id for can message receiver
void receiver(App *self, int unused)
{
  (void)unused;
  CANMsg msg;
  if (CAN_RECEIVE(&can0, &msg) != 0)
  {
    return;
  }

  print_can_message("RX", &msg);

  if (self->role != MUSICIAN_ROLE)
  {
    /* Only musician boards follow incoming control traffic. */
    return;
  }

  if (msg.msgId != CAN_MSG_PLAYER_CONTROL || msg.length < 1)
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
