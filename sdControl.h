#ifndef _SD_CONTROL_H_
#define _SD_CONTROL_H_

#include "config.h"

// Duration of the runtime SPI bus-protection window.
// When the CS_SENSE interrupt fires (FALLING edge), _spiBlockoutTime is
// set to millis() + SPI_BLOCKOUT_PERIOD. This prevents the ESP from
// initiating new SdFat operations while the host is actively using the bus.
//
// NOTE: this timer is NOT used during the SD initialisation phase.
// Init uses waitForStableBusWindow() instead, which polls CS_SENSE directly
// and is immune to the "timer never expires" problem on MiST FPGA where the
// ARM IO Controller fires the interrupt every few milliseconds.
#define SPI_BLOCKOUT_PERIOD 20000UL

class SDControl {
public:
  SDControl() {}

  // Configure pins and attach the CS_SENSE interrupt.
  // [STEP 1] Applies SD_BOOT_DELAY_MS before returning so the MiST ARM
  //          IO Controller has time to finish its own boot sequence.
  // [STEP 2] Calls waitForStableBusWindow() afterwards so the caller
  //          (network.start() -> dav.init() -> sd.begin()) runs inside
  //          a known-idle SPI bus window.
  static void setup();

  // Acquire the SPI bus: switch all SPI pins to hardware mode and
  // drive SD_CS. Call before every SdFat operation.
  static void takeBusControl();

  // Release the SPI bus: return all SPI pins to INPUT (high-impedance)
  // so the host can drive the bus again. Call after every SdFat operation.
  static void relinquishBusControl();

  // Returns true when it is safe to start a new SdFat operation.
  //
  // Changed for MiST compatibility:
  //   Original: only tested the _spiBlockoutTime timer, which is always
  //             "now + 20 s" on MiST because the interrupt fires constantly.
  //   New:      when the timer is still active, also reads CS_SENSE directly.
  //             If the pin is HIGH right now the bus is momentarily free and
  //             a short single-sector operation can proceed safely.
  static bool canWeTakeBus();

  // [STEP 2] Poll CS_SENSE until it has been HIGH continuously for
  // SD_CS_STABLE_MS ms. Returns true when a stable window is found,
  // false if timeoutMs elapses without finding one.
  // Calls yield() on every iteration to keep the WiFi stack alive.
  static bool waitForStableBusWindow(unsigned long timeoutMs = SD_BUS_WAIT_TIMEOUT_MS);

private:
  static volatile long _spiBlockoutTime;
  static bool          _weTookBus;
};

extern SDControl sdcontrol;

#endif // _SD_CONTROL_H_
