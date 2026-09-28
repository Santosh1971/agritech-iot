// 5 · Bluetooth: send temperature & humidity to a phone over classic Bluetooth.
// On the phone: pair with "FarmIoT-xxxx", open the Serial Bluetooth Terminal app and connect.
// Type LED ON or LED OFF in the app to switch the blue LED from the phone.
// Wiring: DHT22 data -> GPIO 4 (VCC -> 3V3, GND -> GND).

#include <BluetoothSerial.h>
#include <DHTesp.h>

const int DHT_PIN = 4;
const int LED = 2;

BluetoothSerial bt;
DHTesp dht;
String name;
unsigned long lastRead = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  dht.setup(DHT_PIN, DHTesp::DHT22);

  uint64_t mac = ESP.getEfuseMac();
  char id[5];
  snprintf(id, sizeof(id), "%04X", (unsigned)((mac >> 32) & 0xFFFF));
  name = String("FarmIoT-") + id;
  bt.begin(name);
  Serial.println("Program 5: Bluetooth name " + name + " - pair your phone with it");
}

void loop() {
  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    TempAndHumidity r = dht.getTempAndHumidity();
    String line = dht.getStatus() != DHTesp::ERROR_NONE
        ? String("Sensor not found - check the DHT22 on GPIO 4")
        : String("Temperature ") + String(r.temperature, 1) + " C, Humidity " + String(r.humidity, 1) + " %";
    Serial.println(line);
    bt.println(line);
  }

  if (bt.available()) {
    String cmd = bt.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    if (cmd == "LED ON") digitalWrite(LED, HIGH);
    if (cmd == "LED OFF") digitalWrite(LED, LOW);
    String reply = "Got: " + cmd;
    bt.println(reply);
    Serial.println(reply);
  }
}
