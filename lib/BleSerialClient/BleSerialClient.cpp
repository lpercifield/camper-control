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
  doScan = true;
}

void BleSerialClient::onConnectFail(NimBLEClient* /*client*/, int reason) {
  // NimBLE gives a reason code where Bluedroid gave "Unknown ESP_ERR error".
  log_w("BLE connect failed, reason %d", reason);
}

void BleSerialClient::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
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
  doScan = true;
  NimBLEDevice::getScan()->stop();
}

void BleSerialClient::onScanEnd(const NimBLEScanResults& results, int /*reason*/) {
  // Patch 3: the original restarted the scan from inside this callback, which
  // kept the radio busy even while connected. bleLoop() decides when to look
  // again; all we do here is release the result list.
  log_i("scan complete, %d devices", results.getCount());
  NimBLEDevice::getScan()->clearResults();
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

  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(this, false);
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  lastScanStartMs = millis();
  doScan = true;
  pBLEScan->start(5000, false, true);  // milliseconds in NimBLE 2.x
  log_i("BLE begin exit");
}

bool BleSerialClient::connectToServer() {
  if (!haveFound || pClient == nullptr) return false;
  haveFound = false;

  pClient->setConnectTimeout(5000);

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
  return true;
}

void BleSerialClient::bleLoop() {
  if (millis() - flush_100ms >= (uint32_t)flush_time) flush();

  // NimBLE will not connect while a scan is running, and stop() is
  // asynchronous - it asks the controller to stop and returns. Connecting
  // straight out of onResult() therefore fails every time. Wait for the
  // scan to actually be down.
  if (doConnect && !pBLEScan->isScanning() && millis() >= nextConnectMs) {
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
  }

  if (bleConnected) return;

  // Patch 2: asynchronous rescan, rate limited. The original used the blocking
  // form of start(), freezing the caller for five seconds every pass while the
  // BMS was out of range.
  if (doScan && millis() - lastScanStartMs >= 6000) {
    log_i("ble disconnected, rescanning");
    lastScanStartMs = millis();
    pBLEScan->start(5000, false, true);
  }
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
