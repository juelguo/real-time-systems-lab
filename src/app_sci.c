/* Handles the serial console UI and command parsing. */

#include "App.h"
#include "TinyTimber.h"
#include <stdlib.h>
#include <string.h>

void command_handler(App *self, char c)
{
  if (c == '\n' || c == '\r')
  {
    self->buffer[self->buffer_pos] = '\0';
    /* Normalize input so short and long commands are case-insensitive. */
    for (int i = 0; self->buffer[i] != '\0'; i++)
    {
      if (self->buffer[i] >= 'A' && self->buffer[i] <= 'Z')
      {
        self->buffer[i] = self->buffer[i] - 'A' + 'a';
      }
    }

    /* Empty Enter redraws the helper menu. */
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
      /* In musician mode, keyboard input becomes outgoing CAN control. */
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
/* Consumes numeric input after tempo/key/volume mode has been selected. */
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

/* Prints the serial control menu shown at startup and after commands. */
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

void reader(App *self, int c)
{
  /* Dispatch serial input based on whether we are in command or value mode. */
  if (self->mode != CONTROL_MODE)
  {
    parameter_control_handler(self, (char)c);
    return;
  }
  command_handler(self, (char)c);
}
