#include <ESP8266WiFi.h>
#include "sdControl.h"
#include "pins.h"

// ── Static member definitions ────────────────────────────────────────────────
volatile long SDControl::_spiBlockoutTime = 0;
bool          SDControl::_weTookBus       = false;

// ────────────────────────────────────────────────────────────────────────────
// waitForStableBusWindow()
//
// [STEP 2] Poll CS_SENSE directly until the pin stays HIGH continuously
// for SD_CS_STABLE_MS ms without interruption. This replaces the blind
// canWeTakeBus() check for the SD initialisation phase.
//
// Why direct polling instead of the interrupt timer:
//   The MiST FPGA ARM IO Controller accesses the SD card very frequently
//   (serving disk image sectors to the running FPGA core). Each FALLING
//   edge on CS_SENSE re-arms _spiBlockoutTime to "millis() + 20000", so
//   the timer never expires and canWeTakeBus() always returns false.
//   Direct polling of the pin detects genuine inter-access windows that
//   are too brief to reset the 20 s timer but long enough to run sd.begin().
//
// yield() is called on every iteration so the WiFi/TCP stack does not
// starve during potentially long waits.
// ────────────────────────────────────────────────────────────────────────────
bool SDControl::waitForStableBusWindow(unsigned long timeoutMs) {
  unsigned long deadline   = millis() + timeoutMs;
  unsigned long stableFrom = 0;
  bool          wasFree    = false;

  while (millis() < deadline) {
    yield(); // keep WiFi stack alive while waiting

    if (digitalRead(CS_SENSE) == HIGH) {
      if (!wasFree) {
        // CS just rose: begin measuring how long it stays HIGH
        stableFrom = millis();
        wasFree    = true;
      } else if ((millis() - stableFrom) >= SD_CS_STABLE_MS) {
        // CS has been HIGH uninterrupted long enough — window is valid
        return true;
      }
    } else {
      // CS fell again: host is active, reset the stability counter
      if (wasFree) {
        wasFree = false;
      }
      delay(5); // brief pause to reduce CPU spin while host is busy
    }
  }

  return false; // timed out without finding a stable window
}

// ────────────────────────────────────────────────────────────────────────────
// setup()
//
// Original behaviour: attach CS_SENSE interrupt, then delay(SPI_BLOCKOUT_PERIOD)
// (a 20 s blind wait). This was sufficient for Marlin 3D printers because the
// printer MCU leaves the SPI bus idle for long stretches between layers.
//
// Problem with MiST FPGA:
//   1. The 20 s delay finishes, but the ARM is still actively loading the
//      initial FPGA core, so _spiBlockoutTime is already refreshed.
//   2. canWeTakeBus() returns false → network.start() → dav.init() skips
//      or aborts sd.begin() → "Failed to initialize SD Card".
//
// [STEP 1] Replace the blind delay with a yield()-based loop of
//   SD_BOOT_DELAY_MS ms. The WiFi stack stays responsive during the wait.
//
// [STEP 2] After the boot delay, call waitForStableBusWindow() so that
//   by the time setup() returns the bus is known-idle. The caller
//   (network.start() → startDAVServer() → dav.init() → sd.begin()) then
//   runs inside that quiet window.
// ────────────────────────────────────────────────────────────────────────────
void SDControl::setup() {
  // --- GPIO ---
  // Detect when another SPI master (printer MCU, MiST ARM, …) uses the bus.
  pinMode(CS_SENSE, INPUT);
  attachInterrupt(CS_SENSE, []() {
    if (!_weTookBus)
      _spiBlockoutTime = millis() + SPI_BLOCKOUT_PERIOD;
  }, FALLING);

  // [STEP 1] Boot delay — yield()-based so WiFi stays alive ─────────────────
  // Give the host (MiST ARM IO Controller or Marlin MCU) time to finish its
  // own startup before the ESP competes for the SPI bus.
  {
    unsigned long t0 = millis();
    while (millis() - t0 < SD_BOOT_DELAY_MS) {
      yield();
      delay(100);
    }
  }

  // [STEP 2] Wait for a stable SPI bus window ───────────────────────────────
  // CS_SENSE must stay HIGH for SD_CS_STABLE_MS ms before we return.
  // If no window is found within SD_BUS_WAIT_TIMEOUT_MS the function returns
  // anyway; the background retry in loop() will reattempt sd.begin() later.
  waitForStableBusWindow(SD_BUS_WAIT_TIMEOUT_MS);
}

// ────────────────────────────────────────────────────────────────────────────
// takeBusControl()
//
// Switch SPI pins from INPUT (high-impedance) to hardware SPI mode and
// assert SD_CS (OUTPUT). Must be called before every SdFat operation.
// ────────────────────────────────────────────────────────────────────────────
void SDControl::takeBusControl() {
  _weTookBus = true;
  pinMode(MISO_PIN, SPECIAL);
  pinMode(MOSI_PIN, SPECIAL);
  pinMode(SCLK_PIN, SPECIAL);
  pinMode(SD_CS,    OUTPUT);
}

// ────────────────────────────────────────────────────────────────────────────
// relinquishBusControl()
//
// Return all SPI pins to INPUT so the host can drive the bus freely.
// Must be called after every SdFat operation.
// ────────────────────────────────────────────────────────────────────────────
void SDControl::relinquishBusControl() {
  pinMode(MISO_PIN, INPUT);
  pinMode(MOSI_PIN, INPUT);
  pinMode(SCLK_PIN, INPUT);
  pinMode(SD_CS,    INPUT);
  _weTookBus = false;
}

// ────────────────────────────────────────────────────────────────────────────
// canWeTakeBus()
//
// Returns true when the ESP may safely start a new SdFat operation.
//
// Changed for MiST compatibility:
//   Original: only tested _spiBlockoutTime. On MiST this timer is
//             permanently "now + 20 s" because the CS_SENSE interrupt
//             fires every few ms, so the function always returned false.
//   New:      when the timer is still active, fall back to a direct pin
//             read. If CS_SENSE is HIGH right now the bus is momentarily
//             free and a single-sector read/write operation can proceed.
//             This does NOT apply to sd.begin() (init phase); use
//             waitForStableBusWindow() for that instead.
// ────────────────────────────────────────────────────────────────────────────
bool SDControl::canWeTakeBus() {
  // Fast path: blockout timer has expired — bus is definitely free
  if (millis() >= (unsigned long)_spiBlockoutTime) {
    return true;
  }
  // Slow path (MiST): timer still active but check pin directly.
  // HIGH means the host is not asserting CS at this instant.
  return (digitalRead(CS_SENSE) == HIGH);
}
