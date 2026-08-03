#include <Arduino.h>
#include <WiFi.h>
#include <IRremote.hpp>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>

#include "config.h"

uint16_t RECV_PIN = 4;
uint16_t SEND_PIN = 16;
uint8_t SD_CS_PIN= 5;

void blastFromSdCard();

bool isReadingMode = false;

void postIrCodeToApi(int id, String name, String protocol, String code, String category);
void fetchAndBlastCategory(String category);
void fetchAndBlastById(int id);
void fetchAndBlastInteractive();
void printMenu();
void blastFromSdCard();
void blastFromSdCardById(int targetId);
void saveToSdCard(int id, String name, String protocol, String code);
void sendByProtocol(String protocol, uint32_t codeVal);

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP:" + WiFi.localIP().toString());
  Serial.println("Initializing SD Card...");
  if(!SD.begin(SD_CS_PIN)){
    Serial.println(" Failed!");
  }
  else{
    Serial.println(" Success!");
  }
  IrReceiver.begin(RECV_PIN, ENABLE_LED_FEEDBACK);
  IrSender.begin(SEND_PIN);

  printMenu();
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.equalsIgnoreCase("read")) {
      isReadingMode = true;
      Serial.println("VS1838B Active, press a button.");
    }
    else if (command.equalsIgnoreCase("receiveLocal")) {
      isReadingMode = false;
      blastFromSdCard();
    }
    else if (command.equalsIgnoreCase("receiveNetwork")) {
      isReadingMode = false;
      fetchAndBlastCategory("network");
    }
    else if(command.equalsIgnoreCase("sendNet")){
      isReadingMode = false;
      fetchAndBlastInteractive();
    }
    else if (command.startsWith("sendNet ")) {
      isReadingMode = false;
      int id = command.substring(8).toInt();
      fetchAndBlastById(id);
    }
    else if (command.startsWith("sendLoc")) {
      isReadingMode = false;
      blastFromSdCard();
    }
    else if (command.startsWith("sendLoc ")) {
      isReadingMode = false;
      int id = command.substring(8).toInt();
      blastFromSdCardById(id);
    }
    else {
      Serial.println("Unknown Command");
    }
  }

  if (isReadingMode) {
    if (IrReceiver.decode()) {
      if (IrReceiver.decodedIRData.protocol != UNKNOWN) {
        String protocolName = String(getProtocolString(IrReceiver.decodedIRData.protocol));

        char codeHex[12];
        snprintf(codeHex, sizeof(codeHex), "0x%08X", IrReceiver.decodedIRData.decodedRawData);
        
        Serial.println("IR Code Received!");
        Serial.println("Protocol: " + protocolName);
        Serial.println("Code: " + String(codeHex));

        Serial.println("Enter a Custom ID (or press ENTER for auto-ID)");
        while (Serial.available() > 0){Serial.read();}
        while (Serial.available() == 0){delay(10);}
        
        String idInput = Serial.readStringUntil('\n');
        idInput.trim();
        int customId = idInput.length() > 0 ? idInput.toInt() : -1;

        Serial.println("Enter a name for this button");

        while (Serial.available() > 0){ Serial.read();}

        while (Serial.available() == 0){
          delay (10);
        }

        String customName = Serial.readStringUntil('\n');
        customName.trim();

        if (customName.length() == 0){
          customName = "Unnamed IR Code";
        }

        Serial.println("Save destination? Type 'local' for SD Card or 'network' for Server (default: network):");
        while (Serial.available() > 0) {Serial.read();}
        while (Serial.available() == 0){ delay(10); }

        String category = Serial.readStringUntil('\n');
        category.trim();
        category.toLowerCase();

        if (category == "local"){
          saveToSdCard(customId, customName, protocolName, String(codeHex));
        }
        else{
          postIrCodeToApi(customId, customName, protocolName, String(codeHex), "network");
        }
        isReadingMode = false;
      }
      IrReceiver.resume();
    }
  }
}

void postIrCodeToApi(int id, String name, String protocol, String code, String category) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(API_BASE_URL + "/ircodes");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", API_KEY);

  JsonDocument doc;
  if (id > 0) doc["id"] = id; 
  doc["name"] = name;
  doc["protocol"] = protocol;
  doc["code"] = code;
  doc["category"] = category;

  String jsonBody;
  serializeJson(doc, jsonBody);
 
  int httpCode = http.POST(jsonBody);

  if (httpCode == 200 || httpCode == 201){
    Serial.println("API Response: " + String(httpCode));
    Serial.println("Saved successfully to database!\n");
  }
  else if (httpCode == 409){
    Serial.println("Duplicate Code: This IR code already exists in your database!\n");
  }
  else if (httpCode == 400){
    Serial.println("ID Error: That custom ID is already taken!\n");
  }
  else{
    Serial.println("HTTP POST Failed: " + http.errorToString(httpCode) + "\n");
  }
  http.end();
}

void fetchAndBlastCategory(String category) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(API_BASE_URL + "/ircodes?category=" + category);
  http.addHeader("x-api-key", API_KEY);

  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();

    JsonDocument doc;
    deserializeJson(doc, payload);
    JsonArray array = doc.as<JsonArray>();

    Serial.println("\n--- Found " + String(array.size()) + " codes in category '" + category + "' ---");

    for (JsonObject item : array) {
      int id = item["id"].as<int>();
      String name = item["name"].as<String>();
      String protocol = item["protocol"].as<String>();
      String codeStr = item["code"].as<String>();

      uint32_t codeVal = strtoul(codeStr.c_str(), NULL, 0);

      Serial.println("Blasting -> ID: " + String(id) + " | Name: " + name + " | Proto: " + protocol + " | Code: " + codeStr);

      sendByProtocol(protocol, codeVal);
      delay(500);
    }
    Serial.println("-----------------------------------------\n");
  }
  else {
    Serial.println("Failed to fetch category: " + String(httpCode));
  }
  http.end();
}

void fetchAndBlastInteractive() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(API_BASE_URL + "/ircodes");
  http.addHeader("x-api-key", API_KEY);

  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();

    JsonDocument doc;
    deserializeJson(doc, payload);
    JsonArray array = doc.as<JsonArray>();

    if (array.size() == 0) {
      Serial.println("No IR codes found in database.");
      http.end();
      return;
    }

    Serial.println("\n--- ALL SAVED IR CODES ---");
    for (JsonObject item : array) {
      Serial.println("ID: " + String(item["id"].as<int>()) + 
                     " | Name: " + item["name"].as<String>() + 
                     " | Protocol: " + item["protocol"].as<String>() + 
                     " | Code: " + item["code"].as<String>() + 
                     " | Category: " + item["category"].as<String>());
    }
    Serial.println("--------------------------");
    Serial.println("Enter the ID of the code you want to blast:");

    while (Serial.available() > 0) { Serial.read(); }
    while (Serial.available() == 0) { delay(10); }

    String input = Serial.readStringUntil('\n');
    input.trim();
    int targetId = input.toInt();

    fetchAndBlastById(targetId);
  }
  else {
    Serial.println("Failed to fetch IR codes list: " + String(httpCode));
  }
  http.end();
}

void fetchAndBlastById(int id) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(API_BASE_URL + "/ircodes/" + String(id));
  http.addHeader("x-api-key", API_KEY); 

  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();

    JsonDocument doc;
    deserializeJson(doc, payload);

    String codeStr = doc["code"].as<String>();
    String name = doc["name"].as<String>();
    String protocol = doc["protocol"].as<String>();
    uint32_t codeVal = strtoul(codeStr.c_str(), NULL, 0);

    Serial.println("Blasting ID " + String(id) + " (" + name + "): " + codeStr);
    
    sendByProtocol(protocol, codeVal);
  }
  else if (httpCode == 404) {
    Serial.println("Error: IR code ID " + String(id) + " not found!");
  }
  else {
    Serial.println("HTTP GET Error: " + String(httpCode));
  }
  http.end();
}

void saveToSdCard(int id, String name, String protocol,  String code){
  File file = SD.open("/ircodes_local.txt", FILE_APPEND);
  if (file){
    int targetId = (id > 0) ? id : millis();
    file.printf("%d,%s,%s,%s,local\n", targetId, name.c_str(), protocol.c_str(), code.c_str());
    file.close();
    Serial.println("Saved to SD Card!\n");
  }
  else{
    Serial.println("Failed to open /ircodes_local.txt on SD Card!\n");
  }
}


void blastFromSdCard(){
  File file = SD.open("/ircodes_local.txt", FILE_READ);
  if (!file){
    Serial.println("Could not open /ircodes_local.txt on SD Card!");
    return;
  }

  Serial.println("\n--- Blasting SD Card Local IR Codes ---");
  while (file.available()){
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int p1 = line.indexOf(',');
    int p2 = line.indexOf(',', p1 + 1);
    int p3 = line.indexOf(',', p2 + 1);
    int p4 = line.indexOf(',', p3 + 1);

    if (p1 != -1 && p2 != -1 && p3 != -1 && p4 != -1){
      String idStr = line.substring(0, p1);
      String name = line.substring(p1 + 1, p2);
      String protocol = line.substring(p2 + 1, p3);
      String codeStr = line.substring(p3 + 1, p4);

      uint32_t codeVal = strtoul(codeStr.c_str(), NULL, 0);

      Serial.println("SD Blast -> ID: " + idStr + " | Name: " + name + " | Proto: " + protocol +" | Code: " + codeStr);
      sendByProtocol(protocol, codeVal);
      delay(500);

    }
  }
  file.close();
  Serial.println("---------------------------------------\n");
}

void blastFromSdCardById(int targetId){
  File file = SD.open("/ircodes_local.txt", FILE_READ);
  if (!file){
    Serial.println("Could not open /ircodes_local.txt on SD Card!");
    return;
  }

  bool found = false;
  while (file.available())
  {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int p1 = line.indexOf(',');
    int p2 = line.indexOf(',', p1 + 1);
    int p3 = line.indexOf(',', p2 + 1);
    int p4 = line.indexOf(',', p3 + 1);
    if (p1 != -1 && p2 != -1 && p3 != -1 && p4 != -1){
      int id = line.substring(0, p1).toInt();

      if (id == targetId){

      String name = line.substring(p1 + 1, p2);
      String protocol = line.substring(p2 + 1, p3);
      String codeStr = line.substring(p3 + 1, p4);

      uint32_t codeVal = strtoul(codeStr.c_str(), NULL, 0);

      Serial.println("Blasting SD ID " + String(id) + " (" + name + "): " + codeStr);
      sendByProtocol(protocol, codeVal);
      found = true;
      break;
      }
    }
   

   }
    if (!found){
      Serial.println("Error: SD Code ID" + String(targetId) + " not found!"); 
    }
  file.close();
}


void sendByProtocol(String protocol, uint32_t codeVal){
  if (protocol == "NEC") IrSender.sendNECMSB(codeVal, 32);
  else if (protocol == "SONY") IrSender.sendSonyMSB(codeVal, 12);
  else if (protocol == "RC5") IrSender.sendRC5(codeVal, 13);
  else if (protocol == "RC6") IrSender.sendRC6(codeVal, 20);
  else if (protocol == "SAMSUNG") IrSender.sendSamsungMSB(codeVal, 32);
  else if (protocol == "LG") IrSender.sendLG(codeVal, 28);
  else Serial.println("Unsupported protocol for sending: " + protocol);
}



void printMenu() {
Serial.println("\n--- ESP32 IR COMMAND CENTER ---");
  Serial.println("Commands:");
  Serial.println("  read             - Capture incoming IR code");
  Serial.println("  receiveLocal     - Get & blast all 'local' category codes");
  Serial.println("  receiveNetwork   - Get & blast all 'network' category codes");
  Serial.println("  sendNet          - Interactively list & blast a server code");
  Serial.println("  sendNet <id>     - Blast a specific server code by ID");
  Serial.println("  sendLoc          - Blast ALL SD card codes");
  Serial.println("  sendLoc <id>     - Blast a specific SD card code by ID");
  Serial.println("--------------------------------\n");
}