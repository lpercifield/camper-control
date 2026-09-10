#pragma once
#include <NimBLEDevice.h>

#include <string>

#include "BleScanner.h"

#include "ByteRingBuffer.h"

// Ported from Bluedroid to NimBLE - see docs/decisions/0010. The public
// interface is unchanged, so the BMS integration did not have to move with it.

#define BLE_BUFFER_SIZE 512  // was ESP_GATT_MAX_ATTR_LEN, a Bluedroid constant
#define MIN_MTU 50
#define RX_BUFFER_SIZE 4096
#define FLUSH_TIME 1000

// No longer a NimBLEScanCallbacks: BleScanner owns the scan and this is one
// of its listeners. See BleScanner.h.
class BleSerialClient : public NimBLEClientCallbacks,
                        public BleAdvertisementListener,
                        public Stream {
 public:
  BleSerialClient();

  void begin(const char* name, bool enable_led = false, int led_pin = 13);
  // Restrict which BMS we attach to. Pass a lowercase MAC
  // ("a4:c1:38:11:22:33"); empty or null means "first device advertising the
  // JBD service", which is the original behaviour.
  void setTargetAddress(const char* mac);
  void end();

  int available() override;
  int read() override;
  size_t readBytes(uint8_t* buffer, size_t bufferSize);
  int peek() override;
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t* buffer, size_t bufferSize) override;
  void flush() override;
  size_t print(const char* value);

  // NimBLEClientCallbacks
  void onConnect(NimBLEClient* pClient) override;
  void onDisconnect(NimBLEClient* pClient, int reason) override;
  void onConnectFail(NimBLEClient* pClient, int reason) override;
  // BleAdvertisementListener
  void onAdvertisement(const NimBLEAdvertisedDevice* advertisedDevice) override;

  void bleLoop();
  void logConnParams(const char* when);
  bool connectToServer();
  bool connected();
  // Address of the peer we actually connected to, or "" when not connected.
  // Needed to remember the BMS across reboots.
  const char* peerAddress() const { return peerAddress_.c_str(); }

  static ByteRingBuffer<RX_BUFFER_SIZE> receiveBuffer;

  NimBLEClient* pClient = nullptr;
  NimBLERemoteCharacteristic* TxCharacteristic = nullptr;
  NimBLERemoteCharacteristic* RxCharacteristic = nullptr;

  bool enableLed = false;
  int ledPin = 13;

 protected:
  size_t transmitBufferLength = 0;
  bool bleConnected = false;

 private:
  BleSerialClient(BleSerialClient const& other) = delete;
  void operator=(BleSerialClient const& other) = delete;

  NimBLEUUID serviceUUID;
  NimBLEUUID charRxUUID;
  NimBLEUUID charTxUUID;

  // The advertised-device pointer handed to onResult() belongs to the scan
  // results and dies with clearResults(). Keep the address instead and connect
  // by address, which NimBLE supports directly.
  NimBLEAddress foundAddress;
  bool haveFound = false;

  std::string peerAddress_;
  std::string targetAddress;

  size_t numAvailableLines = 0;
  uint8_t transmitBuffer[BLE_BUFFER_SIZE];
  bool doConnect = false;
  uint16_t MTU = 0;
  uint16_t maxTransferSize = BLE_BUFFER_SIZE;
  uint32_t flush_100ms = 0;
  int flush_time = FLUSH_TIME;
  // Connection parameters. NimBLE's defaults are a 30-50 ms interval, which
  // wakes the radio 20-33 times a second to service a BMS we poll twice a
  // second (kRefreshMs is 500). Every one of those events is airtime not
  // available for scanning or for a second connection, and the lighting plan
  // in decisions/0012 wants both.
  //
  // The binding constraint is that the BMS protocol is request/response, so a
  // round trip costs at least two intervals. At 200 ms that is ~400 ms against
  // a 500 ms poll period - it fits, without much room, which is why the
  // ceiling here is 200 and not more.
  //
  // Units: interval is 1.25 ms, timeout is 10 ms.
  static constexpr uint16_t kConnItvlMin = 96;   // 120 ms
  static constexpr uint16_t kConnItvlMax = 160;  // 200 ms
  static constexpr uint16_t kConnLatency = 0;    // pack-powered; answer promptly
  static constexpr uint16_t kConnTimeout = 400;  // 4000 ms
  // Re-read the negotiated parameters once, a little after connecting: a
  // peripheral may renegotiate, and what we asked for is not evidence of what
  // we got.
  uint32_t connParamRecheckMs_ = 0;
  bool connParamRechecked_ = false;

  // HCI 0x3e (connection failed to be established) is common and transient.
  // We already know the address, so retry the connect before paying for a
  // whole rescan cycle.
  static constexpr uint8_t kMaxConnectAttempts = 4;
  uint8_t connectAttempts = 0;
  uint32_t nextConnectMs = 0;

  const char* BLE_SERIAL_SERVICE_UUID = "ff00";
  const char* BLE_RX_UUID = "ff01";
  const char* BLE_TX_UUID = "ff02";
};
