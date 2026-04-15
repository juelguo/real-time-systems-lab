/* Shared helpers and melody lookup data used by the player. */

#include "App.h"
#include "TinyTimber.h"

/* Brother John encoded as semitone offsets from the base note. */
static const int brother_john_melodies[32] = {
    0, 2, 4, 0, 0, 2, 4, 0,
    4, 5, 7, 4, 5, 7, 7, 9,
    7, 5, 4, 0, 7, 9, 7, 5,
    4, 0, 0, -5, 0, 0, -5, 0};

static const int periods[25] = {
    2024, 1911, 1803, 1702, 1607,
    1517, 1432, 1351, 1276, 1204,
    1136, 1073, 1012, 955, 902,
    851, 803, 758, 716, 675,
    638, 602, 568, 536, 506};

static const int beat_lengths[32] = {
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 4, 2, 2, 4,
    1, 1, 1, 1, 2, 2,
    1, 1, 1, 1, 2, 2,
    2, 2, 4, 2, 2, 4};

static const int MIN_SHIFT = -10;

int clamp(int value, int min, int max)
{
  if (value < min)
    return min;
  if (value > max)
    return max;
  return value;
}

/* Maps a melody note plus key offset to the timer period used by the DAC task. */
int get_period(int note_pos, int key_offset)
{
  int base = brother_john_melodies[note_pos];

  int shifted = base + key_offset;

  int actual_index = shifted - MIN_SHIFT;

  /* Clamp to the lookup-table range instead of reading out of bounds. */
  if (actual_index < 0)
    actual_index = 0;
  if (actual_index > 24)
    actual_index = 24;

  return periods[actual_index];
}

int get_beat_length(int note_pos)
{
  if (note_pos < 0)
  {
    return beat_lengths[0];
  }
  if (note_pos >= (int)(sizeof(beat_lengths) / sizeof(beat_lengths[0])))
  {
    return beat_lengths[(sizeof(beat_lengths) / sizeof(beat_lengths[0])) - 1];
  }
  return beat_lengths[note_pos];
}

/* Small integer formatter used by SCI logging. */
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
