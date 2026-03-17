// WebDAV server using ESP8266 and SD card filesystem
// Targeting Windows 7 Explorer WebDav
//
// Changes applied for ESP8266 core 3.x / ESP8266SdFat compatibility:
//   • init() / initSD() now take uint32_t spiSpeed instead of SPISettings
//     (SD_SCK_MHZ() returns uint32_t in this SdFat version, not SPISettings)
//   • sd.vwd() is private in SdFat 3.x — replaced with direct open() calls
//   • dir_t → DirFat_t; FAT_HOUR/MIN/SEC/YEAR/MONTH/DAY → FS_* equivalents
//   • writeStart() now takes only 1 argument (sector) in SdFat 3.x
//   • startServer() missing return value fixed
//
// MiST FPGA compatibility changes:
//   [STEP 2] reinitSD(): background retry hook called from loop()
//   [STEP 3] sd.begin() always uses SD_INIT_SPEED_MHZ (1 MHz) regardless
//            of the spiSpeed argument; the caller-supplied value is ignored

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>
#include <Hash.h>
#include <time.h>
#include "ESPWebDAV.h"
#include "sdControl.h"
#include "config.h"
#include "pins.h"

// Calendar string constants
const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                        "Jul","Aug","Sep","Oct","Nov","Dec"};
const char *wdays[]  = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};


// ────────────────────────────────────────────────────────────────────────────
// init()
//
// Start the WiFi server and initialise the SD card.
//
// spiSpeed is accepted for API compatibility but intentionally ignored.
// [STEP 3] sd.begin() always uses SD_INIT_SPEED_MHZ (1 MHz) to satisfy the
// SD spec init requirement (<= 400 kHz / 1 MHz) and for reliability on the
// shared SPI bus of the MiST FPGA. SdFat negotiates a higher speed after init.
// ────────────────────────────────────────────────────────────────────────────
bool ESPWebDAV::init(int chipSelectPin, uint32_t spiSpeed, int serverPort) {
  sdMounted           = false;
  lastSdInitAttemptMs = 0;

  // Start the WiFi server
  server = new WiFiServer(serverPort);
  server->begin();

  // [STEP 3] Low-speed init regardless of caller-supplied spiSpeed
  sdMounted = sd.begin(chipSelectPin, SD_SCK_MHZ(SD_INIT_SPEED_MHZ));
  return sdMounted;
}

// ────────────────────────────────────────────────────────────────────────────
// initSD() — initialise SD card only (server started separately)
// [STEP 3] Same low-speed init as init().
// ────────────────────────────────────────────────────────────────────────────
bool ESPWebDAV::initSD(int chipSelectPin, uint32_t spiSpeed) {
  // [STEP 3] Low-speed init for reliability on a shared bus
  sdMounted = sd.begin(chipSelectPin, SD_SCK_MHZ(SD_INIT_SPEED_MHZ));
  return sdMounted;
}

// ------------------------
bool ESPWebDAV::startServer() {
// ------------------------
  server->begin();
  return true; // missing return fixed
}

// ────────────────────────────────────────────────────────────────────────────
// reinitSD()
//
// [STEP 2 — background retry hook] Called from loop() every
// SD_INIT_RETRY_INTERVAL ms while sdMounted is false.
//
// Uses a short 3 s window timeout so loop() is not blocked for long;
// the full SD_BUS_WAIT_TIMEOUT_MS window is used by sdcontrol.setup()
// at startup. If no window is found here the next retry will try again.
// ────────────────────────────────────────────────────────────────────────────
bool ESPWebDAV::reinitSD() {
  lastSdInitAttemptMs = millis();

  // Quick bail-out: bus is in active use right now
  if (!sdcontrol.canWeTakeBus()) {
    return false;
  }

  // Wait for a stable window (short timeout to avoid blocking loop())
  if (!sdcontrol.waitForStableBusWindow(3000)) {
    return false;
  }

  sdcontrol.takeBusControl();

  // [STEP 3] Low-speed init for reliability on a shared/noisy bus
  sdMounted = sd.begin(SD_CS, SD_SCK_MHZ(SD_INIT_SPEED_MHZ));

  sdcontrol.relinquishBusControl();

  if (sdMounted) {
    DBG_PRINTLN("SD Card re-initialised successfully.");
  }
  return sdMounted;
}


// ------------------------
void ESPWebDAV::handleNotFound() {
// ------------------------
  String message = "Not found\n";
  message += "URI: ";
  message += uri;
  message += " Method: ";
  message += method;
  message += "\n";

  sendHeader("Allow", "OPTIONS,MKCOL,POST,PUT");
  send("404 Not Found", "text/plain", message);
  DBG_PRINTLN("404 Not Found");
}



// ------------------------
void ESPWebDAV::handleReject(String rejectMessage) {
// ------------------------
  DBG_PRINT("Rejecting request: "); DBG_PRINTLN(rejectMessage);

  if (method.equals("OPTIONS"))
    return handleOptions(RESOURCE_NONE);

  if (method.equals("PROPFIND")) {
    sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");
    setContentLength(CONTENT_LENGTH_UNKNOWN);
    send("207 Multi-Status", "application/xml;charset=utf-8", "");
    sendContent(F("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\"><D:response><D:href>/</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 30 Nov 1979 00:00:00 GMT</D:getlastmodified><D:getetag>\"3333333333333333333333333333333333333333\"</D:getetag><D:resourcetype><D:collection/></D:resourcetype></D:prop></D:propstat></D:response>"));

    if (depthHeader.equals("1")) {
      sendContent(F("<D:response><D:href>/"));
      sendContent(rejectMessage);
      sendContent(F("</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 01 Apr 2016 16:07:40 GMT</D:getlastmodified><D:getetag>\"2222222222222222222222222222222222222222\"</D:getetag><D:resourcetype/><D:getcontentlength>0</D:getcontentlength><D:getcontenttype>application/octet-stream</D:getcontenttype></D:prop></D:propstat></D:response>"));
    }

    sendContent(F("</D:multistatus>"));
    return;
  } else {
    handleNotFound();
  }
}


// set http_proxy=http://localhost:36036
// curl -v -X PROPFIND -H "Depth: 1" http://Rigidbot/Old/PipeClip.gcode
// Test PUT a file: curl -v -T c.txt -H "Expect:" http://Rigidbot/c.txt
// ------------------------
void ESPWebDAV::handleRequest(String blank) {
// ------------------------

  // [STEP 2] SD not yet mounted: return 503 so the WebDAV client retries.
  // loop() calls reinitSD() in the background until the card is available.
  if (!sdMounted) {
    send("503 Service Unavailable", "text/plain",
         "SD Card not available. Retrying in background.");
    DBG_PRINTLN("handleRequest: SD not mounted, returning 503.");
    return;
  }

  ResourceType resource = RESOURCE_NONE;

  // SdFat 3.x: sd.vwd() is private — open directly without vwd() argument
  FatFile tFile;
  if (tFile.open(uri.c_str(), O_READ)) {
    resource = tFile.isDir() ? RESOURCE_DIR : RESOURCE_FILE;
    tFile.close();
  }

  DBG_PRINT("\r\nm: "); DBG_PRINT(method);
  DBG_PRINT(" r: "); DBG_PRINT(resource);
  DBG_PRINT(" u: "); DBG_PRINTLN(uri);

  sendHeader("DAV", "2");

  if (method.equals("PROPFIND"))  return handleProp(resource);
  if (method.equals("GET"))       return handleGet(resource, true);
  if (method.equals("HEAD"))      return handleGet(resource, false);
  if (method.equals("OPTIONS"))   return handleOptions(resource);
  if (method.equals("PUT"))       return handlePut(resource);
  if (method.equals("LOCK"))      return handleLock(resource);
  if (method.equals("UNLOCK"))    return handleUnlock(resource);
  if (method.equals("PROPPATCH")) return handlePropPatch(resource);
  if (method.equals("MKCOL"))     return handleDirectoryCreate(resource);
  if (method.equals("MOVE"))      return handleMove(resource);
  if (method.equals("DELETE"))    return handleDelete(resource);

  handleNotFound();
}



// ------------------------
void ESPWebDAV::handleOptions(ResourceType resource) {
// ------------------------
  DBG_PRINTLN("Processing OPTION");
  sendHeader("Allow", "PROPFIND,GET,DELETE,PUT,COPY,MOVE");
  send("200 OK", NULL, "");
}



// ------------------------
void ESPWebDAV::handleLock(ResourceType resource) {
// ------------------------
  DBG_PRINTLN("Processing LOCK");

  if (resource == RESOURCE_NONE)
    return handleNotFound();

  sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
  sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");

  size_t  contentLen = contentLengthHeader.toInt();
  uint8_t buf[1024];
  size_t  numRead = readBytesWithTimeout(buf, sizeof(buf), contentLen);

  if (numRead == 0)
    return handleNotFound();

  buf[contentLen] = 0;
  String inXML    = String((char*) buf);
  int    startIdx = inXML.indexOf("<D:href>");
  int    endIdx   = inXML.indexOf("</D:href>");
  if (startIdx < 0 || endIdx < 0)
    return handleNotFound();

  String lockUser = inXML.substring(startIdx + 8, endIdx);
  String resp1 = F("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock><D:locktype><write/></D:locktype><D:lockscope><exclusive/></D:lockscope><D:locktoken><D:href>urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9</D:href></D:locktoken><D:lockroot><D:href>");
  String resp2 = F("</D:href></D:lockroot><D:depth>infinity</D:depth><D:owner><a:href xmlns:a=\"DAV:\">");
  String resp3 = F("</a:href></D:owner><D:timeout>Second-3600</D:timeout></D:activelock></D:lockdiscovery></D:prop>");

  send("200 OK", "application/xml;charset=utf-8", resp1 + uri + resp2 + lockUser + resp3);
}



// ------------------------
void ESPWebDAV::handleUnlock(ResourceType resource) {
// ------------------------
  DBG_PRINTLN("Processing UNLOCK");
  sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
  sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");
  send("204 No Content", NULL, "");
}



// ------------------------
void ESPWebDAV::handlePropPatch(ResourceType resource) {
// ------------------------
  DBG_PRINTLN("PROPPATCH forwarding to PROPFIND");
  handleProp(resource);
}



// ------------------------
void ESPWebDAV::handleProp(ResourceType resource) {
// ------------------------
  DBG_PRINTLN("Processing PROPFIND");

  DepthType depth = DEPTH_NONE;
  if (depthHeader.equals("1"))
    depth = DEPTH_CHILD;
  else if (depthHeader.equals("infinity"))
    depth = DEPTH_ALL;

  DBG_PRINT("Depth: "); DBG_PRINTLN(depth);

  if (resource == RESOURCE_NONE)
    return handleNotFound();

  if (resource == RESOURCE_FILE)
    sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
  else
    sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");

  setContentLength(CONTENT_LENGTH_UNKNOWN);
  send("207 Multi-Status", "application/xml;charset=utf-8", "");
  sendContent(F("<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
  sendContent(F("<D:multistatus xmlns:D=\"DAV:\">"));

  // SdFat 3.x: open directly without vwd()
  SdFile baseFile;
  baseFile.open(uri.c_str(), O_READ);
  sendPropResponse(false, &baseFile);

  if ((resource == RESOURCE_DIR) && (depth == DEPTH_CHILD)) {
    SdFile childFile;
    while (childFile.openNext(&baseFile, O_READ)) {
      yield();
      sendPropResponse(true, &childFile);
      childFile.close();
    }
  }

  baseFile.close();
  sendContent(F("</D:multistatus>"));
}



// ------------------------
void ESPWebDAV::sendPropResponse(boolean recursing, FatFile *curFile) {
// ------------------------
  char buf[255];
  curFile->getName(buf, sizeof(buf));

  String fullResPath = uri;

  if (recursing) {
    if (fullResPath.endsWith("/"))
      fullResPath += String(buf);
    else
      fullResPath += "/" + String(buf);
  }

  // SdFat 3.x API:
  //   • struct renamed: dir_t → DirFat_t
  //   • field renamed:  lastWriteTime → modifyTime, lastWriteDate → modifyDate
  //   • fields are uint8_t[2] (little-endian); cast to uint16_t to read them
  //     (same approach used by ESP8266 Arduino core's SDFS.h)
  //   • time macros renamed: FAT_* → FS_*
  DirFat_t dir;
  curFile->dirEntry(&dir);

  uint16_t modTime = *(uint16_t*)dir.modifyTime;
  uint16_t modDate = *(uint16_t*)dir.modifyDate;

  tm tmStr;
  tmStr.tm_hour = FS_HOUR(modTime);
  tmStr.tm_min  = FS_MINUTE(modTime);
  tmStr.tm_sec  = FS_SECOND(modTime);
  tmStr.tm_year = FS_YEAR(modDate) - 1900;
  tmStr.tm_mon  = FS_MONTH(modDate) - 1;
  tmStr.tm_mday = FS_DAY(modDate);
  time_t t2t = mktime(&tmStr);
  tm    *gTm = gmtime(&t2t);

  // Tue, 13 Oct 2015 17:07:35 GMT
  sprintf(buf, "%s, %02d %s %04d %02d:%02d:%02d GMT",
          wdays[gTm->tm_wday], gTm->tm_mday, months[gTm->tm_mon],
          gTm->tm_year + 1900, gTm->tm_hour, gTm->tm_min, gTm->tm_sec);
  String fileTimeStamp = String(buf);

  sendContent(F("<D:response><D:href>"));
  sendContent(fullResPath);
  sendContent(F("</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>"));
  sendContent(fileTimeStamp);
  sendContent(F("</D:getlastmodified><D:getetag>"));
  sendContent("\"" + sha1(fullResPath + fileTimeStamp) + "\"");
  sendContent(F("</D:getetag>"));

  if (curFile->isDir()) {
    sendContent(F("<D:resourcetype><D:collection/></D:resourcetype>"));
  } else {
    sendContent(F("<D:resourcetype/><D:getcontentlength>"));
    sendContent(String(curFile->fileSize()));
    sendContent(F("</D:getcontentlength><D:getcontenttype>"));
    sendContent(getMimeType(fullResPath));
    sendContent(F("</D:getcontenttype>"));
  }
  sendContent(F("</D:prop></D:propstat></D:response>"));
}




// ------------------------
void ESPWebDAV::handleGet(ResourceType resource, bool isGet) {
// ------------------------
  DBG_PRINTLN("Processing GET");

  if (resource != RESOURCE_FILE)
    return handleNotFound();

  SdFile  rFile;
  long    tStart = millis();
  uint8_t buf[1460];
  rFile.open(uri.c_str(), O_READ); // SdFat 3.x: no vwd() needed

  sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
  size_t fileSize    = rFile.fileSize();
  setContentLength(fileSize);
  String contentType = getMimeType(uri);
  if (uri.endsWith(".gz") &&
      contentType != "application/x-gzip" &&
      contentType != "application/octet-stream")
    sendHeader("Content-Encoding", "gzip");

  send("200 OK", contentType.c_str(), "");

  if (isGet) {
    // SD read speed ~17 s for a 4.5 MB file
    while (rFile.available()) {
      int numRead = rFile.read(buf, sizeof(buf));
      client.write(buf, numRead);
    }
  }

  rFile.close();
  DBG_PRINT("File "); DBG_PRINT(fileSize);
  DBG_PRINT(" bytes sent in: "); DBG_PRINT((millis() - tStart) / 1000);
  DBG_PRINTLN(" sec");
}




// ------------------------
void ESPWebDAV::handlePut(ResourceType resource) {
// ------------------------
  DBG_PRINTLN("Processing Put");

  if (resource == RESOURCE_DIR)
    return handleNotFound();

  SdFile nFile;
  sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");

  if (resource == RESOURCE_NONE) {
    if (!nFile.open(uri.c_str(), O_CREAT | O_WRITE))
      return handleWriteError("Unable to create a new file", &nFile);
  }

  DBG_PRINT(uri); DBG_PRINTLN(" - ready for data");
  size_t contentLen = contentLengthHeader.toInt();

  if (contentLen != 0) {
    const size_t WRITE_BLOCK_CONST = 512;
    uint8_t      buf[WRITE_BLOCK_CONST];
    long         tStart       = millis();
    size_t       numRemaining = contentLen;

    nFile.close();
    sd.remove(uri.c_str());

    size_t   contBlocks = (contentLen / WRITE_BLOCK_CONST + 1);
    uint32_t bgnBlock, endBlock;

    // SdFat 3.x: createContiguous() no longer takes vwd() as first argument
    if (!nFile.createContiguous(uri.c_str(), contBlocks * WRITE_BLOCK_CONST))
      return handleWriteError("File create contiguous sections failed", &nFile);

    if (!nFile.contiguousRange(&bgnBlock, &endBlock))
      return handleWriteError("Unable to get contiguous range", &nFile);

    // SdFat 3.x: writeStart() takes only the start sector (1 argument)
    if (!sd.card()->writeStart(bgnBlock))
      return handleWriteError("Unable to start writing contiguous range", &nFile);

    while (numRemaining > 0) {
      size_t numToRead = (numRemaining > WRITE_BLOCK_CONST) ? WRITE_BLOCK_CONST : numRemaining;
      size_t numRead   = readBytesWithTimeout(buf, sizeof(buf), numToRead);
      if (numRead == 0)
        break;

      if (!sd.card()->writeData(buf))
        return handleWriteError("Write data failed", &nFile);

      numRemaining -= numRead;
    }

    if (!sd.card()->writeStop())
      return handleWriteError("Unable to stop writing contiguous range", &nFile);

    if (numRemaining)
      return handleWriteError("Timed out waiting for data", &nFile);

    if (!nFile.truncate(contentLen))
      return handleWriteError("Unable to truncate the file", &nFile);

    DBG_PRINT("File "); DBG_PRINT(contentLen - numRemaining);
    DBG_PRINT(" bytes stored in: "); DBG_PRINT((millis() - tStart) / 1000);
    DBG_PRINTLN(" sec");
  }

  if (resource == RESOURCE_NONE)
    send("201 Created", NULL, "");
  else
    send("200 OK", NULL, "");

  nFile.close();
}




// ------------------------
void ESPWebDAV::handleWriteError(String message, FatFile *wFile) {
// ------------------------
  wFile->close();
  sd.remove(uri.c_str());
  send("500 Internal Server Error", "text/plain", message);
  DBG_PRINTLN(message);
}


// ------------------------
void ESPWebDAV::handleDirectoryCreate(ResourceType resource) {
// ------------------------
  DBG_PRINTLN("Processing MKCOL");

  if (resource != RESOURCE_NONE)
    return handleNotFound();

  if (!sd.mkdir(uri.c_str(), true)) {
    send("500 Internal Server Error", "text/plain", "Unable to create directory");
    DBG_PRINTLN("Unable to create directory");
    return;
  }

  DBG_PRINT(uri); DBG_PRINTLN(" directory created");
  sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
  send("201 Created", NULL, "");
}



// ------------------------
void ESPWebDAV::handleMove(ResourceType resource) {
// ------------------------
  DBG_PRINTLN("Processing MOVE");

  if (resource == RESOURCE_NONE)
    return handleNotFound();

  if (destinationHeader.length() == 0)
    return handleNotFound();

  String dest = urlToUri(destinationHeader);
  DBG_PRINT("Move destination: "); DBG_PRINTLN(dest);

  if (!sd.rename(uri.c_str(), dest.c_str())) {
    send("500 Internal Server Error", "text/plain", "Unable to move");
    DBG_PRINTLN("Unable to move file/directory");
    return;
  }

  DBG_PRINTLN("Move successful");
  sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
  send("201 Created", NULL, "");
}




// ------------------------
void ESPWebDAV::handleDelete(ResourceType resource) {
// ------------------------
  DBG_PRINTLN("Processing DELETE");

  if (resource == RESOURCE_NONE)
    return handleNotFound();

  bool retVal;

  if (resource == RESOURCE_FILE)
    retVal = sd.remove(uri.c_str());
  else
    retVal = sd.rmdir(uri.c_str());

  if (!retVal) {
    send("500 Internal Server Error", "text/plain", "Unable to delete");
    DBG_PRINTLN("Unable to delete file/directory");
    return;
  }

  DBG_PRINTLN("Delete successful");
  sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
  send("200 OK", NULL, "");
}

ESPWebDAV dav;
