#ifndef _APP_H
#define _APP_H

#include "TinyTimber.h"

typedef struct
{
  Object super;
  Timer timer;
  char buffer[12];
  int background_loop;
  int mode;
  int pos;
  int val;
  int mute;
} App;

#define initApp() {initObject(), initTimer(), {0}, 0, 0, 0, 0, 0}

void reader(App *, int);
void receiver(App *, int);
void startApp(App *, int);

typedef struct
{
  /* data */
  Object super;
  int background_loop_range;
  int period;
} LoadTask;

#define initLoadTask() {initObject(), 0, 1300}

#endif
