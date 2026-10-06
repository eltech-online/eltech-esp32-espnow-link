// ElTech-Online ESP32 Wireless Link — SENDER
// Reads temperature, humidity and air pressure and sends them through the air
// to the second board (the receiver), with no router, no internet, no pairing.
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// The technique this kit teaches:
//   - ESP-NOW -> two ESP32 boards talking DIRECTLY to each other by radio.
//     It uses the WiFi radio but not a WiFi network: there is no name, no
//     password and no connecting. One board just shouts a small packet of
//     data, and any board listening on the same channel hears it.
//
// This kit has TWO boards and TWO sketches. Flash this one onto the board with
// the sensor, and receiver/receiver.ino onto the board with the display.
//
// Libraries needed (Arduino IDE Library Manager):
//   Adafruit AHTX0
//   Adafruit BMP280 Library
// (click "Install all" if it offers dependencies. WiFi and ESP-NOW are built
// into the ESP32 board package — no separate install)
// Board package: esp32 by Espressif Systems
// Full source, wiring diagrams and setup guide: github.com/eltech-online/eltech-esp32-espnow-link
//
// ---------------------------------------------------------------------------
// New to Arduino code? How to read this file
// ---------------------------------------------------------------------------
// Lines starting with // are comments: notes for people, ignored by the board.
//   1. Settings     - values you can safely change
//   2. The packet   - the exact shape of the data that is sent
//   3. setup()      - runs ONCE when the board is powered on
//   4. loop()       - then runs over and over: read the sensor, send, wait
// A good first experiment: change SEND_EVERY_MS below and upload.

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

// ---- Settings (LINK_ID and WIFI_CHANNEL must be the same in both sketches) ----
// Two of these kits in the same building? Give each pair its own LINK_ID
// (any number), and each receiver ignores the other pair's sender.
const uint32_t LINK_ID = 1;
// WiFi has channels like a radio has stations. Both boards must be on the
// same one: 1, 6 or 11 are the usual choices.
const int WIFI_CHANNEL = 1;
const unsigned long SEND_EVERY_MS = 2000;   // send a packet every 2 seconds

#define I2C_SDA 8
#define I2C_SCL 9
#define BMP280_ADDR 0x77   // the sketch also tries 0x76 by itself

// ---------------------------------------------------------------------------
// The packet
// ---------------------------------------------------------------------------
// A "struct" bundles several values into one thing. This is exactly what
// travels through the air: 24 bytes. The receiver has the SAME struct, so it
// knows which bytes are which. If you add a value here, add it there too, in
// the same place.
struct Packet {
  uint32_t linkId;        // which pair this packet belongs to
  uint32_t counter;       // goes up by 1 with every packet, so the receiver can spot missed ones
  float temperature;      // degrees C
  float humidity;         // percent
  float pressure;         // hectopascals
  uint32_t uptimeSeconds; // how long the sender has been running
};

// The "broadcast" address: a packet sent to FF:FF:FF:FF:FF:FF is heard by
// every board in range. That is why no pairing is needed. (Every WiFi device
// has a 6-byte address called a MAC address; this one means "everyone".)
uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
bool ahtOK = false, bmpOK = false, radioOK = false;
uint32_t packetCounter = 0;

// Starts the radio for ESP-NOW. Returns true if it worked.
bool startRadio() {
  WiFi.mode(WIFI_STA);                 // radio on, but not joined to any network
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  // The ESP32-C3 SuperMini's tiny antenna distorts the signal at full power.
  // 8.5 dBm is cleaner and still reaches across a house.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  if (esp_now_init() != ESP_OK) return false;

  // ESP-NOW needs to be told about each address it will send to: a "peer".
  esp_now_peer_info_t peer = {};       // = {} fills every field with zero first
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

// setup() runs once, when the board is powered on or reset.
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  ahtOK = aht.begin();
  bmpOK = bmp.begin(BMP280_ADDR) || bmp.begin(0x76);
  radioOK = startRadio();

  Serial.println("========================================");
  Serial.println("           ElTech-Online");
  Serial.println("  ESP32 Wireless Link: SENDER (BETA)");
  Serial.println("========================================");
  Serial.println("--- Self-test ---");
  Serial.print("AHT20:         "); Serial.println(ahtOK ? "OK" : "NOT FOUND");
  Serial.print("BMP280:        "); Serial.println(bmpOK ? "OK" : "NOT FOUND");
  Serial.print("ESP-NOW radio: "); Serial.println(radioOK ? "OK" : "FAILED");
  Serial.print("RESULT:        "); Serial.println(ahtOK && bmpOK && radioOK ? "PASS" : "FAIL");
  Serial.print("This board's MAC address: "); Serial.println(WiFi.macAddress());
  Serial.printf("Link ID %lu, WiFi channel %d\n", (unsigned long)LINK_ID, WIFI_CHANNEL);
}

// loop() runs over and over, forever.
void loop() {
  // Fill in a packet with fresh readings. NAN ("not a number") marks a value
  // whose sensor is missing; the receiver shows that as "n/a".
  Packet packet;
  packet.linkId = LINK_ID;
  packet.counter = packetCounter;
  packet.temperature = NAN;
  packet.humidity = NAN;
  packet.pressure = NAN;
  packet.uptimeSeconds = millis() / 1000;

  if (ahtOK) {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    packet.temperature = temp.temperature;
    packet.humidity = humidity.relative_humidity;
  }
  if (bmpOK) packet.pressure = bmp.readPressure() / 100.0F;   // pascals -> hectopascals

  // Send it. "(uint8_t*)&packet" means "the bytes this packet is stored in",
  // and sizeof(packet) is how many of them there are.
  // A broadcast is not acknowledged: the sender never knows whether anyone
  // heard it. That is the receiver's job to work out, from the counter.
  esp_err_t result = radioOK ? esp_now_send(BROADCAST, (uint8_t*)&packet, sizeof(packet)) : ESP_FAIL;

  Serial.printf("Packet %lu: %.1f C, %.1f %%, %.1f hPa -> %s\n", (unsigned long)packet.counter,
                packet.temperature, packet.humidity, packet.pressure,
                result == ESP_OK ? "sent" : "SEND FAILED");

  packetCounter++;
  delay(SEND_EVERY_MS);   // nothing else to do in between, so delay() is fine here
}
