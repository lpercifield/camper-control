#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>

#include <atomic>

#include <vector>

// -----------------------------------------------------------------------------
// One scanner, many consumers.
//
// NimBLE has exactly one scan object and one set of scan callbacks, so whoever
// calls setScanCallbacks() last wins. BleSerialClient used to claim it, which
// meant a second consumer - a BLE temperature sensor - could not register, and
// worse, the radio stopped scanning entirely once the BMS connected. See
// `docs/ARCHITECTURE.md` under BleScanner for the measurements.
//
// This owns the scan instead and fans advertisements out to listeners. It keeps
// scanning while a connection is up, which the controller time-slices happily:
// measured at ~4 advertisements/sec delivered with the BMS online, and no
// effect on the cooperative loop.
//
// It lives in this library rather than src/ because BleSerialClient is a
// library and has to include it; PlatformIO builds libraries in isolation.
// -----------------------------------------------------------------------------

class BleAdvertisementListener {
 public:
  virtual ~BleAdvertisementListener() = default;
  // Called from the NimBLE host task for every advertisement seen. The device
  // pointer belongs to the scan results and does not outlive this call - copy
  // anything you need to keep.
  virtual void onAdvertisement(const NimBLEAdvertisedDevice* device) = 0;
};

class BleScanner : public NimBLEScanCallbacks {
 public:
  static BleScanner& instance();

  // Must be called after NimBLEDevice::init(). Idempotent.
  void begin();
  // Safe before begin(); listeners are kept either way.
  void addListener(BleAdvertisementListener* listener);

  // Keeps the radio scanning. Call every pass of whichever loop owns BLE.
  void loop();

  // Who is asking the scanner to stand down. One bit per connector, because
  // there is more than one: the BMS task and the lighting task both connect,
  // and a single shared flag let either of them resume a scan the other was
  // relying on staying down. Lock-free rather than guarded, since a bitmask
  // set and cleared atomically needs no critical section.
  static constexpr uint8_t kPauseBms = 1 << 0;
  static constexpr uint8_t kPauseLighting = 1 << 1;

  // NimBLE will not connect while a scan is running, and stop() is
  // asynchronous - it asks the controller and returns (`decisions/0010`). So a
  // would-be connector calls pause(), waits for scanning() to go false, and
  // calls resume() when it is done either way.
  //
  // **Pause for the attempt, not for the whole retry cycle.** Holding it down
  // across four attempts at a 5 s timeout starves every other listener for
  // twenty seconds; see `ARCHITECTURE.md`.
  //
  // Both are idempotent, which matters: callers resume on every pass of their
  // loop rather than tracking whether they had paused.
  void pause(uint8_t owner);
  void resume(uint8_t owner);
  bool scanning() const;
  // True while anybody is holding it down. Diagnostics only.
  bool paused() const { return pauseMask_.load() != 0; }

 private:
  BleScanner() = default;
  BleScanner(const BleScanner&) = delete;
  void operator=(const BleScanner&) = delete;

  void onResult(const NimBLEAdvertisedDevice* device) override;
  void onScanEnd(const NimBLEScanResults& results, int reason) override;

  // 5 s windows with a short gap. The gap is not a rate limit - it exists only
  // so a pause() landing mid-window is noticed promptly.
  static constexpr uint32_t kWindowMs = 5000;
  static constexpr uint32_t kGapMs = 200;

  NimBLEScan* pScan_ = nullptr;
  std::vector<BleAdvertisementListener*> listeners_;
  bool started_ = false;
  std::atomic<uint8_t> pauseMask_{0};
  uint32_t lastStartMs_ = 0;
  uint32_t advCount_ = 0;
  uint32_t lastReportMs_ = 0;
};
