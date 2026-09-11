#pragma once
#include <string>
#include <vector>

#include "reader/wifi_radio.h"

namespace reader {

// A RADIO THE DESKTOP CAN DRIVE, and the reason WifiRadio is an interface at
// all: you cannot ask a real router to answer NO_AP_FOUND on demand, so two of
// WifiError's three shapes would otherwise ship having never been reached from
// the state they exist for.
//
// It is poll-shaped like the real one, and it does NOT advance itself: a test
// calls finishScan()/succeedJoin()/failJoin() at the moment it wants, which is
// what lets a case pin what happens to a press that arrives mid-scan.
//
// fake_fs.h's shape -- an in-memory implementation with injectable failure --
// for fake_fs.h's reason.
struct FakeWifiRadio : WifiRadio {
  // What a completed scan will report, raw and in radio order.
  std::vector<ScanResult> seen;
  // Make beginScan()/beginJoin() refuse outright: the radio would not come up.
  bool refuseScan = false;
  bool refuseJoin = false;

  // Observations, so a test can assert the flow did what it claimed.
  int scansStarted = 0;
  int joinsStarted = 0;
  int downCalls = 0;
  std::string lastSsid;
  std::string lastPsk;

  bool beginScan() override {
    if (refuseScan) return false;
    ++scansStarted;
    scan_ = ScanState::Running;
    results_.clear();
    return true;
  }
  ScanState scanState() const override { return scan_; }
  const std::vector<ScanResult>& scanResults() const override { return results_; }

  bool beginJoin(std::string_view ssid, std::string_view psk) override {
    if (refuseJoin) return false;
    ++joinsStarted;
    lastSsid = std::string(ssid);
    lastPsk = std::string(psk);
    join_ = JoinState::Running;
    reason_ = 0;
    return true;
  }
  JoinState joinState() const override { return join_; }
  int joinReason() const override { return reason_; }

  void down() override {
    ++downCalls;
    scan_ = ScanState::Idle;
    join_ = JoinState::Idle;
  }

  // --- the levers a test pulls -------------------------------------------
  void finishScan() {
    results_ = seen;
    scan_ = ScanState::Done;
  }
  void failScan() {
    results_.clear();
    scan_ = ScanState::Failed;
  }
  void succeedJoin() { join_ = JoinState::Ok; }
  void failJoin(int reason) {
    join_ = JoinState::Failed;
    reason_ = reason;
  }

 private:
  ScanState scan_ = ScanState::Idle;
  JoinState join_ = JoinState::Idle;
  int reason_ = 0;
  std::vector<ScanResult> results_;
};

}  // namespace reader
