// 1 · Blink: the ESP32's blue LED turns on and off every second.
// Board: ESP32 DevKit (ESP32-WROOM). Built-in LED on GPIO 2.

const int LED = 2;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  Serial.println("Program 1: Blink - the blue LED blinks every second");
}

void loop() {
  digitalWrite(LED, HIGH);
  Serial.println("LED ON");
  delay(1000);
  digitalWrite(LED, LOW);
  Serial.println("LED OFF");
  delay(1000);
}
