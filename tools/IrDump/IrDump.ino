// IrDump.ino — prints every IR frame received as hex, on one line per button press.
// Tools > Protocol stack > None      Serial Monitor: 115200
// Wiring: VS1838B OUT -> D3, VCC -> D7, GND -> GND
//
// Example output:
//   0x20DF10EF R x5                       (TV: code, then "repeat" 5 times)
//   0xC3E7...2A 0xC3E7...2B               (AC: 2 different frames)
//   0x11DA2700C5 x2                       (same frame sent twice)

const int RXOUT = D3;
const int RXPWR = D7;

const int MAXE = 1500;
volatile uint32_t edgeUs[MAXE];
volatile int nEdges = 0;

void onEdge() {
  if (nEdges < MAXE) edgeUs[nEdges++] = micros();
}

uint16_t dur[MAXE];

// Decode dur[s..e) (starts with a mark) into a hex string, bits MSB-first as received.
String decode(int s, int e) {
  if (dur[s] > 8000 && e - s <= 4 && s + 1 < e && dur[s + 1] < 2700) return "R";  // NEC repeat
  if (dur[s] > 2000) s += 2;                          // skip header
  uint16_t sMin = 65535, sMax = 0, mMin = 65535, mMax = 0;
  for (int i = s; i < e; i += 2) {
    mMin = min(mMin, dur[i]); mMax = max(mMax, dur[i]);
    if (i + 1 < e) { sMin = min(sMin, dur[i + 1]); sMax = max(sMax, dur[i + 1]); }
  }
  bool bySpace = sMax > sMin * 1.6f;
  if (!bySpace && mMax <= mMin * 1.6f) return "?";
  uint16_t thr = bySpace ? (sMin + sMax) / 2 : (mMin + mMax) / 2;

  String hex = "0x";
  uint8_t nibble = 0;
  int bits = 0;
  for (int i = s; i < e; i += 2) {
    if (bySpace && i + 1 >= e) break;                 // stop bit
    bool one = bySpace ? dur[i + 1] > thr : dur[i] > thr;
    nibble = (nibble << 1) | one;
    if (++bits % 4 == 0) { hex += "0123456789ABCDEF"[nibble]; nibble = 0; }
  }
  if (bits % 4) hex += "0123456789ABCDEF"[(nibble << (4 - bits % 4)) & 0xF];
  if (bits == 0) return "?";
  return hex;
}

void setup() {
  pinMode(RXPWR, OUTPUT); digitalWrite(RXPWR, HIGH);
  pinMode(RXOUT, INPUT_PULLUP);
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nIR DUMP ready: press remote buttons");
  attachInterrupt(digitalPinToInterrupt(RXOUT), onEdge, CHANGE);
}

void loop() {
  noInterrupts();
  int n = nEdges;
  uint32_t last = n ? edgeUs[n - 1] : 0;
  interrupts();
  if (n == 0 || micros() - last < 200000) return;   // wait for 200 ms of silence

  detachInterrupt(digitalPinToInterrupt(RXOUT));
  int nd = n - 1;
  for (int i = 0; i < nd; i++) {
    uint32_t d = edgeUs[i + 1] - edgeUs[i];
    dur[i] = d > 65535 ? 65535 : d;
  }

  if (n >= 4) {
    // Split into frames at long spaces (> 5 ms) or at a new header mark (> 2 ms)
    String line, prev;
    int count = 0, start = 0;
    for (int i = 0; i <= nd; i++) {
      bool split = (i == nd) || (i % 2 == 1 && dur[i] > 5000) || (i % 2 == 0 && i > start && dur[i] > 2000);
      if (!split) continue;
      // frame = dur[start..end), always ending on a mark (drop the gap before a header)
      int end = (i < nd && i % 2 == 0) ? i - 1 : i;
      if (end - start >= 2) {
        String v = decode(start, end);
        if (v == prev) count++;
        else {
          if (prev.length()) { line += prev; if (count > 1) line += " x" + String(count); line += "  "; }
          prev = v; count = 1;
        }
      }
      start = (i % 2 == 1) ? i + 1 : i;
    }
    if (prev.length()) { line += prev; if (count > 1) line += " x" + String(count); }
    Serial.println(line);
    if (n >= MAXE) Serial.println("(buffer full: end missing)");
  }

  nEdges = 0;
  attachInterrupt(digitalPinToInterrupt(RXOUT), onEdge, CHANGE);
}
