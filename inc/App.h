#ifndef _APP_H
#define _APP_H

#include "TinyTimber.h"
#include <stdint.h>

typedef enum
{
  CONTROL_MODE = 0,
  VOLUME_MODE = 1,
  BACKGROUND_LOAD_MODE = 2,
  DEADLINE_CONTROL_MODE = 3
} Mode;

typedef struct
{
  Object super;         // inherit from Object
  Timer timer;          // inherit from Timer
  char buffer[32];      // buffer for user input
  Mode mode;             // 0: default, 1: volume control, 2: background load adjust
  int pos;
  int val;
  bool mute;             // set mute to 1 to stop tone generator, set to 0 to start it
  bool deadline;         // the deadline for the deadline control mode
} App;

#define initApp() {initObject(), initTimer(), {0}, 0, 0, 0, 0, 0}

void reader(App *, int);
void receiver(App *, int);
void startApp(App *, int);

typedef struct
{
  /* data */
  Object super;
  int background_loop_range;    // the range of the background loop
  int period;
  bool deadline;
  uint32_t wcet_sample_count;
  uint32_t wcet_max_cycles;
  uint64_t wcet_total_cycles;
} LoadTask;

#define initLoadTask() {initObject(), 0, 1000, 0, 0, 0, 0}

#endif
