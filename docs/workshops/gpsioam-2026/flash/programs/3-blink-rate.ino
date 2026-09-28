// 3 · Blink rate: each press of the BOOT button changes how fast the LED blinks.
// INPUT = BOOT button (GPIO 0), CONTROL = choose the speed, OUTPUT = blue LED (GPIO 2).

const int LED = 2;
const int BUTTON = 0;
const int RATES_MS[] = {1000, 500, 200, 100};
const int RATE_COUNT = 4;

int rate = 0;
bool ledOn = false;
unsigned long lastToggle = 0;
bool lastPressed = false;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  Serial.println("Program 3: press BOOT to change the blink speed");
  Serial.printf("Blinking every %d ms\n", RATES_MS[rate]);
}

void loop() {
  bool pressed = digitalRead(BUTTON) == LOW;
  if (pressed && !lastPressed) {  // a new press
    rate = (rate + 1) % RATE_COUNT;
    Serial.printf("Button pressed -> blinking every %d ms\n", RATES_MS[rate]);
    delay(30);  // ignore switch bounce
  }
  lastPressed = pressed;

  if (millis() - lastToggle >= (unsigned long)RATES_MS[rate]) {
    lastToggle = millis();
    ledOn = !ledOn;
    digitalWrite(LED, ledOn ? HIGH : LOW);
  }
}
