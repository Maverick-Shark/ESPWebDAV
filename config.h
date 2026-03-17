#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdio.h>
#include <string.h>

#define WIFI_SSID_LEN   32
#define WIFI_PASSWD_LEN 64

#define EEPROM_SIZE 512

typedef struct config_type
{
  unsigned char flag; // Was saved before?
  char ssid[32];
  char psw[64];
} CONFIG_TYPE;

class Config {
public:
  int loadSD();
  unsigned char load();
  char* ssid();
  void ssid(char* ssid);
  char* password();
  void password(char* password);
  void save(const char* ssid, const char* password);
  void save();
  int save_ip(const char* ip);

protected:
  CONFIG_TYPE data;
};

extern Config config;

// -----------------------------------------------------------------------
// SD Card / SPI bus arbitration — MiST FPGA compatibility
// -----------------------------------------------------------------------
//
// The original firmware was designed for 3D printers (Marlin), where the
// host MCU accesses the SD card infrequently, leaving long idle windows.
// The MiST FPGA ARM IO Controller accesses the SD card almost continuously
// to serve disk images to the running FPGA core. This causes two problems:
//
//  Problem 1 — canWeTakeBus() never returns true (STEP 1 + STEP 2 fix):
//    The CS_SENSE FALLING-edge interrupt fires every few ms and keeps
//    refreshing _spiBlockoutTime to "now + 20 s", so canWeTakeBus()
//    always returns false and sd.begin() is never attempted.
//
//  Problem 2 — sd.begin() fails even when the bus is momentarily free:
//    The call uses SPI_FULL_SPEED (~50 MHz). The SD spec requires
//    initialisation at <= 400 kHz. On a shared bus with the MiST ARM
//    this causes reliable init failures. (STEP 3 fix)
//
// Fix summary:
//   [STEP 1] SD_BOOT_DELAY_MS — wait before the first SPI attempt so the
//            ARM IO Controller can finish its own boot and initial core load.
//   [STEP 2] SD_CS_STABLE_MS / SD_BUS_WAIT_TIMEOUT_MS — poll CS_SENSE
//            directly and require it to stay HIGH for a continuous window
//            before attempting sd.begin(). Background retry in loop() if
//            the first attempt in setup() fails.
//   [STEP 3] SD_INIT_SPEED_MHZ — use a safe low speed for sd.begin().
//            SdFat automatically negotiates a higher speed afterwards.
// -----------------------------------------------------------------------

// [STEP 1] Milliseconds to wait at startup before touching the SPI bus.
// Increase to 12000-15000 if the problem persists on your MiST setup.
#define SD_BOOT_DELAY_MS        10000UL

// [STEP 2] CS_SENSE must remain HIGH (bus idle) for this many consecutive
// milliseconds before an sd.begin() attempt is allowed.
#define SD_CS_STABLE_MS         800UL

// Maximum time to wait for a stable window on each attempt.
#define SD_BUS_WAIT_TIMEOUT_MS  15000UL

// Interval between background sd.begin() retries in loop().
#define SD_INIT_RETRY_INTERVAL  5000UL

// [STEP 3] SPI clock speed for sd.begin(). Must be <= 1 MHz for reliable
// initialisation on a shared/noisy bus. 0 = 400 kHz (SD spec minimum).
#define SD_INIT_SPEED_MHZ       1

// SPI clock speed for normal read/write after a successful init.
#define SD_OPER_SPEED_MHZ       4

#endif // _CONFIG_H_
