/* Implements the low-level square-wave tone generator. */

#include "App.h"
#include "TinyTimber.h"

#define DAC_PORT (*(char *)0x4000741C)

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

int tone_generator(ToneTask *self, int state)
{
  if (self->mute == 1)
  {
    /* Keep scheduling while muted so playback can resume immediately. */
    DAC_PORT = 0;
    SEND(USEC(self->period), USEC(100), self, tone_generator, state);
    return 0;
  }

  /* Alternate between 0 and the configured amplitude to create the waveform. */
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
  return 0;
}
