/* Serial console UI for the CAN message regulator. */

#include "App.h"
#include "TinyTimber.h"
#include <stdlib.h>
#include <string.h>

void print_helper(App *self)
{
  (void)self;
  char buf[8];
  SCI_WRITE(&sci0, "\n=== CAN Message Regulator ===\n");
  SCI_WRITE(&sci0, "Commands (press Enter after each):\n");
  SCI_WRITE(&sci0, "  o          Send one CAN message\n");
  SCI_WRITE(&sci0, "  b          Start burst mode (send every 0.5s)\n");
  SCI_WRITE(&sci0, "  x          Stop burst mode\n");
  SCI_WRITE(&sci0, "  d          Set delta (1 / 2 / 5 seconds)\n");
  SCI_WRITE(&sci0, "  p          Toggle TX seq print (on/off)\n");
  SCI_WRITE(&sci0, "  h | help   Show this menu\n");
  SCI_WRITE(&sci0, "Current delta: ");
  int_to_string(self->delta_sec, buf);
  SCI_WRITE(&sci0, buf);
  SCI_WRITE(&sci0, "s  burst: ");
  if (self->burst_mode)
    SCI_WRITE(&sci0, "ON");
  else
    SCI_WRITE(&sci0, "OFF");
  SCI_WRITE(&sci0, "  TX print: ");
  if (self->print_tx)
    SCI_WRITE(&sci0, "ON");
  else
    SCI_WRITE(&sci0, "OFF");
  SCI_WRITE(&sci0, "\nChoice: ");
}

void command_handler(App *self, char c)
{
  if (c == '\n' || c == '\r')
  {
    self->buffer[self->buffer_pos] = '\0';

    /* Normalize to lowercase */
    for (int i = 0; self->buffer[i] != '\0'; i++)
    {
      if (self->buffer[i] >= 'A' && self->buffer[i] <= 'Z')
        self->buffer[i] = self->buffer[i] - 'A' + 'a';
    }

    if (self->buffer_pos == 0)
    {
      print_helper(self);
      return;
    }

    if (strcmp(self->buffer, "o") == 0)
    {
      self->buffer_pos = 0;
      ASYNC(self, send_one_message, 0);
      return;
    }

    if (strcmp(self->buffer, "b") == 0)
    {
      self->buffer_pos = 0;
      if (!self->burst_mode)
      {
        self->burst_mode = 1;
        SCI_WRITE(&sci0, "\nBurst mode ON.\n");
        ASYNC(self, burst_tick, 0);
      }
      else
      {
        SCI_WRITE(&sci0, "\nAlready in burst mode.\n");
      }
      return;
    }

    if (strcmp(self->buffer, "x") == 0)
    {
      self->buffer_pos = 0;
      self->burst_mode = 0;
      SCI_WRITE(&sci0, "\nBurst mode OFF.\n");
      return;
    }

    if (strcmp(self->buffer, "d") == 0)
    {
      self->mode = DELTA_SELECT_MODE;
      self->buffer_pos = 0;
      SCI_WRITE(&sci0, "\nEnter delta (1, 2, or 5): ");
      return;
    }

    if (strcmp(self->buffer, "p") == 0)
    {
      self->buffer_pos = 0;
      self->print_tx = !self->print_tx;
      SCI_WRITE(&sci0, self->print_tx ? "\nTX print ON.\n" : "\nTX print OFF.\n");
      return;
    }

    if (strcmp(self->buffer, "h") == 0 || strcmp(self->buffer, "help") == 0)
    {
      self->buffer_pos = 0;
      print_helper(self);
      return;
    }

    self->buffer_pos = 0;
    SCI_WRITE(&sci0, "\nUnknown command. Type 'h' for help.\n");
    return;
  }
  else if (self->buffer_pos < (int)(sizeof(self->buffer) - 1))
  {
    self->buffer[self->buffer_pos++] = c;
    SCI_WRITECHAR(&sci0, c);
  }
}

void parameter_control_handler(App *self, char c)
{
  if (c == '\n' || c == '\r')
  {
    self->buffer[self->buffer_pos] = '\0';
    int value = atoi(self->buffer);
    self->buffer_pos = 0;
    self->mode = CONTROL_MODE;

    if (value == 1 || value == 2 || value == 5)
    {
      self->delta_sec = value;
      char buf[8];
      SCI_WRITE(&sci0, "\nDelta set to ");
      int_to_string(value, buf);
      SCI_WRITE(&sci0, buf);
      SCI_WRITE(&sci0, "s.\n");
    }
    else
    {
      SCI_WRITE(&sci0, "\nInvalid value. Delta unchanged.\n");
    }
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
