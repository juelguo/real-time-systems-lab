/* CAN message regulator: sender and regulated receiver/delivery. */

#include "App.h"
#include "TinyTimber.h"

void int_to_string(int, char *);

/* Send one CAN message with the current sequence number. */
void send_one_message(App *self, int unused)
{
  (void)unused;
  CANMsg msg;
  msg.msgId  = self->seq_tx;
  msg.nodeId = 0;
  msg.length = 1;
  msg.buff[0] = 0;

  if (self->print_tx)
  {
    char buf[8];
    SCI_WRITE(&sci0, "\nTX seq=");
    int_to_string(self->seq_tx, buf);
    SCI_WRITE(&sci0, buf);
    SCI_WRITE(&sci0, "\n");
  }

  self->seq_tx = (self->seq_tx + 1) & 0x7F;
  CAN_SEND(&can0, &msg);
}

/* Periodic burst sender: fires every 500 ms while burst_mode is active. */
void burst_tick(App *self, int unused)
{
  (void)unused;
  send_one_message(self, 0);
  if (self->burst_mode)
  {
    AFTER(MSEC(500), self, burst_tick, 0);
  }
}

/* Deliver one buffered message to the application. */
void deliver_message(App *self, int unused)
{
  (void)unused;
  if (self->reg_count <= 0)
  {
    return;
  }

  int seq = self->reg_buf[self->reg_head];
  self->reg_head = (self->reg_head + 1) % BURST_BUF_SIZE;
  self->reg_count--;

  Time now = T_SAMPLE(&self->startup_timer);
  char buf[12];
  SCI_WRITE(&sci0, "\nDELIVERED seq=");
  int_to_string(seq, buf);
  SCI_WRITE(&sci0, buf);
  SCI_WRITE(&sci0, " at T=");
  int_to_string(SEC_OF(now), buf);
  SCI_WRITE(&sci0, buf);
  SCI_WRITE(&sci0, "s ");
  int_to_string(MSEC_OF(now), buf);
  SCI_WRITE(&sci0, buf);
  SCI_WRITE(&sci0, "ms\n");
}

/* CAN interrupt handler — implements the regulator logic. */
void receiver(App *self, int unused)
{
  (void)unused;
  CANMsg msg;
  if (CAN_RECEIVE(&can0, &msg) != 0)
  {
    return;
  }

  /* t_in: absolute arrival time since startup (via T_SAMPLE on startup_timer).
     CURRENT_OFFSET() is merely execution jitter, NOT wall-clock time. */
  Time t_in = T_SAMPLE(&self->startup_timer);

  if (self->reg_count >= BURST_BUF_SIZE)
  {
    SCI_WRITE(&sci0, "\nWARN: buffer full, message dropped\n");
    return;
  }

  /* Enqueue sequence number */
  self->reg_buf[self->reg_tail] = (int)msg.msgId;
  self->reg_tail = (self->reg_tail + 1) % BURST_BUF_SIZE;
  self->reg_count++;

  /* Compute regulated delivery time (absolute) */
  Time delta = SEC(self->delta_sec);
  Time t_deliver = self->last_delivery_time + delta;
  if (t_in > t_deliver)
  {
    t_deliver = t_in;
  }

  /* Record scheduled delivery time now (not at callback) for correct spacing */
  self->last_delivery_time = t_deliver;

  /* delay is relative to current message baseline; AFTER adds it to that baseline */
  Time delay = t_deliver - t_in;
  if (delay < 0)
  {
    delay = 0;
  }

  AFTER(delay, self, deliver_message, 0);
}
