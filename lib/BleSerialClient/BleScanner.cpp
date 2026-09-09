#include "BleScanner.h"

BleScanner& BleScanner::instance() {
  static BleScanner s;
  return s;
}

void BleScanner::begin() {
  if (started_) return;
  pScan_ = NimBLEDevice::getScan();
  pScan_->setScanCallbacks(this, false);
  pScan_->setInterval(1349);
  pScan_->setWindow(449);
  // Active scanning makes us send scan requests, which the sensor has to answer
  // out of its own battery. The BMS is found from its service UUID, which is in
  // the advertisement rather than the scan response, so this could become
  // passive - untested, and noted in ROADMAP.
  pScan_->setActiveScan(true);
  started_ = true;
  lastStartMs_ = 0;  // start one on the next loop() rather than waiting a gap
}

void BleScanner::addListener(BleAdvertisementListener* listener) {
  if (listener == nullptr) return;
  for (auto* l : listeners_) {
    if (l == listener) return;
  }
  listeners_.push_back(listener);
}

void BleScanner::loop() {
  if (!started_) return;

  if (millis() - lastReportMs_ >= 10000) {
    lastReportMs_ = millis();
    log_i("scanner: adv=%u listeners=%u scanning=%d paused=%d",
          (unsigned)advCount_, (unsigned)listeners_.size(),
          (int)(pScan_ != nullptr && pScan_->isScanning()), (int)paused_);
  }

  if (paused_) return;
  if (pScan_->isScanning()) return;
  if (lastStartMs_ != 0 && millis() - lastStartMs_ < kGapMs) return;
  lastStartMs_ = millis();
  pScan_->start(kWindowMs, false, true);
}

void BleScanner::pause() {
  paused_ = true;
  // Stop now rather than letting the window run out. A disconnect that waits
  // for a 5 s window cost 6,192 ms to reconnect against 3,492 ms without any
  // scanning at all; this is the fix for that.
  if (pScan_ != nullptr && pScan_->isScanning()) pScan_->stop();
}

void BleScanner::resume() { paused_ = false; }

bool BleScanner::scanning() const {
  return pScan_ != nullptr && pScan_->isScanning();
}

void BleScanner::onResult(const NimBLEAdvertisedDevice* device) {
  advCount_++;
  for (auto* l : listeners_) l->onAdvertisement(device);
}

void BleScanner::onScanEnd(const NimBLEScanResults& results, int /*reason*/) {
  // Do not restart from in here - that keeps the radio busy on the host task
  // and was patch 3 in the original port. loop() decides when to look again.
  log_d("scan window ended, %d devices, %u advertisements total",
        results.getCount(), (unsigned)advCount_);
  NimBLEDevice::getScan()->clearResults();
}
