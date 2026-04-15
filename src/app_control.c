/* Owns player state transitions and playback sequencing. */

#include "App.h"

static int output_muted = 0;
static int volume_before_output_mute = DEFAULT_VOLUME;

void apply_play(App *self)
{
  /* Bump the session so stale scheduled callbacks stop acting on old playback. */
  self->play_session++;
  self->current_index = 0;
  self->mute = 0;
  self->status = 1;
  ASYNC(self, play_note, self->play_session);
}

void apply_stop(App *self)
{
  /* Invalidate any pending note callbacks and silence the generator. */
  self->play_session++;
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

void play_note(App *self, int unused)
{
  int session = unused;
  /* Ignore callbacks that belong to an old play request. */
  if (session != self->play_session || self->mute == 1 || self->status == 0)
  {
    return;
  }

  int current_period = get_period(self->current_index, self->key);
  int length = get_beat_length(self->current_index) * (30000 / self->tempo);

  /* Leave a short gap between notes so the melody is articulated. */
  int active_play_time = length - 50;

  if (active_play_time < 10)
  {
    active_play_time = 10;
  }

  SYNC(&tone_task, tone_set_period, current_period);
  SYNC(&tone_task, tone_set_mute, 0);

  SEND(MSEC(active_play_time), MSEC(2), self, stop_note, session);
}

void stop_note(App *self, int unused)
{
  int session = unused;
  if (session != self->play_session || self->mute == 1 || self->status == 0)
  {
    return;
  }

  SYNC(&tone_task, tone_set_mute, 1);

  /* Wrap around to repeat the melody continuously. */
  self->current_index = (self->current_index + 1) % 32;

  SEND(MSEC(50), MSEC(2), self, play_note, session);
}
