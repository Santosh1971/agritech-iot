// 2 · Button → LED: hold the BOOT button and the blue LED lights up.
// INPUT = BOOT button (GPIO 0, reads LOW when pressed), OUTPUT = blue LED (GPIO 2).

const int LED = 2;
const int BUTTON = 0;

bool lastPressed = false;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  Serial.println("Program 2: press the BOOT button to switch on the blue LED");
}

void loop() {
  bool pressed = digitalRead(BUTTON) == LOW;
  digitalWrite(LED, pressed ? HIGH : LOW);
  if (pressed != lastPressed) {
    Serial.println(pressed ? "Button pressed -> LED ON" : "Button released -> LED OFF");
    lastPressed = pressed;
  }
  delay(20);
}
