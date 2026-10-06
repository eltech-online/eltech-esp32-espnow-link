// ElTech-Online ESP32 Wireless Link — RECEIVER
// Listens for the packets the sender board transmits and shows the readings on
// the OLED, along with how good the radio link is.
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// The technique this kit teaches:
//   - ESP-NOW -> two ESP32 boards talking DIRECTLY to each other by radio,
//     with no router, no network name and no password.
//
// This kit has TWO boards and TWO sketches. Flash this one onto the board with
// the display, and sender/sender.ino onto the board with the sensor.
//
// Libraries needed (Arduino IDE Library Manager):
//   Adafruit SH110X
//   Adafruit GFX Library
// (click "Install all" if it offers Adafruit BusIO. WiFi and ESP-NOW are built
// into the ESP32 board package — no separate install)
// Board package: esp32 by Espressif Systems
// Full source, wiring diagrams and setup guide: github.com/eltech-online/eltech-esp32-espnow-link
//
// ---------------------------------------------------------------------------
// New to Arduino code? How to read this file
// ---------------------------------------------------------------------------
// Lines starting with // are comments: notes for people, ignored by the board.
//   1. Settings      - values you can safely change
//   2. The packet    - the exact shape of the data that arrives
//   3. Receiving     - the function the radio calls when a packet comes in
//   4. setup()       - runs ONCE when the board is powered on
//   5. loop()        - then runs over and over: redraw the screen
// A good first experiment: carry the sender to another room and watch the
// signal strength and the "missed" count on the display.

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "logo_bitmap.h"   // shop logo bitmap for the OLED splash screen

// ---- Settings (LINK_ID and WIFI_CHANNEL must be the same in both sketches) ----
const uint32_t LINK_ID = 1;
const int WIFI_CHANNEL = 1;
// No packet for this long = the link is shown as lost.
const unsigned long LINK_LOST_AFTER_MS = 10000;

#define I2C_SDA 8
#define I2C_SCL 9
#define OLED_ADDR 0x3C   // try 0x3D if the screen stays blank
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------------------------------------------------------------------
// The packet — must be exactly the same struct as in sender.ino
// ---------------------------------------------------------------------------
struct Packet {
  uint32_t linkId;        // which pair this packet belongs to
  uint32_t counter;       // goes up by 1 with every packet
  float temperature;      // degrees C
  float humidity;         // percent
  float pressure;         // hectopascals
  uint32_t uptimeSeconds; // how long the sender has been running
};

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------
// The radio calls onPacket() by itself the moment a packet arrives, even in the
// middle of loop(). Like an interrupt, it should do as little as possible: it
// copies the packet into these variables and leaves the rest to loop().
// "volatile" tells the compiler they can change at any moment.
Packet latest;                          // the newest packet
volatile bool havePacket = false;       // has anything arrived yet?
volatile unsigned long lastPacketMs = 0;// millis() when it arrived
volatile int lastRssi = 0;              // signal strength of that packet
volatile uint32_t packetsReceived = 0;
volatile uint32_t packetsMissed = 0;
uint8_t senderMac[6];                   // the sender's address, just to show it

void onPacket(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
  // Ignore anything that is not one of our packets: wrong size (some other
  // ESP-NOW device nearby) or wrong LINK_ID (another one of these kits).
  if (length != sizeof(Packet)) return;
  Packet incoming;
  memcpy(&incoming, data, sizeof(Packet));     // copy the bytes into the struct
  if (incoming.linkId != LINK_ID) return;

  // Missed packets: the counter should go up by exactly 1 each time. If it
  // jumps from 41 to 44, packets 42 and 43 never arrived. (A counter that goes
  // DOWN means the sender was restarted, which is not a miss.)
  if (havePacket && incoming.counter > latest.counter + 1) {
    packetsMissed = packetsMissed + (incoming.counter - latest.counter - 1);
  }

  latest = incoming;
  memcpy(senderMac, info->src_addr, 6);
  // RSSI = received signal strength, in dBm. It is always negative, and closer
  // to 0 is stronger: -40 is excellent, -70 is fine, -90 is barely there.
  lastRssi = info->rx_ctrl->rssi;
  lastPacketMs = millis();
  packetsReceived = packetsReceived + 1;
  havePacket = true;
}

// Starts the radio for ESP-NOW. Returns true if it worked.
bool startRadio() {
  WiFi.mode(WIFI_STA);                 // radio on, but not joined to any network
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  // "Call onPacket() whenever a packet arrives."
  return esp_now_register_recv_cb(onPacket) == ESP_OK;
}

// Draws `text` horizontally centered at the given y, for the given text size.
// Each character of the default font is 6 pixels wide at size 1.
void centerText(const String& text, int y, int textSize) {
  display.setTextSize(textSize);
  int x = (SCREEN_WIDTH - (int)text.length() * 6 * textSize) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

// Signal strength as a word.
String signalWord(int rssi) {
  if (rssi > -55) return "strong";
  if (rssi > -72) return "good";
  if (rssi > -85) return "weak";
  return "v.weak";
}

// The screen:
//
//   y=0   Link OK      2s ago       <- is the link alive, and how old is the data
//   y=13      21.4C                 <- big temperature (text size 2)
//   y=33  48%          1013hPa      <- humidity and pressure
//   y=45  -52dBm strong             <- signal strength
//   y=55  Got 128  Missed 2         <- packet counts
void drawScreen() {
  display.clearDisplay();
  display.setTextSize(1);

  if (!havePacket) {
    centerText("Waiting for the", 12, 1);
    centerText("sender board...", 24, 1);
    centerText("Link ID " + String(LINK_ID) + ", channel " + String(WIFI_CHANNEL), 46, 1);
    display.display();
    return;
  }

  unsigned long ageMs = millis() - lastPacketMs;
  bool linkOK = ageMs < LINK_LOST_AFTER_MS;

  display.setCursor(0, 0);
  display.print(linkOK ? "Link OK" : "LINK LOST");
  String age = String(ageMs / 1000) + "s ago";
  display.setCursor(SCREEN_WIDTH - age.length() * 6, 0);
  display.print(age);
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SH110X_WHITE);

  // isnan() = "is this not a number?": the sender's sensor was missing.
  centerText(isnan(latest.temperature) ? String("n/a") : String(latest.temperature, 1) + "C", 14, 2);

  display.setTextSize(1);
  display.setCursor(0, 33);
  display.print(isnan(latest.humidity) ? String("--") : String(latest.humidity, 0) + "%");
  String pressure = isnan(latest.pressure) ? String("--") : String(latest.pressure, 0) + "hPa";
  display.setCursor(SCREEN_WIDTH - pressure.length() * 6, 33);
  display.print(pressure);

  display.setCursor(0, 45);
  display.print(String(lastRssi) + "dBm " + signalWord(lastRssi));

  display.setCursor(0, 55);
  display.print("Got " + String(packetsReceived) + "  Missed " + String(packetsMissed));
  display.display();
}

// setup() runs once, when the board is powered on or reset.
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  bool oledOK = display.begin(OLED_ADDR, true);
  display.setTextColor(SH110X_WHITE);   // required, or no text is drawn
  display.setTextWrap(false);
  bool radioOK = startRadio();

  Serial.println("========================================");
  Serial.println("           ElTech-Online");
  Serial.println(" ESP32 Wireless Link: RECEIVER (BETA)");
  Serial.println("========================================");
  Serial.println("--- Self-test ---");
  Serial.print("OLED (SH1106): "); Serial.println(oledOK ? "OK" : "NOT FOUND");
  Serial.print("ESP-NOW radio: "); Serial.println(radioOK ? "OK" : "FAILED");
  Serial.print("RESULT:        "); Serial.println(oledOK && radioOK ? "PASS" : "FAIL");
  Serial.print("This board's MAC address: "); Serial.println(WiFi.macAddress());
  Serial.printf("Link ID %lu, WiFi channel %d\n", (unsigned long)LINK_ID, WIFI_CHANNEL);

  display.clearDisplay();
  display.drawBitmap((SCREEN_WIDTH - LOGO_WIDTH) / 2, 0, logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, SH110X_WHITE);
  centerText("ElTech-Online", 36, 1);
  centerText("Wireless Link", 48, 1);
  display.display();
  delay(2000);
}

// loop() runs over and over, forever.
void loop() {
  // Print each new packet to Serial Monitor once. ("static" makes lastPrinted
  // keep its value between one run of loop() and the next.)
  static uint32_t lastPrinted = 0;
  if (havePacket && packetsReceived != lastPrinted) {
    lastPrinted = packetsReceived;
    Serial.printf("Packet %lu from %02X:%02X:%02X:%02X:%02X:%02X: %.1f C, %.1f %%, %.1f hPa, %d dBm, missed %lu\n",
                  (unsigned long)latest.counter,
                  senderMac[0], senderMac[1], senderMac[2], senderMac[3], senderMac[4], senderMac[5],
                  latest.temperature, latest.humidity, latest.pressure,
                  (int)lastRssi, (unsigned long)packetsMissed);
  }

  drawScreen();
  delay(200);   // redraw 5 times a second; packets still arrive during the wait
}
