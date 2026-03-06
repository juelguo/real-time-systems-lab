#include "App.h"
#include "canTinyTimber.h"
#include "sciTinyTimber.h"
#include "sioTinyTimber.h"

App app = initApp();
ToneTask tone_task = initToneTask();
// LoadTask load_obj = initLoadTask();
Can can0 = initCan(CAN_PORT0, &app, receiver);
Serial sci0 = initSerial(SCI_PORT0, &app, reader);
SysIO sio0 = initSysIO(SIO_PORT0, &app, user_button_event);
