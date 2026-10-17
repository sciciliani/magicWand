// Host test for the IR decoder/encoder in firmware/MagicWand/ir.cpp.
// Build: make test
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/MagicWand/ir.cpp"

// --- stubs for the Arduino calls ir.cpp makes
void pinMode(int, int) {}
void digitalWrite(int, int) {}
int digitalRead(int) { return 0; }
uint32_t millis() { return 0; }
uint32_t micros() { return 0; }
void delay(uint32_t) {}
void delayMicroseconds(uint32_t) {}
void attachInterrupt(int, void (*)(), int) {}
void detachInterrupt(int) {}
void noInterrupts() {}
void GPIO_PortOutSet(GPIO_Port_TypeDef, uint32_t) {}
void GPIO_PortOutClear(GPIO_Port_TypeDef, uint32_t) {}
void GPIO_PinModeSet(GPIO_Port_TypeDef, unsigned int, GPIO_Mode_TypeDef, unsigned int) {}
void interrupts() {}
static DWT_Type dwt; DWT_Type* DWT = &dwt;
static DCB_Type dcb; DCB_Type* DCB = &dcb;
uint32_t SystemCoreClock = 78000000;
// Hardware-carrier fakes: by default the "timer" never toggles the pin, so the
// boot check must reject every timer and sending stays on the software path.
static TIMER_TypeDef t1, t2, t3, t4;
TIMER_TypeDef *TIMER1 = &t1, *TIMER2 = &t2, *TIMER3 = &t3, *TIMER4 = &t4;
static GPIO_TypeDef gpio; GPIO_TypeDef* GPIO = &gpio;
void TIMER_Init(TIMER_TypeDef*, const TIMER_Init_TypeDef*) {}
void TIMER_InitCC(TIMER_TypeDef*, unsigned int, const TIMER_InitCC_TypeDef*) {}
void TIMER_TopSet(TIMER_TypeDef*, uint32_t) {}
void TIMER_CompareSet(TIMER_TypeDef*, unsigned int, uint32_t) {}
void TIMER_Enable(TIMER_TypeDef*, bool) {}
void CMU_ClockEnable(CMU_Clock_TypeDef, bool) {}
uint32_t CMU_ClockFreqGet(CMU_Clock_TypeDef) { return 39000000; }
static bool timerReachesPin = false;  // flip to simulate a timer whose output toggles D6
static unsigned pinReads = 0;
unsigned int GPIO_PinInGet(GPIO_Port_TypeDef, unsigned int) { return timerReachesPin ? (pinReads++ & 1) : 0; }

static int fails = 0;
static void check(bool ok, const char* what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) fails++;
}

static uint16_t raw[2000];

static void roundTrip(const char* proto, const char* hx, int reps, const char* what) {
  IrCode a; memset(&a, 0, sizeof(a));
  assert(ir::setProtocol(proto, a));
  assert(ir::fromHex(hx, a));
  a.repeats = reps;
  int n = ir::toRaw(a, raw, 2000);
  // simulate a real receiver: jitter +-80 us, marks stretched by 50 us then corrected
  srand(1);
  for (int i = 0; i < n; i++) raw[i] += (rand() % 161) - 80;
  IrCode b; memset(&b, 0, sizeof(b));
  bool ok = ir::decode(raw, n, a.khz, b);
  char h[80]; ir::hex(b, h, sizeof(h));
  char msg[200];
  snprintf(msg, sizeof(msg), "%-22s %s x%d -> %s x%d (%d durations)", what, hx, reps, ok ? h : "FAIL", b.repeats, n);
  check(ok && !strcmp(h, hx) && b.repeats == reps && b.pw == a.pw, msg);
}

int main(int argc, char** argv) {
  // Coolix raw exactly as given to Santiago (CODE line)
  const char* coolix =
      "4692,4692,552,1656,552,552,552,1656,552,1656,552,552,552,552,552,1656,552,552,552,552,552,1656,552,552,"
      "552,552,552,1656,552,1656,552,552,552,1656,552,552,552,552,552,552,552,1656,552,1656,552,1656,552,1656,"
      "552,1656,552,1656,552,1656,552,1656,552,552,552,552,552,552,552,552,552,552,552,552,552,1656,552,552,552,"
      "552,552,1656,552,552,552,552,552,552,552,1656,552,552,552,1656,552,1656,552,552,552,1656,552,1656,552,1656,"
      "552,5244,4692,4692,552,1656,552,552,552,1656,552,1656,552,552,552,552,552,1656,552,552,552,552,552,1656,"
      "552,552,552,552,552,1656,552,1656,552,552,552,1656,552,552,552,552,552,552,552,1656,552,1656,552,1656,552,"
      "1656,552,1656,552,1656,552,1656,552,1656,552,552,552,552,552,552,552,552,552,552,552,552,552,1656,552,552,"
      "552,552,552,1656,552,552,552,552,552,552,552,1656,552,552,552,1656,552,1656,552,552,552,1656,552,1656,552,"
      "1656,552";
  int n = 0;
  for (const char* p = coolix; *p;) { raw[n++] = strtoul(p, (char**)&p, 10); if (*p == ',') p++; }
  IrCode c; memset(&c, 0, sizeof(c));
  bool ok = ir::decode(raw, n, 38, c);
  char h[80]; ir::hex(c, h, sizeof(h));
  check(ok && !strcmp(h, "B24D1FE048B7") && c.repeats == 2 && c.gap == 5244, "Coolix CODE line -> B24D1FE048B7 x2");
  static uint16_t back[400];
  int m = ir::toRaw(c, back, 400);
  check(m == n && !memcmp(back, raw, n * 2), "Coolix re-encodes to the identical raw list");

  roundTrip("coolix", "B24D7B84E01F", 2, "Coolix off");
  roundTrip("nec", "20DF10EF", 1, "LG power (NEC)");
  roundTrip("samsung", "E0E040BF", 1, "Samsung power");
  roundTrip("samsung", "E0E040BF", 3, "Samsung held (x3)");
  roundTrip("sony", "A90", 3, "Sony power (12 bit)");
  roundTrip("coolix", "C3E76000200000002000040000002A", 1, "long AC frame 120 bit");

  // NEC followed by "repeat" bursts while the button is held
  IrCode a; memset(&a, 0, sizeof(a));
  ir::setProtocol("nec", a); ir::fromHex("20DF10EF", a);
  n = ir::toRaw(a, raw, 2000);
  for (int r = 0; r < 3; r++) { raw[n++] = 40000; raw[n++] = 9000; raw[n++] = 2250; raw[n++] = 560; }
  IrCode b; memset(&b, 0, sizeof(b));
  ok = ir::decode(raw, n, 38, b); ir::hex(b, h, sizeof(h));
  check(ok && !strcmp(h, "20DF10EF") && b.repeats == 1, "NEC + held repeats -> 20DF10EF x1");

  // Keeps the name
  memset(&b, 0, sizeof(b)); strcpy(b.name, "TV");
  ir::decode(raw, n, 38, b);
  check(!strcmp(b.name, "TV"), "decode keeps the code's name");

  // Garbage is rejected
  for (int i = 0; i < 40; i++) raw[i] = 600;
  check(!ir::decode(raw, 40, 38, b), "same-length pulses rejected");

  {  // Press caught half-way (Santiago's AC "off"): short first frame, then a full one.
    IrCode ac = {}, got = {};
    ir::setProtocol("coolix", ac); ir::fromHex("B24D7B84E01F", ac); ac.khz = 38;  // 2 frames
    static uint16_t ar[300];
    int an = ir::toRaw(ac, ar, 300);
    int skip = 2 + 28 * 2;  // header + the first 28 bits missed
    char ah[32];
    bool aok = ir::decode(ar + skip, an - skip, 38, got);
    ir::hex(got, ah, sizeof(ah));
    check(aok && !strcmp(ah, "B24D7B84E01F") && got.nbits == 48, "late start: keeps the complete frame B24D7B84E01F, not 4E01F");
    bool fok = ir::decode(ar, an, 38, got);
    ir::hex(got, ah, sizeof(ah));
    check(fok && !strcmp(ah, "B24D7B84E01F") && got.repeats == 2, "full capture: B24D7B84E01F x2 (as IrDump)");
  }
  {  // RCA (the TV remote's codes): learned -> IR_RCA_KHZ (38), HEX rca -> same timing
    IrCode r = {}, back = {};
    check(ir::setProtocol("rca", r) && ir::fromHex("F2A0D5", r) && r.khz == IR_RCA_KHZ && r.repeats == 2, "HEX rca F2A0D5 -> 38 kHz x2");
    static uint16_t rr[200];
    int rn = ir::toRaw(r, rr, 200);
    char rh[16];
    bool rok = ir::decode(rr, rn, 38, back);
    ir::hex(back, rh, sizeof(rh));
    check(rok && !strcmp(rh, "F2A0D5") && ir::carrierFor(back) == IR_RCA_KHZ, "learned RCA F2A0D5 -> 38 kHz");
    IrCode n = {};
    ir::setProtocol("nec", n); ir::fromHex("20DF10EF", n);
    check(ir::carrierFor(n) == 38, "learned NEC stays 38 kHz");
  }

  // Every app preset (TV brands) must decode (file made by test/dump_presets.mjs)
  if (argc > 1) {
    FILE* f = fopen(argv[1], "r");
    static char line[8000];
    int count = 0, bad = 0;
    while (f && fgets(line, sizeof(line), f)) {
      char name[64]; unsigned khz; char* list = line;
      if (sscanf(line, "%63s %u", name, &khz) != 2) continue;
      list = strchr(strchr(line, ' ') + 1, ' ') + 1;
      int k = 0;
      for (char* p = list; *p && *p != '\n' && k < 2000;) { raw[k++] = strtoul(p, &p, 10); if (*p == ',') p++; }
      IrCode x; memset(&x, 0, sizeof(x));
      if (!ir::decode(raw, k, khz, x)) { printf("FAIL  preset %s doesn't decode\n", name); bad++; }
      count++;
    }
    if (f) fclose(f);
    char msg[80]; snprintf(msg, sizeof(msg), "all %d app presets decode", count);
    check(bad == 0 && count > 0, msg);
  }

  // --- hardware carrier selection (0.5.0)
  ir::begin();
  check(!ir::hwCarrier() && strstr(ir::timingInfo(), "carrier SW"), "no timer reaches the pin -> software carrier");
  check(!ir::setHwCarrier(true), "IRMODE hw refused without a working timer");
  timerReachesPin = true;
  ir::begin();
  check(ir::hwCarrier() && strstr(ir::timingInfo(), "carrier HW TIMER1"), "timer toggles the pin -> HW TIMER1");
  check(GPIO->TIMERROUTE[1].CC0ROUTE == ((2u << 0) | (6u << 16)), "TIMER1 CC0 routed to PC6 (D6)");
  IrCode s; memset(&s, 0, sizeof(s));
  ir::setProtocol("nec", s); ir::fromHex("20DF10EF", s);
  ir::send(s);
  check(GPIO->TIMERROUTE_CLR[1].ROUTEEN == 1u, "route switched off after a send (LED pin back to LOW)");
  check(!ir::setHwCarrier(false) && strstr(ir::timingInfo(), "IRMODE sw"), "IRMODE sw -> software carrier");
  check(ir::setHwCarrier(true), "IRMODE hw again");

  printf("\n%s\n", fails ? "FAILED" : "PASS");
  return fails ? 1 : 0;
}
