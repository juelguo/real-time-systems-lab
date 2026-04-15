/* Owns player state transitions and playback sequencing. */

#include "App.h"

static int output_muted = 0;
static int volume_before_output_mute = DEFAULT_VOLUME;

static void print_int_line(char *prefix, int value, char *suffix)
{
  char buffer[16];
  SCI_WRITE(&sci0, prefix);
  int_to_string(value, buffer);
  SCI_WRITE(&sci0, buffer);
  SCI_WRITE(&sci0, suffix);
}

int get_alive_board_count(App *self)
{
  int i;
  int count = 0;

  for (i = 0; i < BOARD_COUNT; i++)
  {
    if (self->boards[i].status == BOARD_STATUS_UP)
    {
      count++;
    }
  }

  return count;
}

int get_local_board_rank(App *self)
{
  int i;

  for (i = 0; i < BOARD_COUNT; i++)
  {
    if (self->boards[i].node_id == self->node_id)
    {
      return self->boards[i].rank;
    }
  }

  return BOARD_RANK_UNKNOWN;
}

int note_belongs_to_self(App *self, int note_index)
{
  int alive_count = get_alive_board_count(self);
  int local_rank = get_local_board_rank(self);

  if (alive_count <= 0 || local_rank == BOARD_RANK_UNKNOWN)
  {
    return 0;
  }

  return (note_index % alive_count) == local_rank;
}

void sync_output_for_note(App *self, int note_index)
{
  if (note_belongs_to_self(self, note_index))
  {
    apply_output_unmute();
    return;
  }

  apply_output_mute();
}

void apply_play(App *self)
{
  /* Bump the session so stale scheduled callbacks stop acting on old playback. */
  self->play_session++;
  self->current_index = 0;
  self->mute = 0;
  self->status = 1;
  sync_output_for_note(self, self->current_index);
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

void reset_conductor_settings(App *self)
{
  int tempo = apply_tempo(self, DEFAULT_TEMPO);
  int key = apply_key(self, DEFAULT_KEY);

  send_can_player_command(self, CAN_CMD_SET_TEMPO, tempo);
  send_can_player_command(self, CAN_CMD_SET_KEY, key);

  SCI_WRITE(&sci0, "\nReset to default settings: tempo=");
  print_int_line("", tempo, " bpm, key=");
  print_int_line("", key, "\n");
}

void set_tempo_print_enabled(App *self, int enabled)
{
  self->tempo_print_enabled = enabled ? 1 : 0;
  if (self->tempo_print_enabled)
  {
    SCI_WRITE(&sci0, "\nTempo reporting enabled.\n");
    return;
  }

  SCI_WRITE(&sci0, "\nTempo reporting disabled.\n");
}

void periodic_tempo_report(App *self, int unused)
{
  (void)unused;

  if (self->role == CONDUCTOR_ROLE && self->tempo_print_enabled)
  {
    print_int_line("\nCurrent tempo: ", self->tempo, " bpm\n");
  }

  SEND(SEC(TEMPO_PRINT_PERIOD_SEC), MSEC(10), self, periodic_tempo_report, 0);
}

int is_output_muted(void)
{
  return output_muted;
}

void toggle_output_mute(void)
{
  if (output_muted)
  {
    apply_output_unmute();
    return;
  }

  apply_output_mute();
}

void set_muted_print_enabled(App *self, int enabled)
{
  self->muted_print_enabled = enabled ? 1 : 0;
  if (self->muted_print_enabled)
  {
    SCI_WRITE(&sci0, "\nMuted reporting enabled.\n");
    return;
  }

  SCI_WRITE(&sci0, "\nMuted reporting disabled.\n");
}

void periodic_muted_report(App *self, int unused)
{
  (void)unused;

  if (self->role == MUSICIAN_ROLE && self->muted_print_enabled && is_output_muted())
  {
    SCI_WRITE(&sci0, "\nMUTED\n");
  }

  SEND(SEC(MUTED_PRINT_PERIOD_SEC), MSEC(10), self, periodic_muted_report, 0);
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

  send_token(self, self->current_index);
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
