#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>

#include <vector>

// -----------------------------------------------------------------------------
// One scanner, many consumers.
//
// NimBLE has exactly one scan object and one set of scan callbacks, so whoever
// calls setScanCallbacks() last wins. BleSerialClient used to claim it, which
// meant a second consumer - a BLE temperature sensor - could not register, and
// worse, the radio stopped scanning entirely once the BMS connected. See
// `docs/ROADMAP.md` design gap 1 for the measurements.
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

  // NimBLE will not connect while a scan is running, and stop() is
  // asynchronous - it asks the controller and returns (`decisions/0010`). So a
  // would-be connector calls pause(), waits for scanning() to go false, and
  // calls resume() when it is done either way.
  void pause();
  void resume();
  bool scanning() const;

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
  bool paused_ = false;
  uint32_t lastStartMs_ = 0;
  uint32_t advCount_ = 0;
  uint32_t lastReportMs_ = 0;
};
