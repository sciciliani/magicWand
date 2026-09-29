#include "ir.h"

#include <Arduino.h>

namespace ir {

// ------------------------------------------------------------------ transmit
// NRF_PWM3 is otherwise unused by the sketch (analogWrite/tone use PWM0-2).
// Each entry of `seq` is one carrier period: either "on at duty" or "off".
static NRF_PWM_Type* const kPwm = NRF_PWM3;
static const int kSeqMax = 8000;  // ~210 ms of carrier per DMA burst
static uint16_t seq[kSeqMax + 1];

void begin() {
  pinMode(PIN_IR_LED, OUTPUT);
  digitalWrite(PIN_IR_LED, LOW);
#if HAS_IR_RECEIVER
  pinMode(PIN_IR_RECV_PWR, OUTPUT);
  digitalWrite(PIN_IR_RECV_PWR, LOW);  // receiver off until learning
  pinMode(PIN_IR_RECV, INPUT);
#endif
}

static void play(int n) {
  if (n <= 0) return;
  seq[n++] = 0x8000;  // make sure we end with the LED off
  kPwm->SEQ[0].PTR = (uint32_t)(uintptr_t)seq;  // must point to RAM (EasyDMA)
  kPwm->SEQ[0].CNT = n;
  kPwm->SEQ[0].REFRESH = 0;
  kPwm->SEQ[0].ENDDELAY = 0;
  kPwm->SHORTS = PWM_SHORTS_SEQEND0_STOP_Msk;
  kPwm->EVENTS_STOPPED = 0;
  kPwm->TASKS_SEQSTART[0] = 1;
  uint32_t t0 = micros();
  while (!kPwm->EVENTS_STOPPED && micros() - t0 < 500000) {
  }
}

void send(const IrCode& code) {
  if (code.khz == 0 || code.n == 0) return;
  uint16_t top = 16000 / code.khz;  // 16 MHz clock -> carrier
  uint16_t on = 0x8000 | (uint16_t)(top * IR_DUTY_PERCENT / 100);
  const uint16_t off = 0x8000;
  float periodUs = 1000.0f / code.khz;

  kPwm->PSEL.OUT[0] = g_ADigitalPinMap[PIN_IR_LED];
  kPwm->PSEL.OUT[1] = kPwm->PSEL.OUT[2] = kPwm->PSEL.OUT[3] = 0xFFFFFFFF;
  kPwm->MODE = PWM_MODE_UPDOWN_Up;
  kPwm->PRESCALER = PWM_PRESCALER_PRESCALER_DIV_1;
  kPwm->COUNTERTOP = top;
  kPwm->LOOP = 0;
  kPwm->DECODER = PWM_DECODER_LOAD_Common | PWM_DECODER_MODE_RefreshCount;
  kPwm->ENABLE = 1;

  int n = 0;
  for (int i = 0; i < code.n; i++) {
    bool mark = (i % 2) == 0;
    uint32_t us = code.d[i];
    if (!mark && us > 4000) {
      // Long gap between frames (AC remotes): play what we have, then just wait.
      play(n);
      n = 0;
      delayMicroseconds(us);
      continue;
    }
    int periods = (int)(us / periodUs + 0.5f);
    for (int k = 0; k < periods; k++) {
      if (n >= kSeqMax) {
        play(n);
        n = 0;
      }
      seq[n++] = mark ? on : off;
    }
  }
  play(n);

  kPwm->ENABLE = 0;
  kPwm->PSEL.OUT[0] = 0xFFFFFFFF;
  digitalWrite(PIN_IR_LED, LOW);
}

// ------------------------------------------------------------------ learn
static volatile uint32_t edges[IR_MAX_EDGES + 1];
static volatile int nEdges = 0;
static volatile bool overflow = false;
static LearnState state = kIdle;
static uint32_t learnStart = 0;

static void onEdge() {
  if (nEdges <= IR_MAX_EDGES) edges[nEdges++] = micros();
  else overflow = true;
}

void startLearn() {
#if HAS_IR_RECEIVER
  digitalWrite(PIN_IR_RECV_PWR, HIGH);
  delay(50);  // TSOP needs a moment to settle its AGC
  nEdges = 0;
  overflow = false;
  attachInterrupt(digitalPinToInterrupt(PIN_IR_RECV), onEdge, CHANGE);
  state = kWaiting;
  learnStart = millis();
#else
  state = kTimeout;
#endif
}

void stopLearn() {
#if HAS_IR_RECEIVER
  detachInterrupt(digitalPinToInterrupt(PIN_IR_RECV));
  digitalWrite(PIN_IR_RECV_PWR, LOW);
#endif
  if (state == kWaiting || state == kCapturing) state = kIdle;
}

LearnState pollLearn(IrCode& out) {
  if (state == kWaiting) {
    if (nEdges > 0) state = kCapturing;
    else if (millis() - learnStart > IR_LEARN_TIMEOUT_MS) {
      stopLearn();
      state = kTimeout;
    }
  }
  if (state == kCapturing) {
    noInterrupts();
    int n = nEdges;
    uint32_t last = n ? edges[n - 1] : 0;
    interrupts();
    if (overflow) {
      stopLearn();
      state = kOverflow;
    } else if (micros() - last > IR_END_GAP_US) {
      stopLearn();
      if (n < 8) {  // glitch, not a remote
        state = kTimeout;
      } else {
        out.khz = IR_CARRIER_KHZ;
        out.n = n - 1;
        for (int i = 0; i < n - 1; i++) {
          int32_t d = (int32_t)(edges[i + 1] - edges[i]);
          d += (i % 2 == 0) ? -IR_MARK_EXCESS_US : IR_MARK_EXCESS_US;
          out.d[i] = (uint16_t)constrain(d, 1, 65535);
        }
        state = kDone;
      }
    }
  }
  LearnState s = state;
  if (s == kDone || s == kTimeout || s == kOverflow) state = kIdle;
  return s;
}

}  // namespace ir
