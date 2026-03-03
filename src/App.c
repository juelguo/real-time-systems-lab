#include "App.h"
#include "TinyTimber.h"
#include "canTinyTimber.h"
#include "sciTinyTimber.h"

// custom incl
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

extern App app;
extern ToneTask tone_task;
extern Can can0;
extern Serial sci0;

// internal helper function

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
  (void)self;
  SCI_WRITE(&sci0, "\n=== Brother John Music Player ===\n");
  SCI_WRITE(&sci0, "Enter a command and press Enter.\n");

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

void receiver(App *self, int unused)
{
  CANMsg msg;
  CAN_RECEIVE(&can0, &msg);
  SCI_WRITE(&sci0, "Can msg received: ");
  SCI_WRITE(&sci0, msg.buff);
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
      SYNC(&tone_task, tone_set_volume, value);
      int current_val = SYNC(&tone_task, tone_get_volume, 0);
      int_to_string(current_val, out_buf);
      SCI_WRITE(&sci0, "\nVolume set to: ");
    }
    else if (self->mode == TEMPO_MODE)
    {
      if (value < 60)
        value = 60;
      if (value > 240)
        value = 240;
      self->tempo = value;
      int_to_string(self->tempo, out_buf);
      SCI_WRITE(&sci0, "\nTempo set to: ");
    }
    else if (self->mode == KEY_MODE)
    {
      if (value < -5)
        value = -5;
      if (value > 5)
        value = 5;
      self->key = value;
      int_to_string(self->key, out_buf);
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

    if (strcmp(self->buffer, "v") == 0 || strcmp(self->buffer, "volume") == 0)
    {
      self->mode = VOLUME_MODE;
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nInput the volume, end with enter: ");
      return;
    }

    if (strcmp(self->buffer, "q") == 0 || strcmp(self->buffer, "stop") == 0)
    {
      self->mute = 1;
      SYNC(&tone_task, tone_set_mute, 1);
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

    if (strcmp(self->buffer, "p") == 0)
    {
      self->buffer_pos = 0;
      self->current_index = 0;
      self->mute = 0;
      SCI_WRITE(&sci0, "\nPlaying...\n");
      ASYNC(self, play_note, 0);
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "h") == 0 || strcmp(self->buffer, "help") == 0)
    {
      self->buffer_pos = 0;
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "p") == 0)
    {
      self->buffer_pos = 0;
      self->current_index = 0;
      SCI_WRITE(&sci0, "\nPlaying...\n");

      ASYNC(self, play_note, 0);

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
  CANMsg msg;

  CAN_INIT(&can0);
  SCI_INIT(&sci0);
  CAN_SEND(&can0, &msg);

  SYNC(&tone_task, tone_set_period, 1136);

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
