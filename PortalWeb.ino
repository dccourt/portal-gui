/*
 * XXX license etc.
 *
// XXX MUST UPLOAD THIS TO A MODULE WITH AT LEAST 1M FLASH.  Designed for WeMOS D1 Mini with at least 4M flash,
// organised as 1M code + 3M SPIFFS.
 *
 */

// On first boot, look for wifi AP called "PortalManager".  Connect to it, 
// then request page http://portal/wifi.htm .  Choose to either connect to a 
// local access point, or continue in AP mode (portal continues to act as an AP).
// Once connected in non-AP mode, need to find its IP address.
// From Windows, open Explorer and browse to Network, should see PortalManager icon, double-click to open web.
// Other OSs, browse to http://portalmanager.local/ ought to work, but seems broken right now.


#define DEVICE_NAME "PortalManager"

#include <PersWiFiManager.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <FS.h>
#include <DNSServer.h>
#include <EasySSDP.h> 
#include <ArduinoJson.h>

// Debug output will be TX-only on pin D4 (WeMOS D1 MINI)
#define DBG_OUTPUT_PORT Serial1

// UART used to communicate with the portal emulator
// We use this in 'pin-swapped' mode, so this corresponds to 
// D7 (RX) and D8 (TX) on WeMOS D1 MINI.
#define PORTAL_UART Serial

#define MAX_BANK_NAME_LEN 100

uint32 spi_flash_get_id();

ESP8266WebServer server ( 80 );
DNSServer dnsServer;
PersWiFiManager persWM(server, dnsServer);

//holds the current upload
File fsUploadFile;

// Allow reasonable default maxima - we'll actually dynamically 
// determine these over time.
int currNumSlots = 9;
int currNumBanks = 1000;

#define DEBUG_LINE(A) DBG_OUTPUT_PORT.println(A)
#define DEBUG(A) DBG_OUTPUT_PORT.print(A)

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  DEBUG_LINE("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    DEBUG("handleFileUpload Name: "); DEBUG_LINE(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //DEBUG("handleFileUpload Data: "); DEBUG_LINE(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    DEBUG("handleFileUpload Size: "); DEBUG_LINE(upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  DEBUG_LINE("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  DEBUG_LINE("handleFileCreate: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  DEBUG_LINE("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  
  output += "]";
  server.send(200, "text/json", output);
}

// If provided, return an integer argument. Otherwise returns supplied default.
int optionalIntArg(const char *name, int defaultVal) {
  int retval = defaultVal;
  if (server.hasArg(name)) {
    String argVal = server.arg(name);
    retval = argVal.toInt();
  }
  return retval;
}

// Enforce supply of an integer argument within a range, sends 500 response if bad/missing and 
// returns one less than the minimum.
int mandatoryIntArg(const char* name, int min, int max) {
  if (!server.hasArg(name)) { 
    String errtext = String("MISSING ") + name + " ARG";
    server.send(500, "text/plain", errtext);
    return (min - 1);
  }
  String argVal = server.arg(name);
  DEBUG_LINE(String("arg  ") + name + "='" + argVal + "'");

  int intVal = argVal.toInt();
  if ((intVal < min) || (intVal > max)) { server.send(500, "text/plain", "BAD VALUE"); return (min - 1); }

  return intVal;
}

// Enforce supply of an string argument, send 500 response if bad/missing and returns NULL
// Caller must delete the returned value after use.
String *mandatoryStrArg(const char* name) {
  if (!server.hasArg(name)) { 
    String errtext = String("MISSING ") + name + " ARG";
    server.send(500, "text/plain", errtext);
    return NULL;
  }
  
  String argVal = server.arg(name);
  DEBUG_LINE(String("arg  ") + name + "='" + argVal + "'");

  String *retval = new String(argVal);

  return retval;
}

// Look for optional string argument, returning default value if not found.
// Caller must delete the returned value after use.
String *optionalStrArg(const char* name, const char* defaultVal) {
  String *retval = NULL;
  if (!server.hasArg(name)) {
    retval = new String(defaultVal);
  } else {
    String argVal = server.arg(name);
    DEBUG_LINE(String("arg  ") + name + "='" + argVal + "'");
    retval = new String(argVal);
  }

  return retval;
}

String* readPortal() {
  String *resp = new String("");  
  while (PORTAL_UART.available() > 0) {
    delay(3);  //delay to allow buffer to fill
    if (PORTAL_UART.available() >0) {
      char c = PORTAL_UART.read();
//      DEBUG(c);
      *resp += c;
    }
  }

  return resp;
}

void flushPortal() {
//  DEBUG("Flush portal UART...");
  while (PORTAL_UART.available()) {
    char ch = PORTAL_UART.read();
    DEBUG(ch);
  }
//  DEBUG_LINE("Done.");
}

// Send a command to the portal, retrieve response.
String *sendPortalCommand(String cmd) {
  flushPortal();
  DEBUG_LINE("Send query:" + cmd);
  PORTAL_UART.println(cmd);

//  DEBUG_LINE("Wait");
  delay(30);

//  DEBUG_LINE("Read response");
  String* resp = readPortal();

  DEBUG("Return response:");
  DEBUG_LINE(*resp);

  return resp;
}

// ARGS: mode={1|2|3}
void setPortalMode() {
  int modeNum = mandatoryIntArg("mode", 1, 3);
  if (modeNum < 1) return;

  String cmd = String("AT+REBOOT=") + modeNum;
  String *resp = sendPortalCommand(cmd);
  server.send(200, "text/plain", *resp);
  delete resp;
}

// ARGS: targetslot
void clearSlot() {
  int slotnum = mandatoryIntArg("targetslot", 1, currNumSlots);
  if (slotnum < 1) return;

  String cmd = String("AT+SLOT=") + slotnum + ",0";
  String *resp = sendPortalCommand(cmd);  
  server.send(200, "text/plain", *resp);
  delete resp;
}

// Sends "AT+" the supplied command string
// ARGS: cmd
void sendLiteral() {
  String *cmd = mandatoryStrArg("cmd");
  if (cmd == NULL) return;

  String fullcmd = String("AT+") + *cmd;
  String *resp = sendPortalCommand(fullcmd);  
  server.send(200, "text/plain", *resp);
  delete resp;
  delete cmd;
}

String getSlotDetails() {
  String *resp = sendPortalCommand("AT+SLOTS?");  
  // A valid response looks like "+SLOTS: 1,2,0,0,0,0\r\nOK\r\n".
  DEBUG(*resp);
  
  if (resp->substring(0, 8) != "+SLOTS: ") {
    server.send(500, "text/plain", String("Bad portal response: ") + *resp + ":'" + resp->substring(0,8) + "'");
    return "";
  }
  
  // Allow for 12 slots.
  const int capacity = JSON_ARRAY_SIZE(12);
  StaticJsonDocument<capacity> doc;

  String currNum = "";
  for (int ii = 8; ii < resp->length() - 5; ii++) {
    char ch = resp->charAt(ii);
    if ((ch == ',') || (ch == '\r')) {
      // Reached the end of a slot value
      int slotVal = currNum.toInt();
      DEBUG_LINE(String("Found slot value: ") + slotVal);
      doc.add(slotVal);
      currNum = "";
    } else {
      currNum += ch;
    }
  }

  String output = "";
  serializeJson(doc, output);  

  delete resp;
  return output;
}

// Gets the status of all the slots
// Returns a JSON array of ints, 0 indicates empty slot.
// ARGS: NONE
void querySlots() {
  String output = getSlotDetails();
  if (output.length() > 0) {
    server.send(200, "text/json", output);
  }
}

int getNumBanks() {
  String *resp = sendPortalCommand("AT+BANKCOUNT?");
  String numPart = resp->substring(12);
  currNumBanks = numPart.toInt();
  delete resp;
  return currNumBanks;
}

int getNumSlots() {
  String *resp = sendPortalCommand("AT+SLOTCOUNT?");
  String numPart = resp->substring(12);
  currNumSlots = numPart.toInt();
  delete resp;
  return currNumSlots;
}

// Gets various info about global state.
// Unusual in that it returns that data as JSON, but we need to return multiple
// easily parsed bits of data, and JSON is natural for the client.
// Returned data structure:
// {
//   "version": ""Skylanders Portal of Power","9.9.9"",
//   "numbanks": 999,
//   "numslots": 6
// }
// ARGS: NONE
void queryState() {
  const int jsonsize = JSON_OBJECT_SIZE(4) + 70; // Allow 70 chars for version info and slot contents
  StaticJsonDocument<jsonsize> doc;
  
  String *resp = sendPortalCommand("AT+VER?");
  // Just strip the initial "+VER: " prefix, and trailing "\r\nOK\r\n".
  String part = resp->substring(6, resp->length() - 6);
  DEBUG_LINE(part);
  doc["version"] = part;
  delete resp;

  doc["numbanks"] = getNumBanks();
  doc["numslots"] = getNumSlots();
  doc["slotcontents"] = serialized(getSlotDetails());

  String output = "";
  serializeJson(doc, output);
  server.send(200, "text/plain", output);
}

// ARGS: banknum, targetslot
void selectBank() {
  int banknum = mandatoryIntArg("banknum", 1, currNumBanks);
  if (banknum < 1) return;
  int slotnum = mandatoryIntArg("targetslot", 1, currNumSlots);
  if (slotnum < 1) return;

  String cmd = String("AT+SLOT=") + slotnum + "," + banknum;
  String *resp = sendPortalCommand(cmd);
  
  server.send(200, "text/plain", *resp);

  delete resp;
}

// Get the name of one or more banks.
// Returned as a JSON array.
// If fetching just one, return as raw string, otherwise return as JSON array.
// ARGS: startbank, count (optional, default=1)
void getBankNames() {
  int maxBank = getNumBanks();  
  int banknum = mandatoryIntArg("startbank", 1, maxBank);
  if (banknum < 1) { return; }
  int count = optionalIntArg("count", 1);
  if (banknum + count > (maxBank + 1)) { count = maxBank - banknum + 1; }
  int jsonCapacity = JSON_ARRAY_SIZE(count) + 50*count; // allows 50 chars per item
  DynamicJsonDocument doc(jsonCapacity);

  int endbank = banknum + count;
  for (int ii = banknum; ii < endbank; ii++) {
    String cmd = String("AT+BANK=") + ii;
    String *resp = sendPortalCommand(cmd);
    // strip "+BANK: \"" prefix and "\"\r\nOK\r\n" suffix
    String part = resp->substring(8, resp->length() - 7);
    doc.add(part);
    delete resp;
  }

  String output = "";
  serializeJson(doc, output);

  server.send(200, "text/json", output);
}

// Get the info of one or more banks.
// Returned as a JSON array of arrays : the sub-arrays contain name + UID + L/E/M data + whether we have an image for this UID (1 or 0)
// ARGS: startbank, imgdir (which dir to look for images in - optional), count (optional, default=1)
void getBankInfo() {
  int maxBank = getNumBanks();  
  int banknum = mandatoryIntArg("startbank", 1, maxBank);
  if (banknum < 1) { return; }
  int count = optionalIntArg("count", 1);
  String *imgdir = optionalStrArg("imgdir", "");
  if (banknum + count > (maxBank + 1)) { count = maxBank - banknum + 1; }
  int jsonCapacity = JSON_ARRAY_SIZE(count) + MAX_BANK_NAME_LEN*count + JSON_ARRAY_SIZE(4)*count + 16*count + 20*count + 2*count; // allows MAX_BANK_NAME_LEN chars per name, 16 chars per UID, 20 chars L/E/M data
  DynamicJsonDocument doc(jsonCapacity);

  int endbank = banknum + count;
  for (int ii = banknum; ii < endbank; ii++) {
    String cmd = String("AT+BANKINFO=") + ii + ",255";
    String *resp = sendPortalCommand(cmd);

    // Need to find the double-quote delimited values in the response.
    String namepart = "";
    String uidpart = "";
    String lempart = "";
    int len = resp->length();
    String *currStr = &namepart;
    bool inQuotes = false;
    int partnum = 0;
    for (int jj=0; jj < len; jj++)
    {
      char ch = resp->charAt(jj);
      if (ch == '"') {
        if (inQuotes) {
          // Just finished a value.  Get ready to grab the next.
          partnum++;
          switch(partnum) {
            case 1:
              currStr = &uidpart;
              break;
            case 2:
              currStr = &lempart;
              break;
            default:
              DEBUG_LINE("ERR: Too many parts");
              break;
          }
        }
        inQuotes = !inQuotes;
      } else if (inQuotes) {
        *currStr += ch;
      }
    }

    JsonArray innerdoc = doc.createNestedArray();  // Automatically adds to doc.
    innerdoc.add(namepart);
    innerdoc.add(uidpart);
    innerdoc.add(lempart);
    int gotImg = 0;
    if (imgdir->length() > 0) {
      String imgpath = String("/images/") + *imgdir + '/' + uidpart + ".jpg";
      DEBUG_LINE(imgpath);
      if (SPIFFS.exists(imgpath)) {
        DEBUG_LINE("Got img");
        gotImg = 1;
      }
    }
    innerdoc.add(gotImg);
    
    delete resp;
  }

  String output = "";
  serializeJson(doc, output);

  delete imgdir;
  server.send(200, "text/json", output);
}

void setup ( void ) {
  DBG_OUTPUT_PORT.begin ( 115200 );
  DEBUG_LINE ( "" );

  PORTAL_UART.begin( 115200 );
  PORTAL_UART.swap();
  
  SPIFFS.begin();

  //optional code handlers to run everytime wifi is connected...
  persWM.onConnect([]() {
    DEBUG_LINE("wifi connected");
    DEBUG_LINE(WiFi.localIP());
    EasySSDP::begin(server, DEVICE_NAME);
    if (!MDNS.begin(DEVICE_NAME)) {
      DEBUG_LINE("Failed to start MDNS");
    } else {
      DEBUG("MDNS started : ");
      DEBUG_LINE(DEVICE_NAME);
    }
  });
  //...or AP mode is started
  persWM.onAp([](){
    DEBUG_LINE("AP MODE");
    DEBUG_LINE(persWM.getApSsid());
  });

  persWM.setApCredentials(DEVICE_NAME);
  persWM.setConnectNonBlock(true);
  persWM.begin();
  
  // SUPPORTED URLs:

  // ----- File management
  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, [](){
    if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);

  // ----- Portal interaction
  server.on ( "/setportalmode", setPortalMode );
  server.on ( "/literal", sendLiteral );
  server.on ( "/selectbank", selectBank );
  server.on ( "/queryslots", querySlots );
  server.on ( "/querystate", queryState );
  server.on ( "/clearslot", clearSlot );  
  server.on ( "/banknames", getBankNames );
  server.on ( "/bankinfo", getBankInfo );

  // Everything under /images is static content that we allow
  // to be cached by the browser - the WebServer class can
  // handle all that for us transparently.
  server.serveStatic("/images", SPIFFS, "/images", "max-age=86400");
  // And general static content
  server.serveStatic("/static", SPIFFS, "/static", "max-age=86400");

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });

  server.begin();
  DEBUG_LINE ( "HTTP server started" );

  DEBUG_LINE ( WiFi.localIP() );
}

void loop ( void ) {
  persWM.handleWiFi();
  server.handleClient();
  dnsServer.processNextRequest();
}
