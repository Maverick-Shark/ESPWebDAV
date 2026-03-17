#include <ESP8266WiFi.h>
#include <SdFat.h>
#include "config.h"

#define DEBUG

#ifdef DEBUG
  #define DBG_PRINT(...)   { Serial.print(__VA_ARGS__); }
  #define DBG_PRINTLN(...) { Serial.println(__VA_ARGS__); }
#else
  #define DBG_PRINT(...)   {}
  #define DBG_PRINTLN(...) {}
#endif

// Constants for WebServer
#define CONTENT_LENGTH_UNKNOWN ((size_t) -1)
#define CONTENT_LENGTH_NOT_SET ((size_t) -2)
#define HTTP_MAX_POST_WAIT     5000

enum ResourceType { RESOURCE_NONE, RESOURCE_FILE, RESOURCE_DIR };
enum DepthType    { DEPTH_NONE, DEPTH_CHILD, DEPTH_ALL };


class ESPWebDAV {
public:
  // Start the WiFi server and initialise the SD card.
  // [STEP 3] spiSettings is forwarded to sd.begin(); callers should pass
  //          SD_SCK_MHZ(SD_INIT_SPEED_MHZ) instead of SPI_FULL_SPEED so
  //          the SD init sequence runs at a safe low speed on a shared bus.
  bool init(int chipSelectPin, SPISettings spiSettings, int serverPort);

  // Initialise the SD card only (server already started separately).
  // [STEP 3] Same note as init(): use SD_SCK_MHZ(SD_INIT_SPEED_MHZ).
  bool initSD(int chipSelectPin, SPISettings spiSettings);

  bool startServer();
  bool isClientWaiting();
  void handleClient(String blank = "");
  void rejectClient(String rejectMessage);

  // ── MiST FPGA background SD re-init support ─────────────────────────────
  //
  // sdMounted: true after a successful sd.begin(). Set by init() / reinitSD().
  // loop() in ESPWebDAV.ino checks this flag and calls reinitSD() periodically
  // until the SD card is available.
  bool sdMounted;

  // Timestamp of the last reinitSD() attempt, used in loop() to pace retries
  // at SD_INIT_RETRY_INTERVAL ms.
  unsigned long lastSdInitAttemptMs;

  // [STEP 2 — retry hook] Called from loop() every SD_INIT_RETRY_INTERVAL ms
  // while sdMounted is false. Waits for a stable SPI bus window, then calls
  // sd.begin() at SD_INIT_SPEED_MHZ [STEP 3]. Updates sdMounted.
  // Returns true if the SD card was successfully initialised.
  bool reinitSD();

protected:
  typedef void (ESPWebDAV::*THandlerFunction)(String);

  void processClient(THandlerFunction handler, String message);
  void handleNotFound();
  void handleReject(String rejectMessage);
  void handleRequest(String blank);
  void handleOptions(ResourceType resource);
  void handleLock(ResourceType resource);
  void handleUnlock(ResourceType resource);
  void handlePropPatch(ResourceType resource);
  void handleProp(ResourceType resource);
  void sendPropResponse(boolean recursing, FatFile *curFile);
  void handleGet(ResourceType resource, bool isGet);
  void handlePut(ResourceType resource);
  void handleWriteError(String message, FatFile *wFile);
  void handleDirectoryCreate(ResourceType resource);
  void handleMove(ResourceType resource);
  void handleDelete(ResourceType resource);

  // Sections copied from ESP8266WebServer
  String getMimeType(String path);
  String urlDecode(const String& text);
  String urlToUri(String url);
  bool   parseRequest();
  void   sendHeader(const String& name, const String& value, bool first = false);
  void   send(String code, const char* content_type, const String& content);
  void   _prepareHeader(String& response, String code, const char* content_type, size_t contentLength);
  void   sendContent(const String& content);
  void   sendContent_P(PGM_P content);
  void   setContentLength(size_t len);
  size_t readBytesWithTimeout(uint8_t *buf, size_t bufSize);
  size_t readBytesWithTimeout(uint8_t *buf, size_t bufSize, size_t numToRead);

  // Variables pertaining to the HTTP request currently being serviced
  WiFiServer *server;
  SdFat       sd;

  WiFiClient client;
  String     method;
  String     uri;
  String     contentLengthHeader;
  String     depthHeader;
  String     hostHeader;
  String     destinationHeader;

  String _responseHeaders;
  bool   _chunked;
  int    _contentLength;
};

extern ESPWebDAV dav;
