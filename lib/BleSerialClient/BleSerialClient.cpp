#include "BleSerialClient.h"

// Ported from Bluedroid to NimBLE. Every Camper Control change made during the
// original port is preserved here - the numbered list in
// docs/PORTING_NOTES.md still applies. See docs/decisions/0010.

ByteRingBuffer<RX_BUFFER_SIZE> BleSerialClient::receiveBuffer;

static void notifyCallback(NimBLERemoteCharacteristic* /*characteristic*/,
                           uint8_t* pData, size_t length, bool /*isNotify*/) {
  // Deliberately quiet: this fires for every BMS frame, several times a second.
  // Logging per byte here is what flooded the UART and throttled the loop.
  for (size_t i = 0; i < length; i++) {
    BleSerialClient::receiveBuffer.add(pData[i]);
  }
}

BleSerialClient::BleSerialClient() {}

// OFF, and deliberately so. Asking the BMS for a 200 ms interval works - it
// accepts, and the negotiated values come back 200 ms / 4000 ms - but measured
// on hardware 2026-09-10 it made advertisement reception *worse*, from a mean
// 4.93/s across two runs to 4.03/s across two runs. The prediction was that
// waking the radio 4x less often would free airtime for scanning; it did not,
// and the mechanism is not understood.
//
// Left in, off, because the lever is proven and the lighting plan
// (decisions/0012) is the case it was meant for: with two more connections
// contending there may be a trade worth making, and it can be measured then
// rather than assumed now. Flip to 1 to re-run the comparison.
#define CONN_PARAM_TUNING 0

void BleSerialClient::onConnect(NimBLEClient* /*client*/) {
  log_i("BLE connected");
  bleConnected = true;
  if (enableLed) digitalWrite(ledPin, HIGH);
}

void BleSerialClient::onDisconnect(NimBLEClient* /*client*/, int reason) {
  // Patch 1: the original slept five seconds inside this callback, which is a
  // BLE stack context. Never block here.
  log_i("BLE disconnected, reason %d", reason);
  bleConnected = false;
  if (enableLed) digitalWrite(ledPin, LOW);
  receiveBuffer.clear();
  // Patch 1 continued: drop the characteristic pointers rather than leaving
  // them dangling until the next connect.
  TxCharacteristic = nullptr;
  RxCharacteristic = nullptr;
  peerAddress_.clear();
  // Let the scanner look again. It is the only thing that will re-find the BMS
  // now, since this class no longer drives the scan itself.
  BleScanner::instance().resume();
}

void BleSerialClient::onConnectFail(NimBLEClient* /*client*/, int reason) {
  // NimBLE gives a reason code where Bluedroid gave "Unknown ESP_ERR error".
  log_w("BLE connect failed, reason %d", reason);
}

void BleSerialClient::onAdvertisement(const NimBLEAdvertisedDevice* advertisedDevice) {
  if (bleConnected) return;  // already have our battery
  if (!advertisedDevice->haveServiceUUID()) return;
  if (!advertisedDevice->isAdvertisingService(serviceUUID)) return;

  // Patch 5: pin to one battery. Empty target means take the first.
  std::string found = advertisedDevice->getAddress().toString().c_str();
  for (auto& ch : found) ch = tolower(ch);
  if (!targetAddress.empty() && found != targetAddress) {
    log_i("ignoring BMS %s, waiting for %s", found.c_str(), targetAddress.c_str());
    return;
  }

  foundAddress = advertisedDevice->getAddress();
  haveFound = true;
  doConnect = true;
  // Ask the scanner to stand down so the connect can proceed; bleLoop waits
  // for it to actually be down before calling connect().
  BleScanner::instance().pause();
}

void BleSerialClient::setTargetAddress(const char* mac) {
  targetAddress.clear();
  if (mac == nullptr) return;
  for (const char* p = mac; *p; ++p) targetAddress.push_back(tolower(*p));
}

void BleSerialClient::begin(const char* name, bool enable_led, int led_pin) {
  log_i("BLE begin");
  enableLed = enable_led;
  ledPin = led_pin;
  if (enableLed) pinMode(ledPin, OUTPUT);

  serviceUUID = NimBLEUUID(BLE_SERIAL_SERVICE_UUID);
  charRxUUID = NimBLEUUID(BLE_RX_UUID);
  charTxUUID = NimBLEUUID(BLE_TX_UUID);

  NimBLEDevice::init(name ? name : "");
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(this, false);

  BleScanner::instance().begin();
  BleScanner::instance().addListener(this);
  log_i("BLE begin exit");
}

bool BleSerialClient::connectToServer() {
  if (!haveFound || pClient == nullptr) return false;
  haveFound = false;

  pClient->setConnectTimeout(5000);

#if CONN_PARAM_TUNING
  // Asked for before connecting, so the link comes up on these rather than
  // negotiating twice. This is a request: the peripheral may refuse it, which
  // is exactly why the negotiated values are logged below rather than assumed.
  pClient->setConnectionParams(kConnItvlMin, kConnItvlMax, kConnLatency,
                               kConnTimeout);
#endif

  // Patch 6: the original discarded this result and logged success regardless.
  // A failed connect then called getService() on a dead handle, which blocks
  // waiting for a discovery event that never arrives.
  if (!pClient->connect(foundAddress)) {
    log_w("BLE connect failed, will retry");
    return false;
  }
  peerAddress_ = foundAddress.toString().c_str();
  log_i("connected to %s", peerAddress_.c_str());

  NimBLERemoteService* service = pClient->getService(serviceUUID);
  if (service == nullptr) {
    log_w("service %s not found", serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }

  RxCharacteristic = service->getCharacteristic(charRxUUID);
  if (RxCharacteristic == nullptr) {
    log_w("rx characteristic not found");
    pClient->disconnect();
    return false;
  }
  if (RxCharacteristic->canNotify()) {
    RxCharacteristic->subscribe(true, notifyCallback);
  }

  TxCharacteristic = service->getCharacteristic(charTxUUID);
  if (TxCharacteristic == nullptr) {
    log_w("tx characteristic not found");
    pClient->disconnect();
    return false;
  }

  bleConnected = true;
  logConnParams("negotiated");
  connParamRecheckMs_ = millis() + 10000;
  connParamRechecked_ = false;
  return true;
}

void BleSerialClient::logConnParams(const char* when) {
  if (pClient == nullptr) return;
  NimBLEConnInfo info = pClient->getConnInfo();
  log_i("conn params %s: interval %.1f ms, latency %u, timeout %u ms, mtu %u",
        when, info.getConnInterval() * 1.25f, (unsigned)info.getConnLatency(),
        (unsigned)info.getConnTimeout() * 10, (unsigned)info.getMTU());
}

void BleSerialClient::bleLoop() {
  // Whoever drives this loop drives the scanner too. The BMS task is the only
  // caller today; if that ever stops being true this needs its own home.
  BleScanner::instance().loop();

  if (millis() - flush_100ms >= (uint32_t)flush_time) flush();

  if (bleConnected && !connParamRechecked_ && connParamRecheckMs_ != 0 &&
      millis() >= connParamRecheckMs_) {
    connParamRechecked_ = true;
    logConnParams("after 10 s");
  }

  if (!doConnect) {
    BleScanner::instance().resume();
    return;
  }

  // NimBLE will not connect while a scan is running, and stop() is
  // asynchronous - it asks the controller to stop and returns. Connecting
  // straight out of the advertisement callback therefore fails every time.
  // Wait for the scan to actually be down.
  BleScanner::instance().pause();
  if (BleScanner::instance().scanning() || millis() < nextConnectMs) return;

  if (connectToServer()) {
    doConnect = false;
    connectAttempts = 0;
    receiveBuffer.clear();
  } else if (++connectAttempts < kMaxConnectAttempts) {
    // Keep the address and try again shortly. A rescan costs six seconds and
    // tells us nothing we do not already know.
    nextConnectMs = millis() + 400;
    haveFound = true;
  } else {
    doConnect = false;
    connectAttempts = 0;  // give up on this address and look again
  }

  if (!doConnect) BleScanner::instance().resume();
}

bool BleSerialClient::connected() { return bleConnected; }

int BleSerialClient::read() {
  uint8_t result = receiveBuffer.pop();
  if (result == (uint8_t)'\n' && numAvailableLines > 0) numAvailableLines--;
  return result;
}

size_t BleSerialClient::readBytes(uint8_t* buffer, size_t bufferSize) {
  size_t i = 0;
  while (i < bufferSize && available()) {
    buffer[i] = receiveBuffer.pop();
    i++;
  }
  return i;
}

int BleSerialClient::peek() {
  if (receiveBuffer.getLength() == 0) return -1;
  return receiveBuffer.get(0);
}

int BleSerialClient::available() { return receiveBuffer.getLength(); }

size_t BleSerialClient::print(const char* str) {
  if (!bleConnected) return 0;
  size_t written = 0;
  for (size_t i = 0; str[i] != '\0'; i++) written += write((uint8_t)str[i]);
  if (transmitBufferLength >= maxTransferSize ||
      millis() - flush_100ms >= (uint32_t)flush_time) {
    flush();
  }
  return written;
}

size_t BleSerialClient::write(const uint8_t* buffer, size_t bufferSize) {
  if (!bleConnected || pClient == nullptr || !pClient->isConnected()) return 0;

  if (maxTransferSize < MIN_MTU) {
    const uint16_t previous = maxTransferSize;
    MTU = pClient->getMTU() - 5;
    maxTransferSize = MTU > BLE_BUFFER_SIZE ? BLE_BUFFER_SIZE : MTU;
    if (maxTransferSize != previous) {
      log_i("max BLE transfer size %u", maxTransferSize);
    }
    if (maxTransferSize < MIN_MTU) return 0;
  }

  size_t written = 0;
  for (size_t i = 0; i < bufferSize; i++) written += write(buffer[i]);
  if (transmitBufferLength >= maxTransferSize ||
      millis() - flush_100ms >= (uint32_t)flush_time) {
    flush();
  }
  return written;
}

size_t BleSerialClient::write(uint8_t byte) {
  if (!bleConnected) return 0;
  if (transmitBufferLength >= sizeof(transmitBuffer)) return 0;
  transmitBuffer[transmitBufferLength++] = byte;
  return 1;
}

void BleSerialClient::flush() {
  // Patch 4: the original wrote without checking the characteristic was still
  // valid. If we have lost the link, drop the partial frame rather than
  // dereferencing a stale pointer.
  if (transmitBufferLength > 0) {
    if (TxCharacteristic != nullptr && bleConnected) {
      TxCharacteristic->writeValue(transmitBuffer, transmitBufferLength, false);
    }
    transmitBufferLength = 0;
  }
  flush_100ms = millis();
}

void BleSerialClient::end() { NimBLEDevice::deinit(true); }
