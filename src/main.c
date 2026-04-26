/* Bootstraps the TinyTimber app and hardware-facing services. */

#include "TinyTimber.h"
#include "canTinyTimber.h"
#include "sciTinyTimber.h"
#include "App.h"

void startApp(App *self, int arg)
{
  (void)arg;

  CAN_INIT(&can0);
  SCI_INIT(&sci0);

  T_RESET(&self->startup_timer);

  print_helper(self);
}

int main()
{
  /* Route hardware interrupts into the TinyTimber objects. */
  INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
  INSTALL(&can0, can_interrupt, CAN_IRQ0);
  TINYTIMBER(&app, startApp, 0);
  return 0;
}
