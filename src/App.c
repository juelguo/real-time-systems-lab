#include "App.h"
#include "TinyTimber.h"
#include "canTinyTimber.h"
#include "sciTinyTimber.h"
// standard lib
#include "stdlib.h"
#include "string.h"

const char flush = 'F';
const char delimiter = 'e';

extern App app;
extern Can can0;
extern Serial sci0;
extern void DUMPD(int v);

void receiver(App *self, int c)
{
  CANMsg msg;
  CAN_RECEIVE(&can0, &msg);
  SCI_WRITE(&sci0, "Can msg received: ");
  msg.buff[msg.length] = '\0'; // Null-terminate the buffer
  SCI_WRITE(&sci0, msg.buff);
}

/* due to buffer error and some ghoslty
   result, best way to display is string */
void int_to_str(int n, char *str)
{
  int i = 0, is_negative = 0;
  if (n == 0)
  {
    str[i++] = '0';
    str[i] = '\0';
    return;
  }
  if (n < 0)
  {
    is_negative = 1;
    n = -n;
  }
  while (n != 0)
  {
    str[i++] = (n % 10) + '0';
    n = n / 10;
  }
  if (is_negative)
    str[i++] = '-';
  str[i] = '\0';
  // Reverse the string
  for (int j = 0; j < i / 2; j++)
  {
    char temp = str[j];
    str[j] = str[i - j - 1];
    str[i - j - 1] = temp;
  }
}

/* old debug method, caused ghostly
   and garbage display */
void debugNumber(App *self)
{
  SCI_WRITE(&sci0, "\nArray: ");
  for (int i = 0; i < 3; i++)
  {
    DUMPD(self->history[i]);
    SCI_WRITE(&sci0, " ");
  }
  SCI_WRITE(&sci0, "\n");
}

// median method
int calculateMedian(App *self)
{
  int a = self->history[0];
  int b = self->history[1];
  int c = self->history[2];
  int median;

  if (self->count == 1)
  {
    // If we have only one number, return it
    return self->history[0];
  }
  else if (self->count == 2)
  {
    // If we have less than 3 numbers, return the most recent one
    median = (a + b) / 2;
    return median;
  }

  if ((a <= b && b <= c) || (c <= b && b <= a))
    median = b;
  else if ((b <= a && a <= c) || (c <= a && a <= b))
    median = a;
  else
    median = c;

  return median;
}

void reader(App *self, int c)
{
  // just echo the char back to the console for debug purpose
  // SCI_WRITECHAR(&sci0, c);

  // fluse case, reset the history an buffer
  if (c == 'F')
  {
    // handle flush case
    // debugNumber(self);
    SCI_WRITE(&sci0, "\nRcv: 'F'\n");
    for (int i = 0; i < 3; i++)
      self->history[i] = 0;
    self->num_pos = 0;
    self->count = 0;
    SCI_WRITE(&sci0, "The 3-history has been erased\n");
    return;
  }

  // terminator logic \n, \r or 'e'
  if (c == '\n' || c == '\r' || c == 'e')
  {
    self->number[self->num_pos] = '\0';
    int value = atoi(self->number);

    // for history count
    // not test the sequence yet!!!
    self->history[self->count % 3] = value;
    self->count++;

    // math time
    int sum = self->history[0] + self->history[1] + self->history[2];
    int median = calculateMedian(self);

    // to string
    char s_val[12], s_sum[12], s_med[12];
    int_to_str(value, s_val);
    int_to_str(sum, s_sum);
    int_to_str(median, s_med);

    SCI_WRITE(&sci0, "\nEntered: ");
    SCI_WRITE(&sci0, s_val);
    SCI_WRITE(&sci0, " | Sum: ");
    SCI_WRITE(&sci0, s_sum);
    SCI_WRITE(&sci0, " | Median: ");
    SCI_WRITE(&sci0, s_med);
    SCI_WRITE(&sci0, "\n");

    self->num_pos = 0;
  }
  else // store what we typed
  {
    SCI_WRITE(&sci0, "\nRcv: '");
    SCI_WRITECHAR(&sci0, c);
    SCI_WRITE(&sci0, "'\n");

    // hanlde the number
    if (self->num_pos < 63)
    {
      self->number[self->num_pos++] = (char)c;
    }

  }
}

void startApp(App *self, int arg)
{
  // CANMsg msg;

  // init components: CAN, SCI
  CAN_INIT(&can0);
  SCI_INIT(&sci0);
  SCI_WRITE(&sci0, "Hello, if this code here, the program is setup correctly,... \n");

  // call the method
  // also test if it can output 'A'
  //
}

int main()
{
  INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
  INSTALL(&can0, can_interrupt, CAN_IRQ0);
  TINYTIMBER(&app, startApp, 0);
  return 0;
}
