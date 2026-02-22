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

// print the header each time
void print_helper(App *self)
{
  SCI_WRITE(&sci0, "\n--- Main Menu ---\n");
  SCI_WRITE(&sci0, "This is tone generator, it has 2 functions: \n");
  SCI_WRITE(&sci0, "The tone will be automatically run, can be stopped with 's'.\n");
  SCI_WRITE(&sci0, "Press 'v' to begin increase or decrease the volume.\n");
  SCI_WRITE(&sci0, "Press 'b' to adjust background payload period...\n");
  SCI_WRITE(&sci0, "Choice: ");
}

// convert integer to string
// for printing
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
    buffer[i] = '\0';
  }

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
    asm("nop");
  }
  AFTER(USEC(self->period), self, background_task, 0);
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
  if (self->mute == 1)
  {
    if (DEBUG)
    {
      SCI_WRITE(&sci0, "\nReceive mute signal, surpress tone generator...\n");
    }
    DAC_PORT = 0;
    return;
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

  // generate tone
  AFTER(USEC(500), self, tone_generator, next_state);
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
  else if (self->pos < 12)
  {
    self->buffer[self->pos++] = controL_character;
    SCI_WRITECHAR(&sci0, controL_character);
  }
}

void reader(App *self, int c)
{
  if (self->mode == 1)
  {
    if (DEBUG)
    {
      SCI_WRITE(&sci0, "\nDefault mode, currently in volume mode...\n");
    }
    volume_control_handler(self, (char)c);
    return;
  }

  if (self->mode == 2)
  {
    background_loop_handler(self, &load_obj, (char)c);
    return;
  }

  switch (c)
  {
  case 'v':
    /* code */
    if (DEBUG)
    {
      SCI_WRITE(&sci0, "\nEnter volume mode, input value to adjust volume.\n");
    }
    self->mode = 1;
    self->pos = 0;
    SCI_WRITE(&sci0, "\nInput the volume, end with enter: ");
    break;
  case 's':
    self->mute = 1;
    SCI_WRITE(&sci0, "\nMuting tone generator...\n");
    print_helper(self);
    break;
  case 'b':
    self->mode = 2;
    SCI_WRITE(&sci0, "\nAdjusting background load (use '+' or '-' to change): ");
    break;
  default:
    break;
  }
}

void startApp(App *self, int arg)
{
  CANMsg msg;

  CAN_INIT(&can0);
  SCI_INIT(&sci0);

  self->mute = 0;
  self->mode = 0;
  self->background_loop = 1000;

  print_helper(self);
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
