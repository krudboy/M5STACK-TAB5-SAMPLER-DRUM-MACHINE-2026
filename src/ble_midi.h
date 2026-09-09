#pragma once
// Bluetooth LE MIDI (BLE-MIDI, per the MMA/AMEI spec) for the M5Stack Tab5.
//
// EXPERIMENTAL: the Tab5's ESP32-P4 application processor has no radio of
// its own — Bluetooth only exists via the onboard ESP32-C6 co-processor,
// bridged through Espressif's "ESP-Hosted" transport. arduino-esp32 exposes
// that bridge through its normal BLEDevice/BLEServer/BLECharacteristic API
// once the hosted-BLE sdkconfig options are enabled (see platformio.ini's
// board_build.esp-idf.custom_sdkconfig) — the same guard arduino-esp32's own
// BLEDevice.cpp uses is mirrored below, so this file quietly compiles down
// to no-op stubs on any build where hosted BLE isn't actually available,
// instead of breaking the build.
//
// Because this path is new and still moving upstream, everything here is
// written defensively: BLE bring-up can never block boot, and a failure
// only disables Bluetooth MIDI (logged once) — the sampler/synth/sequencer
// keep running exactly as if this file didn't exist.
//
// Received notes/CC/etc. are decoded into 3-byte MIDI messages and handed to
// parse_midi_message() (the same entry point the USB MIDI host in
// USB_tools.ino already uses), so a BLE MIDI controller gets identical
// transport control, rotary-CC mapping and ReBirth338 routing "for free".
// Outgoing MIDI (send_midi_message() in USB_tools.ino) mirrors every
// message to any connected BLE MIDI central via ble_midi_send().

#include "soc/soc_caps.h"
#include "sdkconfig.h"

#if defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
#define WASABI338_BLE_MIDI_AVAILABLE 1
#else
#define WASABI338_BLE_MIDI_AVAILABLE 0
#endif

#if WASABI338_BLE_MIDI_AVAILABLE

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static const char *BLE_MIDI_SERVICE_UUID = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
static const char *BLE_MIDI_CHAR_UUID = "7772e5db-3868-4112-a1a9-f2669d106bf3";

static BLEServer *bleMidiServer = nullptr;
static BLECharacteristic *bleMidiChar = nullptr;
static volatile bool bleMidiConnected = false;

static void ble_midi_decode_and_dispatch(const uint8_t *data, size_t len);

// Defined in USB_tools.ino: the shared MIDI entry point, so BLE MIDI gets the
// same transport control, rotary-CC mapping and ReBirth338 routing as USB.
void parse_midi_message(const uint8_t *p);

class BleMidiServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    (void)server;
    bleMidiConnected = true;
    bleMidiStatus = BT_CONNECTED;
    Serial.println("BLE MIDI: central connected");
  }
  void onDisconnect(BLEServer *server) override {
    bleMidiConnected = false;
    bleMidiStatus = BT_READY;
    bleMidiPeer[0] = 0;
    Serial.println("BLE MIDI: central disconnected, re-advertising");
    server->getAdvertising()->start();
  }
};

class BleMidiCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *chr) override {
    String v = chr->getValue();
    if (v.length() == 0) return;
    bleMidiRxCount++;
    bleMidiLastRx = millis();
    ble_midi_decode_and_dispatch((const uint8_t *)v.c_str(), v.length());
  }
};

// Decode one BLE-MIDI packet: [header byte][timestamp byte][status][data...]
// repeated, with running status allowed (a timestamp byte followed directly
// by data bytes, reusing the previous status). Realtime/SysEx bytes are
// skipped — ReBirth338 and the existing rotary-CC/transport handling in
// parse_midi_message() only need Note On/Off, CC and Program Change.
static void ble_midi_decode_and_dispatch(const uint8_t *data, size_t len) {
  if (len < 2) return;
  size_t pos = 1;  // skip the header byte
  uint8_t runningStatus = 0;

  while (pos < len) {
    if (!(data[pos] & 0x80)) {
      // Expected a timestamp byte here; resync by bailing out rather than
      // misinterpreting the rest of a malformed/unsupported packet.
      break;
    }
    pos++;  // consumed the timestamp byte
    if (pos >= len) break;

    uint8_t status = runningStatus;
    if (data[pos] & 0x80) {
      status = data[pos];
      pos++;
    }
    if (status == 0) break;

    uint8_t hi = status & 0xF0;
    if (hi >= 0xF0) break;  // realtime/system/SysEx: not needed here

    uint8_t packet[4] = { 0x08, status, 0, 0 };

    if (hi == 0xC0 || hi == 0xD0) {
      if (pos >= len) break;
      packet[0] = 0x0C;
      packet[2] = data[pos];
      pos += 1;
    } else {
      if (pos + 1 >= len) break;
      packet[0] = (hi == 0x90) ? 0x09 : (hi == 0x80) ? 0x08 : 0x0B;
      packet[2] = data[pos];
      packet[3] = data[pos + 1];
      pos += 2;
    }

    runningStatus = status;
    parse_midi_message(packet);
  }
}

// SDIO link from the ESP32-P4 to the Tab5's ESP32-C6 radio co-processor.
// These pins are board-specific — the ESP-Hosted defaults are for Espressif's
// own P4 eval board — so they must be set before BLEDevice::init() or the
// stack has no way to reach the radio. (Same mapping as WiFi.setPins() in
// M5Stack's own Tab5 Wi-Fi docs.) GPIO 8-15 are dedicated to this link.
#define TAB5_SDIO_CLK 12
#define TAB5_SDIO_CMD 13
#define TAB5_SDIO_D0 11
#define TAB5_SDIO_D1 10
#define TAB5_SDIO_D2 9
#define TAB5_SDIO_D3 8
#define TAB5_SDIO_RST 15

void ble_midi_begin() {
  Serial.println("BLE MIDI: starting (ESP-Hosted transport via the ESP32-C6)...");
  bleMidiStatus = BT_INIT;

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  BLEDevice::setPins(TAB5_SDIO_CLK, TAB5_SDIO_CMD, TAB5_SDIO_D0, TAB5_SDIO_D1,
                     TAB5_SDIO_D2, TAB5_SDIO_D3, TAB5_SDIO_RST);
#endif

  BLEDevice::init("WASABI338 TAB5");
  bleMidiServer = BLEDevice::createServer();
  if (bleMidiServer == nullptr) {
    bleMidiStatus = BT_OFF;
    Serial.println("BLE MIDI: init failed, Bluetooth MIDI disabled (USB MIDI still works)");
    return;
  }
  bleMidiServer->setCallbacks(new BleMidiServerCallbacks());

  BLEService *service = bleMidiServer->createService(BLEUUID(BLE_MIDI_SERVICE_UUID), 30);
  bleMidiChar = service->createCharacteristic(
    BLEUUID(BLE_MIDI_CHAR_UUID),
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE_NR |
      BLECharacteristic::PROPERTY_NOTIFY);
  bleMidiChar->addDescriptor(new BLE2902());
  bleMidiChar->setCallbacks(new BleMidiCharCallbacks());
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLEUUID(BLE_MIDI_SERVICE_UUID));
  advertising->setScanResponse(true);
  advertising->start();

  Serial.println("BLE MIDI: advertising as \"WASABI338 TAB5\"");
}

void ble_midi_send(uint8_t status_byte, uint8_t channel, uint8_t data1, uint8_t data2) {
  if (!bleMidiConnected || bleMidiChar == nullptr) return;

  uint8_t status = status_byte | (channel - 1);
  uint8_t hi = status_byte & 0xF0;
  uint16_t timestamp = (uint16_t)millis() & 0x1FFF;

  uint8_t packet[5];
  packet[0] = 0x80 | ((timestamp >> 7) & 0x3F);  // header byte
  packet[1] = 0x80 | (timestamp & 0x7F);         // timestamp byte
  packet[2] = status;
  packet[3] = data1;
  size_t len = 4;
  if (hi != 0xC0 && hi != 0xD0) {
    packet[4] = data2;
    len = 5;
  }

  bleMidiChar->setValue(packet, len);
  bleMidiChar->notify();
}

#else  // !WASABI338_BLE_MIDI_AVAILABLE

void ble_midi_begin() {
  Serial.println("BLE MIDI: not available on this build target/config (USB MIDI still works)");
}
void ble_midi_send(uint8_t status_byte, uint8_t channel, uint8_t data1, uint8_t data2) {
  (void)status_byte;
  (void)channel;
  (void)data1;
  (void)data2;
}

#endif  // WASABI338_BLE_MIDI_AVAILABLE
