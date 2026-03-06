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
#define DEFAULT_UNMUTE_VOLUME 15

#define CAN_MSG_PLAYER_CONTROL 1
#define CAN_NODE_BROADCAST 0x0F

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

extern App app;
extern ToneTask tone_task;
extern Can can0;
extern Serial sci0;

// internal helper function
void int_to_string(int n, char *buffer);
void print_can_message(char *direction, CANMsg *msg);

int clamp(int value, int min, int max)
{
  if (value < min)
    return min;
  if (value > max)
    return max;
  return value;
}

void apply_play(App *self)
{
  self->current_index = 0;
  self->mute = 0;
  if (self->status == 0)
  {
    self->status = 1;
    ASYNC(self, play_note, 0);
  }
}

void apply_stop(App *self)
{
  self->mute = 1;
  self->status = 0;
  SYNC(&tone_task, tone_set_mute, 1);
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
  SYNC(&tone_task, tone_set_volume, clamped);
  return clamped;
}

void apply_output_mute(void)
{
  SYNC(&tone_task, tone_set_volume, 0);
}

void apply_output_unmute(void)
{
  SYNC(&tone_task, tone_set_volume, DEFAULT_UNMUTE_VOLUME);
}

void send_can_player_command(App *self, CanCommand command, int value)
{
  if (self->role != CONDUCTOR_ROLE && self->role != MUSICIAN_ROLE)
  {
    return;
  }

  CANMsg msg;
  msg.msgId = CAN_MSG_PLAYER_CONTROL;
  msg.nodeId = CAN_NODE_BROADCAST;
  msg.length = 2;
  msg.buff[0] = (uchar)command;
  msg.buff[1] = (uchar)value;
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

// print the helper
void print_helper(App *self)
{
  SCI_WRITE(&sci0, "\n=== Brother John Music Player ===\n");
  if (self->role == CONDUCTOR_ROLE)
  {
    SCI_WRITE(&sci0, "Board role: CONDUCTOR (keyboard controls local + CAN broadcast)\n");
  }
  else
  {
    SCI_WRITE(&sci0, "Board role: MUSICIAN (controlled by incoming CAN commands)\n");
  }
  SCI_WRITE(&sci0, "Enter a command and press Enter.\n");

  SCI_WRITE(&sci0, "\nRole Commands:\n");
  SCI_WRITE(&sci0, "  c | conductor         Switch to conductor mode\n");
  SCI_WRITE(&sci0, "  m | musician          Switch to musician mode\n");

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

  SCI_WRITE(&sci0, "\nIn Settings Mode (tempo/key/volume):\n");
  SCI_WRITE(&sci0, "  Type a number and press Enter\n");
  SCI_WRITE(&sci0, "  e                     Cancel and return to main menu\n");

  SCI_WRITE(&sci0, "\nChoice: ");
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

int tone_get_volume(ToneTask *self)
{
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
    DAC_PORT = tone_get_volume(self);
  }
  else
  {
    DAC_PORT = 0;
  }

  SEND(USEC(self->period), USEC(100), self, tone_generator, next_state);
}

void play_note(App *self, int unused)
{
  if (self->mute == 1)
  {
    return;
  }

  int current_period = get_period(self->current_index, self->key);
  int length = beat_lengths[self->current_index] * (30000 / self->tempo);

  int active_play_time = length - 50;

  if (active_play_time < 10)
  {
    active_play_time = 10;
  }

  SYNC(&tone_task, tone_set_period, current_period);
  SYNC(&tone_task, tone_set_mute, 0);

  SEND(MSEC(active_play_time), MSEC(2), self, stop_note, 0);
}

void stop_note(App *self, int unused)
{
  if (self->mute == 1)
  {
    return;
  }

  SYNC(&tone_task, tone_set_mute, 1);

  self->current_index = (self->current_index + 1) % 32;

  SEND(MSEC(50), MSEC(2), self, play_note, 0);
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
    return;
  }

  if (msg.msgId != CAN_MSG_PLAYER_CONTROL || msg.length < 1)
  {
    return;
  }

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
      if (self->role == CONDUCTOR_ROLE)
      {
        current_val = apply_volume(current_val);
      }
      send_can_player_command(self, CAN_CMD_SET_VOLUME, current_val);
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
      send_can_player_command(self, CAN_CMD_SET_TEMPO, tempo);
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
      send_can_player_command(self, CAN_CMD_SET_KEY, key);
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
    // parse to lower case (if needed?)
    for (int i = 0; self->buffer[i] != '\0'; i++)
    {
      if (self->buffer[i] >= 'A' && self->buffer[i] <= 'Z')
      {
        self->buffer[i] = self->buffer[i] - 'A' + 'a';
      }
    }

    // if we reached the begining of buffer
    if (self->buffer_pos == 0)
    {
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

    if (strcmp(self->buffer, "m") == 0 || strcmp(self->buffer, "musician") == 0)
    {
      self->role = MUSICIAN_ROLE;
      self->mode = CONTROL_MODE;
      self->buffer_pos = 0;
      apply_stop(self);
      SCI_WRITE(&sci0, "\nSwitched to MUSICIAN mode.\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "h") == 0 || strcmp(self->buffer, "help") == 0)
    {
      self->buffer_pos = 0;
      print_helper(self);
      return;
    }

    if (self->role != CONDUCTOR_ROLE)
    {
      if (strcmp(self->buffer, "q") == 0 || strcmp(self->buffer, "stop") == 0)
      {
        self->buffer_pos = 0;
        send_can_player_command(self, CAN_CMD_STOP, 0);
        SCI_WRITE(&sci0, "\nCAN stop command sent (musician mode).\n");
        print_helper(self);
        return;
      }

      if (strcmp(self->buffer, "p") == 0 || strcmp(self->buffer, "play") == 0)
      {
        self->buffer_pos = 0;
        send_can_player_command(self, CAN_CMD_PLAY, 0);
        SCI_WRITE(&sci0, "\nCAN play command sent (musician mode).\n");
        print_helper(self);
        return;
      }

      if (strcmp(self->buffer, "s") == 0 || strcmp(self->buffer, "mute") == 0)
      {
        self->buffer_pos = 0;
        send_can_player_command(self, CAN_CMD_MUTE_OUTPUT, 0);
        SCI_WRITE(&sci0, "\nCAN mute-output command sent (musician mode).\n");
        print_helper(self);
        return;
      }

      if (strcmp(self->buffer, "r") == 0 || strcmp(self->buffer, "unmute") == 0)
      {
        self->buffer_pos = 0;
        send_can_player_command(self, CAN_CMD_UNMUTE_OUTPUT, 0);
        SCI_WRITE(&sci0, "\nCAN unmute-output command sent (musician mode).\n");
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
      send_can_player_command(self, CAN_CMD_STOP, 0);
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
      send_can_player_command(self, CAN_CMD_PLAY, 0);
      SCI_WRITE(&sci0, "\nPlaying...\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "s") == 0 || strcmp(self->buffer, "mute") == 0)
    {
      self->buffer_pos = 0;
      apply_output_mute();
      send_can_player_command(self, CAN_CMD_MUTE_OUTPUT, 0);
      SCI_WRITE(&sci0, "\nTone output muted.\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "r") == 0 || strcmp(self->buffer, "unmute") == 0)
    {
      self->buffer_pos = 0;
      apply_output_unmute();
      send_can_player_command(self, CAN_CMD_UNMUTE_OUTPUT, 0);
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
  INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
  INSTALL(&can0, can_interrupt, CAN_IRQ0);
  TINYTIMBER(&app, startApp, 0);
  return 0;
}
