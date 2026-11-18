#include "BleReceiveActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "BleInput.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

#if FREEINK_CAP_BLE_HID_HOST
#include <NimBLEDevice.h>
#endif

namespace {

// Books go next to the user's existing library when that folder exists.
constexpr const char* kPreferredDir = "/Mis Libros Kindle";
constexpr const char* kTmpName = ".bt_recv.tmp";

portMUX_TYPE g_ringMux = portMUX_INITIALIZER_UNLOCKED;
BleReceiveActivity* g_active = nullptr;

#if FREEINK_CAP_BLE_HID_HOST
NimBLECharacteristic* g_ctrlChr = nullptr;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    // Short connection interval: throughput is bounded by packets per interval.
    server->updateConnParams(info.getConnHandle(), 6, 12, 0, 400);
    if (g_active) g_active->onLinkChange(true);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    LOG_INF("BTRX", "phone disconnected (reason %d)", reason);
    if (g_active) g_active->onLinkChange(false);
    NimBLEDevice::startAdvertising();
  }
};

class CtrlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo&) override {
    const NimBLEAttValue v = chr->getValue();
    if (g_active) g_active->onCtrlWrite(v.data(), v.size());
  }
};

class DataCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo&) override {
    const NimBLEAttValue v = chr->getValue();
    if (g_active) g_active->onDataWrite(v.data(), v.size());
  }
};

ServerCallbacks g_serverCallbacks;
CtrlCallbacks g_ctrlCallbacks;
DataCallbacks g_dataCallbacks;
#endif

// FAT forbids these; also drop control characters.
std::string sanitizeFileName(const char* in) {
  std::string out;
  for (const char* p = in; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20 || strchr("\\/:*?\"<>|", c)) {
      out.push_back('_');
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
  if (out.size() > 200) out.resize(200);
  return out;
}

}  // namespace

BleReceiveActivity::~BleReceiveActivity() {
  stopStack();
  free(ring);
  ring = nullptr;
}

void BleReceiveActivity::onEnter() {
  Activity::onEnter();
  state = State::Starting;
  dirPath = Storage.exists(kPreferredDir) ? kPreferredDir : "";
  tmpPath = dirPath + "/" + kTmpName;
  requestUpdate();
}

void BleReceiveActivity::onExit() {
  Activity::onExit();
  if (file) {
    file.close();
    Storage.remove(tmpPath.c_str());
  }
  stopStack();
  free(ring);
  ring = nullptr;
}

bool BleReceiveActivity::startStack() {
#if FREEINK_CAP_BLE_HID_HOST
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    lastError = "WiFi";
    return false;
  }
  if (!ring) ring = static_cast<uint8_t*>(malloc(kRingSize));
  if (!ring) {
    LOG_ERR("BTRX", "OOM: ring buffer");
    return false;
  }
  // The page-turner host owns NimBLE when running; take it over for this screen.
  if (BleHid.isRunning()) bleinput::stop();

  HalPowerManager::Lock powerLock;  // controller init hangs at the low-power clock
  LOG_INF("BTRX", "starting peripheral heap=%u", ESP.getFreeHeap());
  if (!NimBLEDevice::init(kAdvertisedName)) {
    LOG_ERR("BTRX", "NimBLE init failed");
    return false;
  }
  g_active = this;
  NimBLEDevice::setMTU(247);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&g_serverCallbacks, false);
  NimBLEService* service = server->createService(kServiceUuid);
  g_ctrlChr = service->createCharacteristic(kCtrlUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY, 320);
  g_ctrlChr->setCallbacks(&g_ctrlCallbacks);
  NimBLECharacteristic* data = service->createCharacteristic(kDataUuid, NIMBLE_PROPERTY::WRITE_NR, 512);
  data->setCallbacks(&g_dataCallbacks);
  server->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.addServiceUUID(NimBLEUUID(kServiceUuid));
  NimBLEAdvertisementData scanData;
  scanData.setName(kAdvertisedName);
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  if (!adv->start()) {
    LOG_ERR("BTRX", "advertising failed");
    return false;
  }
  stackUp = true;
  LOG_INF("BTRX", "advertising heap=%u", ESP.getFreeHeap());
  return true;
#else
  return false;
#endif
}

void BleReceiveActivity::stopStack() {
#if FREEINK_CAP_BLE_HID_HOST
  if (!stackUp && g_active != this) return;
  HalPowerManager::Lock powerLock;
  g_active = nullptr;
  g_ctrlChr = nullptr;
  NimBLEDevice::deinit(true);
  stackUp = false;
  phoneConnected = false;
  LOG_INF("BTRX", "stopped heap=%u", ESP.getFreeHeap());
#endif
}

void BleReceiveActivity::notify(const char* msg) {
#if FREEINK_CAP_BLE_HID_HOST
  if (g_ctrlChr && phoneConnected) g_ctrlChr->notify(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
#endif
}

// ---- BLE host task side ---------------------------------------------------

void BleReceiveActivity::onLinkChange(const bool connected) {
  phoneConnected = connected;
  linkChanged = true;
}

void BleReceiveActivity::onCtrlWrite(const uint8_t* data, const size_t len) {
  if (len == 0 || len >= sizeof(pendingCmd)) return;
  if (cmdPending) {
    LOG_ERR("BTRX", "command dropped: previous one still pending");
    return;
  }
  memcpy(pendingCmd, data, len);
  pendingCmd[len] = '\0';
  cmdPending = true;
}

void BleReceiveActivity::onDataWrite(const uint8_t* data, const size_t len) {
  if (!acceptingData || !ring) return;
  portENTER_CRITICAL(&g_ringMux);
  const size_t used = (ringHead + kRingSize - ringTail) % kRingSize;
  if (used + len >= kRingSize) {
    ringOverflow = true;
  } else {
    size_t head = ringHead;
    const size_t first = std::min(len, kRingSize - head);
    memcpy(ring + head, data, first);
    memcpy(ring, data + first, len - first);
    ringHead = (head + len) % kRingSize;
  }
  portEXIT_CRITICAL(&g_ringMux);
}

// ---- Main loop side --------------------------------------------------------

void BleReceiveActivity::handleCommand(const char* cmd) {
  LOG_INF("BTRX", "cmd: %.40s", cmd);
  if (cmd[0] == 'B' && cmd[1] == '|') {
    beginFile(cmd + 2);
  } else if (cmd[0] == 'E') {
    if (state == State::Receiving) endRequested = true;
  } else if (cmd[0] == 'C') {
    if (state == State::Receiving) abortFile(tr(STR_BT_RX_CANCELLED), false);
  }
}

std::string BleReceiveActivity::uniqueTargetPath(const std::string& name) const {
  std::string path = dirPath + "/" + name;
  if (!Storage.exists(path.c_str())) return path;
  const size_t dot = name.rfind('.');
  const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
  const std::string ext = dot == std::string::npos ? "" : name.substr(dot);
  for (int i = 2; i < 100; ++i) {
    path = dirPath + "/" + stem + " (" + std::to_string(i) + ")" + ext;
    if (!Storage.exists(path.c_str())) return path;
  }
  return dirPath + "/" + stem + " (" + std::to_string(millis()) + ")" + ext;
}

void BleReceiveActivity::beginFile(const char* args) {
  if (state == State::Receiving) abortFile("restart", false);

  // args: <size>|<crc hex>|<name>
  char* end = nullptr;
  const unsigned long size = strtoul(args, &end, 10);
  if (!end || *end != '|') return abortFile("bad header", true);
  const unsigned long crcValue = strtoul(end + 1, &end, 16);
  if (!end || *end != '|') return abortFile("bad header", true);
  fileName = sanitizeFileName(end + 1);
  if (fileName.empty() || !FsHelpers::isSafePathComponent(fileName)) return abortFile("bad name", true);
  if (size == 0) return abortFile("empty file", true);

  Storage.remove(tmpPath.c_str());
  if (!Storage.openFileForWrite("BTRX", tmpPath, file)) return abortFile(tr(STR_BT_RX_SD_ERROR), true);

  expectedSize = size;
  expectedCrc = crcValue;
  received = 0;
  crc = 0;
  lastAck = 0;
  endRequested = false;
  ringOverflow = false;
  portENTER_CRITICAL(&g_ringMux);
  ringHead = ringTail = 0;
  portEXIT_CRITICAL(&g_ringMux);
  acceptingData = true;
  state = State::Receiving;
  lastError.clear();
  LOG_INF("BTRX", "receiving %s (%lu bytes)", fileName.c_str(), size);
  notify("R");
  requestUpdate();
}

void BleReceiveActivity::drainRing() {
  if (!file || !ring) return;
  while (true) {
    portENTER_CRITICAL(&g_ringMux);
    const size_t head = ringHead;
    portEXIT_CRITICAL(&g_ringMux);
    const size_t tail = ringTail;
    if (head == tail) break;
    // Contiguous span only; the wrap is picked up on the next iteration.
    const size_t span = head > tail ? head - tail : kRingSize - tail;
    if (received + span > expectedSize) return abortFile("too much data", true);
    if (file.write(ring + tail, span) != span) return abortFile(tr(STR_BT_RX_SD_ERROR), true);
    crc = esp_rom_crc32_le(crc, ring + tail, span);
    received += span;
    portENTER_CRITICAL(&g_ringMux);
    ringTail = (tail + span) % kRingSize;
    portEXIT_CRITICAL(&g_ringMux);
  }
  if (received - lastAck >= kAckEvery || (received == expectedSize && lastAck != received)) {
    lastAck = received;
    char msg[24];
    snprintf(msg, sizeof(msg), "A|%lu", static_cast<unsigned long>(received));
    notify(msg);
  }
}

void BleReceiveActivity::finishFile() {
  acceptingData = false;
  file.close();
  if (crc != expectedCrc) {
    LOG_ERR("BTRX", "crc mismatch %08lx != %08lx", static_cast<unsigned long>(crc),
            static_cast<unsigned long>(expectedCrc));
    Storage.remove(tmpPath.c_str());
    return abortFile(tr(STR_BT_RX_CORRUPT), true);
  }
  const std::string target = uniqueTargetPath(fileName);
  if (!Storage.rename(tmpPath.c_str(), target.c_str())) {
    Storage.remove(tmpPath.c_str());
    return abortFile(tr(STR_BT_RX_SD_ERROR), true);
  }
  savedName = target.substr(target.rfind('/') + 1);
  clearBookCache(target);
  library::markLibraryIndexDirty();
  ++booksReceived;
  state = State::Done;
  LOG_INF("BTRX", "saved %s", target.c_str());
  const std::string msg = "OK|" + savedName;
  notify(msg.c_str());
  requestUpdate();
}

void BleReceiveActivity::abortFile(const char* reason, const bool tellPhone) {
  acceptingData = false;
  if (file) file.close();
  Storage.remove(tmpPath.c_str());
  lastError = reason;
  state = State::Failed;
  LOG_ERR("BTRX", "transfer failed: %s", reason);
  if (tellPhone) {
    const std::string msg = std::string("ER|") + reason;
    notify(msg.c_str());
  }
  requestUpdate();
}

void BleReceiveActivity::loop() {
  if (state == State::Starting) {
    requestUpdateAndWait();
    state = startStack() ? State::Waiting : State::StartFailed;
    if (state == State::StartFailed) stopStack();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (linkChanged) {
    linkChanged = false;
    if (!phoneConnected && state == State::Receiving) abortFile(tr(STR_BT_RX_LINK_LOST), false);
    requestUpdate();
  }

  if (cmdPending) {
    char cmd[sizeof(pendingCmd)];
    memcpy(cmd, pendingCmd, sizeof(cmd));
    cmdPending = false;
    handleCommand(cmd);
  }

  if (state == State::Receiving) {
    if (ringOverflow) return abortFile("overflow", true);
    drainRing();
    if (state == State::Receiving && endRequested && received == expectedSize) finishFile();
    // E-ink is slow; refresh the progress bar about every 2 s.
    if (state == State::Receiving && millis() - lastRenderMs > 2000) {
      lastRenderMs = millis();
      requestUpdate();
    }
  }
}

void BleReceiveActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int midY = pageHeight / 2;
  const int textWidth = pageWidth - 2 * metrics.contentSidePadding;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BT_RECEIVE));

  char line[96];
  switch (state) {
    case State::Starting:
      renderer.drawCenteredText(UI_12_FONT_ID, midY, tr(STR_BT_RX_STARTING));
      break;
    case State::StartFailed:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_BT_RX_START_FAILED), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 15, lastError == "WiFi" ? tr(STR_BT_RX_WIFI_ON) : "");
      break;
    case State::Receiving: {
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 60, tr(STR_BT_RX_RECEIVING), true, EpdFontFamily::BOLD);
      const std::string name = renderer.truncatedText(UI_10_FONT_ID, fileName.c_str(), textWidth);
      renderer.drawCenteredText(UI_10_FONT_ID, midY - 25, name.c_str());
      const int barW = textWidth;
      const int barX = metrics.contentSidePadding;
      const int pct = expectedSize ? static_cast<int>(uint64_t{received} * 100 / expectedSize) : 0;
      renderer.drawRect(barX, midY + 5, barW, 20);
      renderer.fillRect(barX + 2, midY + 7, (barW - 4) * pct / 100, 16);
      snprintf(line, sizeof(line), "%d%%  (%lu / %lu KB)", pct, static_cast<unsigned long>(received / 1024),
               static_cast<unsigned long>(expectedSize / 1024));
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 45, line);
      break;
    }
    default: {
      // Waiting / Done / Failed: show how to connect, plus the last outcome.
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 70, tr(STR_BT_RX_READY), true, EpdFontFamily::BOLD);
      snprintf(line, sizeof(line), "%s: %s", tr(STR_BT_RX_NAME), kAdvertisedName);
      renderer.drawCenteredText(UI_10_FONT_ID, midY - 35, line);
      renderer.drawCenteredText(UI_10_FONT_ID, midY - 10,
                                phoneConnected ? tr(STR_BT_RX_PHONE_CONNECTED) : tr(STR_BT_RX_OPEN_APP));
      if (state == State::Done) {
        snprintf(line, sizeof(line), "%s (%d)", tr(STR_BT_RX_SAVED), booksReceived);
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 30, line, true, EpdFontFamily::BOLD);
        const std::string name = renderer.truncatedText(UI_10_FONT_ID, savedName.c_str(), textWidth);
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 55, name.c_str());
      } else if (state == State::Failed) {
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 30, tr(STR_BT_RX_FAILED), true, EpdFontFamily::BOLD);
        const std::string reason = renderer.truncatedText(UI_10_FONT_ID, lastError.c_str(), textWidth);
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 55, reason.c_str());
      }
      break;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
