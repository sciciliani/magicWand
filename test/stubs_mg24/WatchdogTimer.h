#pragma once
enum WatchdogPeriod { WDOG_PERIOD_1_S, WDOG_PERIOD_2_S, WDOG_PERIOD_4_S, WDOG_PERIOD_8_S };
class WatchdogTimerClass { public: void begin(); void begin(WatchdogPeriod); void end(); void feed();
  bool watchdogResetHappened(); void setWatchdogOffWhileSleeping(bool); };
extern WatchdogTimerClass WatchdogTimer;
