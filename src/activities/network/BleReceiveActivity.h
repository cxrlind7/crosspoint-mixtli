#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>

#include "activities/Activity.h"

// Receive books from a phone over Bluetooth LE (Mixtli).
//
// The reader becomes a BLE peripheral advertising kServiceUuid under the name
// kAdvertisedName; the "Mixtli Libros" Android app connects, and pushes one file
// at a time. The BLE HID host (page-turner) stays down while this screen is open:
// it owns NimBLE exclusively, and the lifecycle in main.cpp never restarts the
// HID host here because this activity is not a reader nor keepsBluetoothAlive().
//
// Protocol (all text is UTF-8):
//   CTRL characteristic (write + notify)
//     phone -> "B|<size>|<crc32 hex>|<file name>"   begin a file
//     phone -> "E"                                  all data sent
//     phone -> "C"                                  cancel
//     reader -> "R"                                 ready for data
//     reader -> "A|<bytes stored>"                  progress ack (every 4 KB)
//     reader -> "OK|<saved name>" / "ER|<reason>"   result
//   DATA characteristic (write without response): raw file bytes, in order.
//
// Flow control: the phone keeps at most kMaxInFlight unacknowledged bytes, which is
// below the ring capacity, so BLE callbacks never have to drop data. The NimBLE host
// task only copies into the ring; all SD access happens on the main loop.
class BleReceiveActivity final : public Activity {
 public:
  static constexpr const char* kAdvertisedName = "Xteink Mixtli";
  static constexpr const char* kServiceUuid = "8f2c1000-6b1e-4d3a-9b7e-4d6978746c69";
  static constexpr const char* kCtrlUuid = "8f2c1001-6b1e-4d3a-9b7e-4d6978746c69";
  static constexpr const char* kDataUuid = "8f2c1002-6b1e-4d3a-9b7e-4d6978746c69";
  static constexpr size_t kRingSize = 16 * 1024;
  static constexpr size_t kAckEvery = 4 * 1024;  // the app's window is 2 x kAckEvery

  explicit BleReceiveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BleReceive", renderer, mappedInput) {}
  ~BleReceiveActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return phoneConnected || state == State::Receiving; }

  // Called from the NimBLE host task (via the GATT callbacks).
  void onLinkChange(bool connected);
  void onCtrlWrite(const uint8_t* data, size_t len);
  void onDataWrite(const uint8_t* data, size_t len);

 private:
  enum class State { Starting, Waiting, Receiving, Done, Failed, StartFailed };

  State state = State::Starting;
  bool stackUp = false;
  volatile bool phoneConnected = false;
  volatile bool linkChanged = false;

  // Pending control command, handed from the BLE task to the main loop.
  char pendingCmd[320] = {0};
  volatile bool cmdPending = false;

  // Ring buffer filled by the BLE task and drained to SD by the main loop.
  uint8_t* ring = nullptr;
  volatile size_t ringHead = 0;  // write position (BLE task)
  volatile size_t ringTail = 0;  // read position (main loop)
  volatile bool ringOverflow = false;
  volatile bool acceptingData = false;

  HalFile file;
  std::string dirPath;
  std::string tmpPath;
  std::string fileName;  // requested name
  std::string savedName;  // name used on the SD card
  std::string lastError;
  uint32_t expectedSize = 0;
  uint32_t expectedCrc = 0;
  uint32_t received = 0;
  uint32_t crc = 0;
  uint32_t lastAck = 0;
  bool endRequested = false;
  int booksReceived = 0;
  unsigned long lastRenderMs = 0;

  bool startStack();
  void stopStack();
  void notify(const char* msg);
  void handleCommand(const char* cmd);
  void beginFile(const char* args);
  void drainRing();
  void finishFile();
  void abortFile(const char* reason, bool tellPhone);
  std::string uniqueTargetPath(const std::string& name) const;
};
