// Low-level DHT22 probe on D7 (GPIO13).
// Prints the idle line level and every edge the sensor produces after a start pulse.
// A healthy DHT22 answers ~20-40 us after release: LOW 80 us, HIGH 80 us, then 40 data bits.
#include <Arduino.h>

const uint8_t DHT_PIN = 13;  // D7

void probe() {
  pinMode(DHT_PIN, INPUT);
  delay(5);
  int idleNoPull = digitalRead(DHT_PIN);
  pinMode(DHT_PIN, INPUT_PULLUP);
  delay(5);
  int idlePull = digitalRead(DHT_PIN);

  // Start pulse: LOW for 1.1 ms, then release and record edges for 6 ms.
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delayMicroseconds(1100);
  pinMode(DHT_PIN, INPUT_PULLUP);

  const int MAXE = 100;
  uint32_t t[MAXE];
  uint8_t lv[MAXE];
  int n = 0;
  noInterrupts();
  uint32_t start = micros();
  int last = digitalRead(DHT_PIN);
  while (micros() - start < 6000 && n < MAXE) {
    int v = digitalRead(DHT_PIN);
    if (v != last) { t[n] = micros() - start; lv[n] = v; n++; last = v; }
  }
  interrupts();

  Serial.printf("idle(no pull)=%d idle(pullup)=%d  edges=%d", idleNoPull, idlePull, n);
  if (n) {
    Serial.print("  first:");
    for (int i = 0; i < n && i < 6; i++) Serial.printf(" %s@%uus", lv[i] ? "H" : "L", t[i]);
  }
  Serial.println(n >= 80 ? "  -> sensor is TALKING" : n ? "  -> partial response" : "  -> NO response");
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // DHT22 needs ~1 s after power-up
  Serial.println("\nDHT22 probe on D7");
}

void loop() {
  probe();
  delay(2500);
}
