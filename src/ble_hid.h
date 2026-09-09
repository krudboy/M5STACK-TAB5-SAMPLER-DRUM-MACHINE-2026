#pragma once
// Bluetooth LE keyboard support (HID-over-GATT central).
//
// Where ble_midi.h makes the Tab5 a BLE *peripheral* that a phone or DAW
// connects to, this makes it a BLE *central*: it scans for keyboards and
// macropads advertising the HID service (0x1812), connects, and subscribes to
// their Report characteristics.
//
// Reports are handed to the same places USB HID reports go — the on-screen
// USB KBD monitor and usb_kbd_handle_report() — so a Bluetooth keyboard plays
// notes, drives transport and works with key learn exactly like a wired one,
// and unknown devices (knobs, joysticks) can be inspected on the monitor
// before anything is mapped.
//
// Runs on the same experimental ESP-Hosted transport as BLE MIDI: BLE lives
// on the Tab5's ESP32-C6 co-processor, since the ESP32-P4 has no radio.

#include "ble_midi.h"  // shares the availability guard and the SDIO pin setup

#if WASABI338_BLE_MIDI_AVAILABLE

#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define BLE_HID_SERVICE_UUID ((uint16_t)0x1812)
#define BLE_HID_REPORT_UUID ((uint16_t)0x2A4D)
#define HID_IFACE_BLE 9  // marks BLE reports apart from USB ifaces 0-3 on the monitor

// Defined in USB_tools.ino — shared report log behind the USB KBD monitor.
void hid_log_report(uint8_t iface, const uint8_t *data, uint8_t len);

static BLEClient *bleKbdClient = nullptr;
static BLEAddress *bleKbdPending = nullptr;
static volatile bool bleKbdConnectRequested = false;

static void ble_hid_notify_cb(BLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify) {
  (void)chr;
  (void)isNotify;
  if (len == 0) return;

  bleKbdRxCount++;
  bleKbdLastRx = millis();
  hid_log_report(HID_IFACE_BLE, data, (uint8_t)len);

  // Boot-protocol keyboard reports are 8 bytes: modifiers, reserved, 6 keys.
  // Anything else is logged for the monitor until we know its layout.
  if (len == 8) usb_kbd_handle_report(data);
}

class BleHidClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient *client) override {
    (void)client;
    bleKbdStatus = BT_LINKING;
  }
  void onDisconnect(BLEClient *client) override {
    (void)client;
    bleKbdStatus = BT_READY;
    bleKbdName[0] = 0;
    Serial.println("BLE KBD: disconnected, scanning again");
    BLEDevice::getScan()->start(0, nullptr, false);
  }
};

class BleHidScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!advertisedDevice.isAdvertisingService(BLEUUID(BLE_HID_SERVICE_UUID))) return;
    if (bleKbdConnectRequested || bleKbdStatus == BT_CONNECTED) return;

    // Remember the name for the status panel before we lose the advert.
    String name = advertisedDevice.getName();
    if (name.length() > 0) {
      strncpy(bleKbdName, name.c_str(), sizeof(bleKbdName) - 1);
      bleKbdName[sizeof(bleKbdName) - 1] = 0;
    }

    Serial.printf("BLE KBD: found \"%s\"\n", bleKbdName);
    if (bleKbdPending != nullptr) delete bleKbdPending;
    bleKbdPending = new BLEAddress(advertisedDevice.getAddress());
    bleKbdConnectRequested = true;
    bleKbdStatus = BT_LINKING;
    BLEDevice::getScan()->stop();
  }
};

void ble_hid_begin() {
  if (bleMidiStatus == BT_OFF) return;  // BLE stack never came up

  bleKbdStatus = BT_INIT;
  BLEScan *scan = BLEDevice::getScan();
  if (scan == nullptr) {
    bleKbdStatus = BT_OFF;
    return;
  }
  scan->setAdvertisedDeviceCallbacks(new BleHidScanCallbacks());
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  scan->start(0, nullptr, false);  // 0 = scan until told otherwise
  bleKbdStatus = BT_READY;
  Serial.println("BLE KBD: scanning for HID keyboards");
}

// Connecting must not happen inside the scan callback, so it's deferred to
// this, called from loop().
void ble_hid_task() {
  if (!bleKbdConnectRequested || bleKbdPending == nullptr) return;
  bleKbdConnectRequested = false;

  if (bleKbdClient == nullptr) {
    bleKbdClient = BLEDevice::createClient();
    if (bleKbdClient == nullptr) {
      bleKbdStatus = BT_READY;
      return;
    }
    bleKbdClient->setClientCallbacks(new BleHidClientCallbacks());
  }

  Serial.println("BLE KBD: connecting...");
  if (!bleKbdClient->connect(*bleKbdPending)) {
    Serial.println("BLE KBD: connect failed, scanning again");
    bleKbdStatus = BT_READY;
    BLEDevice::getScan()->start(0, nullptr, false);
    return;
  }

  BLERemoteService *hid = bleKbdClient->getService(BLEUUID(BLE_HID_SERVICE_UUID));
  if (hid == nullptr) {
    Serial.println("BLE KBD: no HID service, dropping");
    bleKbdClient->disconnect();
    bleKbdStatus = BT_READY;
    BLEDevice::getScan()->start(0, nullptr, false);
    return;
  }

  // A keyboard usually exposes several Report characteristics (one per report
  // ID). Subscribe to every notifiable one rather than guessing which carries
  // the keys — the monitor shows which is which.
  uint8_t subscribed = 0;
  std::map<std::string, BLERemoteCharacteristic *> *chars = hid->getCharacteristics();
  for (auto it = chars->begin(); it != chars->end(); ++it) {
    BLERemoteCharacteristic *chr = it->second;
    if (chr == nullptr || !chr->canNotify()) continue;
    chr->registerForNotify(ble_hid_notify_cb);
    subscribed++;
  }

  if (subscribed == 0) {
    Serial.println("BLE KBD: no notifiable reports, dropping");
    bleKbdClient->disconnect();
    bleKbdStatus = BT_READY;
    BLEDevice::getScan()->start(0, nullptr, false);
    return;
  }

  bleKbdStatus = BT_CONNECTED;
  Serial.printf("BLE KBD: registered \"%s\" (%d report characteristics)\n", bleKbdName, subscribed);
}

#else  // !WASABI338_BLE_MIDI_AVAILABLE

void ble_hid_begin() {}
void ble_hid_task() {}

#endif

// Bluetooth off unless explicitly built in: -D WASABI338_ENABLE_BT=1
// Bringing BLE up talks SDIO to the ESP32-C6, and a failed handshake there
// can abort() instead of returning an error — which panics and boot-loops the
// machine. Until that path is proven on real hardware it stays opt-in.
#ifndef WASABI338_ENABLE_BT
#define WASABI338_ENABLE_BT 0
#endif

// Starts Bluetooth once, a few seconds after boot, from loop(). Deferring it
// means the display, audio task and sequencer are all already running, so a
// crash in here is obvious and survivable rather than an unexplained loop.
void ble_start_deferred() {
#if WASABI338_ENABLE_BT
  static bool started = false;
  if (started) return;
  if (millis() < 4000) return;  // let the machine settle first
  started = true;

  Serial.println("BT: starting Bluetooth now (deferred)");
  ble_midi_begin();
  ble_hid_begin();
  Serial.println("BT: startup returned");
#endif
}
