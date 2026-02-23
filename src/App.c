#include "App.h"
#include "TinyTimber.h"
#include "canTinyTimber.h"
#include "sciTinyTimber.h"

// custom incl
#include <stdlib.h>
#include <string.h>

extern App app;
extern Can can0;
extern Serial sci0;
extern LoadTask load_obj;

#define DAC_PORT (*(char *)0x4000741C)
#define MIN_VOLUME 0
#define MAX_VOLUME 20

#define MAX_TASK 8000
#define MIN_TASK 1000

#define DEBUG 0

void receiver(App *self, int unused)
{
  CANMsg msg;
  CAN_RECEIVE(&can0, &msg);
  SCI_WRITE(&sci0, "Can msg received: ");
  SCI_WRITE(&sci0, msg.buff);
}

// helper function to print menu
void print_helper(App *self)
{
  SCI_WRITE(&sci0, "\n--- Main Menu ---\n");
  SCI_WRITE(&sci0, "This is tone generator, it has 2 functions: \n");
  SCI_WRITE(&sci0, "The tone will be automatically run, can be stopped with 'mute' (or 's').\n");
  SCI_WRITE(&sci0, "Type 'volume' (or 'v') to begin increase or decrease the volume.\n");
  SCI_WRITE(&sci0, "Type 'background' (or 'b') to adjust background payload period.\n");
  SCI_WRITE(&sci0, "Type 'deadline'/'nodeadline' (or 'd'/'e') to toggle deadline mode.\n");
  SCI_WRITE(&sci0, "Type 'help' (or 'h') to show this menu.\n");
  SCI_WRITE(&sci0, "Choice: ");
}

// convert int to string, store in buffer
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

// a background task with specific period
void background_task(LoadTask *self, int unused)
{
  for (int i = 0; i < self->background_loop_range; i++)
  {
    asm("nop");     // do nothing, just insert "no operation" isntruction to CPU
  }
  // AFTER(USEC(self->period), self, background_task, 0);
  SEND(USEC(self->period), USEC(1300), self, background_task, 0);
}

// hanlder for loop range adjust
void background_loop_handler(App *self, LoadTask *task, char c)
{
  if (c == 'e')
  {
    // to menu
    self->mode = 0;
    SCI_WRITE(&sci0, "\nReturn to main menu...\n");
    print_helper(self);
    return;
  }
  // adjustment
  if (c == '+')
  {
    task->background_loop_range += 500;
    if (task->background_loop_range > MAX_TASK)
    {
      task->background_loop_range = MAX_TASK;
    }
  }
  else if (c == '-' && task->background_loop_range >= 500)
  {
    task->background_loop_range -= 500;
  }
  else
  {
    return;
  }

  char buff[12];
  int_to_string(task->background_loop_range, buff);
  SCI_WRITE(&sci0, "\nBackground loop range: ");
  SCI_WRITE(&sci0, buff);
  SCI_WRITE(&sci0, "\n");
}

// tone generator
void tone_generator(App *self, int state)
{
  // check if muted
  if (self->mute == 1)
  {
    if (DEBUG)
    {
      SCI_WRITE(&sci0, "\nReceive mute signal, surpress tone generator...\n");
    }
    DAC_PORT = 0;
    // cannot return dirctly, need to keep checking the mute status and wait until it is unmuted
  }

  // change the DAC bit
  int next_state = state ? 0 : 1;

  if (next_state == 1)
  {
    DAC_PORT = self->val;
  }
  else
  {
    DAC_PORT = 0;
  }

  /* generate tone, it will execute every 500 microseconds,
     which means the frequency is 1kHz (since we toggle the bit
     every time, the period is 1ms) */
  if (self->deadline == true)
  {
    // if in deadline control mode, set deadline to 500 microseconds later
    AFTER(USEC(500), self, tone_generator, next_state);
  }
  else
  {
    // otherwise, just set a normal periodic task
    SEND(USEC(500), USEC(100), self, tone_generator, next_state);
  }
}

// control the volume (logic)
void volume_control(App *self, int input)
{
  if (input > MAX_VOLUME)
  {
    // over max volume
    if (DEBUG)
    {
      SCI_WRITE(&sci0, "\nMax volume exceeded, cap the value at max...\n");
    }
    self->val = MAX_VOLUME;
    return;
  }
  if (input < MIN_VOLUME)
  {
    if (DEBUG)
    {
      SCI_WRITE(&sci0, "\nMin volume exceeded, cap the value at min...\n");
    }
    self->val = MIN_VOLUME;
    return;
  }
  // handle normally
  self->val = input;
}

// handler for volume controller
void volume_control_handler(App *self, char controL_character)
{
  if (DEBUG)
  {
    SCI_WRITE(&sci0, "\nBegin volume control handler...\n");
  }

  if (controL_character == 'e')
  {
    if (DEBUG)
    {
      SCI_WRITE(&sci0, "\nExit volume control handler...\n");
    }
    self->mode = 0;
    self->pos = 0;
    SCI_WRITE(&sci0, "\nReturn to main menu...\n");
    print_helper(self);
    return;
  }
  // newline == end input
  if (controL_character == '\n' || controL_character == '\r')
  {
    self->buffer[self->pos++] = '\0';
    int value = atoi(self->buffer);
    volume_control(self, value);

    char current_volume[12];
    int_to_string(self->val, current_volume);
    SCI_WRITE(&sci0, "\nCurrent volume: ");
    SCI_WRITE(&sci0, current_volume);
    SCI_WRITE(&sci0, "\n");
    self->pos = 0;
  }
  else if (self->pos < (int)(sizeof(self->buffer) - 1))
  {
    self->buffer[self->pos++] = controL_character;
    SCI_WRITECHAR(&sci0, controL_character);
  }
}

void command_handler(App *self, char character)
{
  if (DEBUG)
  {
    SCI_WRITE(&sci0, "\nEnter command handler...\n");
  }

  if (character == '\n' || character == '\r')
  {
    self->buffer[self->pos] = '\0';

    // Normalize command to lowercase in-place.
    for (int i = 0; self->buffer[i] != '\0'; i++)
    {
      if (self->buffer[i] >= 'A' && self->buffer[i] <= 'Z')
      {
        self->buffer[i] = self->buffer[i] - 'A' + 'a';
      }
    }

    if (self->pos == 0)
    {
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "v") == 0 || strcmp(self->buffer, "volume") == 0)
    {
      self->mode = VOLUME_MODE;
      self->pos = 0;
      SCI_WRITE(&sci0, "\nInput the volume, end with enter: ");
      return;
    }

    if (strcmp(self->buffer, "s") == 0 || strcmp(self->buffer, "mute") == 0)
    {
      self->mute = true;
      self->pos = 0;
      SCI_WRITE(&sci0, "\nMuting tone generator...\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "r") == 0 || strcmp(self->buffer, "unmute") == 0)
    {
      self->mute = false;
      self->pos = 0;
      SCI_WRITE(&sci0, "\nUnmuting tone generator...\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "b") == 0 || strcmp(self->buffer, "bg") == 0 || strcmp(self->buffer, "background") == 0)
    {
      self->mode = BACKGROUND_LOAD_MODE;
      self->pos = 0;
      SCI_WRITE(&sci0, "\nAdjusting background load (use '+' or '-' to change): ");
      return;
    }

    if (strcmp(self->buffer, "d") == 0 || strcmp(self->buffer, "deadline") == 0)
    {
      self->deadline = true;
      load_obj.deadline = true;
      self->pos = 0;
      SCI_WRITE(&sci0, "\nEnable deadline control mode...\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "e") == 0 || strcmp(self->buffer, "nd") == 0 || strcmp(self->buffer, "nodeadline") == 0)
    {
      self->deadline = false;
      load_obj.deadline = false;
      self->pos = 0;
      SCI_WRITE(&sci0, "\nDisable deadline control mode...\n");
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "h") == 0 || strcmp(self->buffer, "help") == 0)
    {
      self->pos = 0;
      print_helper(self);
      return;
    }

    self->pos = 0;
    SCI_WRITE(&sci0, "\nUnknown command. Type 'help' for valid commands.\n");
    print_helper(self);
    return;
  }
  else if (self->pos < (int)(sizeof(self->buffer) - 1))
  {
    self->buffer[self->pos++] = character;
    SCI_WRITECHAR(&sci0, character);
  }
}

// handler for sci interrupt, read user input and call corresponding handler
void reader(App *self, int c)
{
  // if currently in volume cotrol mode, call volume control handler
  if (self->mode == VOLUME_MODE)
  {
    if (DEBUG)
    {
      SCI_WRITE(&sci0, "\nCurrently in volume mode...\n");
    }
    volume_control_handler(self, (char)c);
    return;
  }

  // if currently in background load adjust mode, call background load handler
  if (self->mode == BACKGROUND_LOAD_MODE)
  {
    if (DEBUG)
    {
      SCI_WRITE(&sci0, "\nCurrently in background load mode...\n");
    }

    background_loop_handler(self, &load_obj, (char)c);
    return;
  }

  // otherwise, it is in control mode
  command_handler(self, (char)c);

}

void startApp(App *self, int arg)
{
  CAN_INIT(&can0);
  SCI_INIT(&sci0);

  self->mute = 0;   // set default to unmuted
  self->mode = CONTROL_MODE;   // set default mode

  print_helper(self);   /* print help info */

  tone_generator(self, 1);
  ASYNC(&load_obj, background_task, 0);
}

int main()
{
  INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
  INSTALL(&can0, can_interrupt, CAN_IRQ0);
  TINYTIMBER(&app, startApp, 0);
  return 0;
}
