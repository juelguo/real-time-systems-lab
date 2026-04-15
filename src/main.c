/* Bootstraps the TinyTimber app and hardware-facing services. */

#include "TinyTimber.h"
#include "canTinyTimber.h"
#include "sciTinyTimber.h"
#include "App.h"

void startApp(App *self, int arg)
{
  (void)arg;

  /* Bring up communication peripherals before any user interaction. */
  CAN_INIT(&can0);
  SCI_INIT(&sci0);

  /* Start in a silent state until playback is explicitly requested. */
  SYNC(&tone_task, tone_set_period, 1136);
  SYNC(&tone_task, tone_set_mute, 1);

  print_helper(self);

  /* Keep the square-wave generator running in the background. */
  ASYNC(&tone_task, tone_generator, 1);
}

int main()
{
  /* Route hardware interrupts into the TinyTimber objects. */
  INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
  INSTALL(&can0, can_interrupt, CAN_IRQ0);
  TINYTIMBER(&app, startApp, 0);
  return 0;
}
