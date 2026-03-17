#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>
#include <EEPROM.h>
#include "pins.h"
#include "config.h"
#include "serial.h"
#include "sdControl.h"

int Config::loadSD() {
  SdFat sdfat;

  SERIAL_ECHOLN("Going to load config from INI file");

  if (!sdcontrol.canWeTakeBus()) {
    SERIAL_ECHOLN("Marlin is controling the bus");
    return -1;
  }
  sdcontrol.takeBusControl();

  // [STEP 3] Low-speed init for reliability on a shared bus
  if (!sdfat.begin(SD_CS, SD_SCK_MHZ(SD_INIT_SPEED_MHZ))) {
    SERIAL_ECHOLN("Initial SD failed");
    sdcontrol.relinquishBusControl();
    return -2;
  }

  // SdFat 3.x: use File32 instead of File (File maps to fs::File in newer cores)
  File32 file = sdfat.open("SETUP.INI", FILE_READ);
  if (!file) {
    SERIAL_ECHOLN("Open INI file failed");
    sdcontrol.relinquishBusControl();
    return -3;
  }

  // Get SSID and PASSWORD from file
  int    rst = 0, step = 0;
  String buffer, sKEY, sValue;
  while (file.available()) {
    buffer = file.readStringUntil('\n');
    if (buffer.length() == 0) continue;
    buffer.replace("\r", "");
    int iS = buffer.indexOf('=');
    if (iS < 0) continue;
    sKEY   = buffer.substring(0, iS);
    sValue = buffer.substring(iS + 1);
    if (sKEY == "SSID") {
      SERIAL_ECHOLN("INI file : SSID found");
      if (sValue.length() > 0) {
        memset(data.ssid, '\0', WIFI_SSID_LEN);
        sValue.toCharArray(data.ssid, WIFI_SSID_LEN);
        step++;
      } else {
        rst = -4;
        goto FAIL;
      }
    } else if (sKEY == "PASSWORD") {
      SERIAL_ECHOLN("INI file : PASSWORD found");
      if (sValue.length() > 0) {
        memset(data.psw, '\0', WIFI_PASSWD_LEN);
        sValue.toCharArray(data.psw, WIFI_PASSWD_LEN);
        step++;
      } else {
        rst = -5;
        goto FAIL;
      }
    } else {
      continue;
    }
  }
  if (step != 2) {
    SERIAL_ECHOLN("Please check your SSDI or PASSWORD in ini file");
    rst = -6;
    goto FAIL;
  }

FAIL:
  file.close();
  sdcontrol.relinquishBusControl();
  return rst;
}

unsigned char Config::load() {
  if (0 == loadSD()) {
    return 1;
  }

  SERIAL_ECHOLN("Going to load config from EEPROM");

  EEPROM.begin(EEPROM_SIZE);
  uint8_t *p = (uint8_t*)(&data);
  for (int i = 0; i < (int)sizeof(data); i++) {
    *(p + i) = EEPROM.read(i);
  }
  EEPROM.commit();

  if (data.flag) {
    SERIAL_ECHOLN("Going to use the old config to connect the network");
  }
  SERIAL_ECHOLN("We didn't connect the network before");
  return data.flag;
}

char* Config::ssid() {
  return data.ssid;
}

void Config::ssid(char* ssid) {
  if (ssid == NULL) return;
  strncpy(data.ssid, ssid, WIFI_SSID_LEN);
}

char* Config::password() {
  return data.psw;
}

void Config::password(char* password) {
  if (password == NULL) return;
  strncpy(data.psw, password, WIFI_PASSWD_LEN);
}

void Config::save(const char* ssid, const char* password) {
  if (ssid == NULL || password == NULL) return;

  EEPROM.begin(EEPROM_SIZE);
  data.flag = 1;
  strncpy(data.ssid, ssid, WIFI_SSID_LEN);
  strncpy(data.psw,  password, WIFI_PASSWD_LEN);
  uint8_t *p = (uint8_t*)(&data);
  for (int i = 0; i < (int)sizeof(data); i++) {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
}

void Config::save() {
  if (data.ssid == NULL || data.psw == NULL) return;

  EEPROM.begin(EEPROM_SIZE);
  data.flag = 1;
  uint8_t *p = (uint8_t*)(&data);
  for (int i = 0; i < (int)sizeof(data); i++) {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
}

// Save IP address to SD card as ip.gcode so the printer/MiST can read it
int Config::save_ip(const char* ip) {
  SdFat sdfat;

  SERIAL_ECHOLN("Going to save config to ip.gcode file");

  if (!sdcontrol.canWeTakeBus()) {
    SERIAL_ECHOLN("Marlin is controling the bus");
    return -1;
  }
  sdcontrol.takeBusControl();

  // [STEP 3] Low-speed init for reliability on a shared bus
  if (!sdfat.begin(SD_CS, SD_SCK_MHZ(SD_INIT_SPEED_MHZ))) {
    SERIAL_ECHOLN("Initial SD failed");
    sdcontrol.relinquishBusControl();
    return -2;
  }

  sdfat.remove("ip.gcode");

  // SdFat 3.x: use File32 instead of File
  File32 file = sdfat.open("ip.gcode", FILE_WRITE);
  if (!file) {
    SERIAL_ECHOLN("Open ip file failed");
    sdcontrol.relinquishBusControl();
    return -3;
  }

  char buf[21] = "M117 ";
  strncat(buf, ip, 15);
  file.write(buf, 21);
  file.close();

  sdcontrol.relinquishBusControl();
  return 0; // missing return fixed
}

Config config;
