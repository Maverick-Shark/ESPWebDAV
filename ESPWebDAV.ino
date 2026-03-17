// Using the WebDAV server with a 3D printer (Marlin) or MiST FPGA.
// Printer controller is a variation of Rambo running Marlin firmware.
//
// MiST FPGA compatibility changes:
//   setup(): sdcontrol.setup() now applies SD_BOOT_DELAY_MS [STEP 1] and
//            waits for a stable SPI bus window [STEP 2] before returning,
//            so network.start() -> dav.init() -> sd.begin() runs inside a
//            known-idle window at SD_INIT_SPEED_MHZ [STEP 3].
//   loop():  [STEP 2] background retry — if dav.init() failed (dav.sdMounted
//            is false), dav.reinitSD() is called every SD_INIT_RETRY_INTERVAL
//            ms until the SD card is successfully initialised.

#include "serial.h"
#include "parser.h"
#include "config.h"
#include "network.h"
#include "gcode.h"
#include "sdControl.h"

// LED is connected to GPIO2 on this board
#define INIT_LED  { pinMode(2, OUTPUT); }
#define LED_ON    { digitalWrite(2, LOW); }
#define LED_OFF   { digitalWrite(2, HIGH); }

// ------------------------
void setup() {
// ------------------------
  SERIAL_INIT(115200);
  INIT_LED;
  blink();

  // sdcontrol.setup() now:
  //   • attaches the CS_SENSE interrupt (unchanged)
  //   • [STEP 1] waits SD_BOOT_DELAY_MS ms (yield-based, WiFi stays alive)
  //   • [STEP 2] calls waitForStableBusWindow() so the bus is idle when
  //              network.start() -> dav.init() -> sd.begin() executes next
  sdcontrol.setup();

  // ----- WiFi + WebDAV + SD init -----
  if (config.load() == 1) { // previously saved credentials exist
    if (!network.start()) {
      SERIAL_ECHOLN("Connect fail, please check your INI file or set the wifi config and connect again");
      SERIAL_ECHOLN("- M50: Set the wifi ssid , 'M50 ssid-name'");
      SERIAL_ECHOLN("- M51: Set the wifi password , 'M51 password'");
      SERIAL_ECHOLN("- M52: Start to connect the wifi");
      SERIAL_ECHOLN("- M53: Check the connection status");
    }
  } else {
    SERIAL_ECHOLN("Welcome to FYSETC: www.fysetc.com");
    SERIAL_ECHOLN("Please set the wifi config first");
    SERIAL_ECHOLN("- M50: Set the wifi ssid , 'M50 ssid-name'");
    SERIAL_ECHOLN("- M51: Set the wifi password , 'M51 password'");
    SERIAL_ECHOLN("- M52: Start to connect the wifi");
    SERIAL_ECHOLN("- M53: Check the connection status");
  }
}

// ------------------------
void loop() {
// ------------------------
  // Handle WebDAV / network requests
  network.handle();

  // Handle serial gcode commands (M50 / M51 / M52 / M53)
  gcode.Handle();

  // [STEP 2] Background SD re-init retry ─────────────────────────────────
  // If sd.begin() failed during setup() (e.g. the MiST ARM IO Controller
  // was still actively using the SPI bus), retry here every
  // SD_INIT_RETRY_INTERVAL ms.
  // dav.reinitSD() calls waitForStableBusWindow(), takeBusControl(),
  // sd.begin() at SD_INIT_SPEED_MHZ [STEP 3], and relinquishBusControl().
  // While dav.sdMounted is false, handleRequest() returns 503 so the
  // WebDAV client retries automatically.
  if (!dav.sdMounted) {
    unsigned long now = millis();
    if (now - dav.lastSdInitAttemptMs >= SD_INIT_RETRY_INTERVAL) {
      if (dav.reinitSD()) {
        SERIAL_ECHOLN("SD Card mounted in background — WebDAV fully operational.");
      }
    }
  }

  // Status LED blink
  statusBlink();
}

// ------------------------
void blink() {
// ------------------------
  LED_ON;
  delay(100);
  LED_OFF;
  delay(400);
}

// ------------------------
void errorBlink() {
// ------------------------
  for (int i = 0; i < 100; i++) {
    LED_ON;
    delay(50);
    LED_OFF;
    delay(50);
  }
}

// ------------------------
void statusBlink() {
// ------------------------
  static unsigned long time = 0;
  if (millis() > time + 1000) {
    if (network.isConnecting()) {
      LED_OFF;
    } else if (network.isConnected()) {
      LED_ON;
      delay(50);
      LED_OFF;
    } else {
      LED_ON;
    }
    time = millis();
  }
}
