#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 5
#define RST_PIN 22
#define SCK_PIN 18
#define MISO_PIN 19
#define MOSI_PIN 23

#define NAME_START_PAGE 4
#define NAME_MAX_LEN 48
#define NAME_MAX_BYTES (NAME_MAX_LEN + 1)

MFRC522 rfid(SS_PIN, RST_PIN);
String inputLine;

void sendOk(const String& uid, const String& name) {
  Serial.print("OK|");
  Serial.print(uid);
  Serial.print("|");
  Serial.println(name);
}

void sendErr(const String& msg) {
  Serial.print("ERR|");
  Serial.println(msg);
}

String uidHex() {
  String out;
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) out += "0";
    out += String(rfid.uid.uidByte[i], HEX);
  }
  out.toUpperCase();
  return out;
}

bool getCard(String& uid) {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return false;
  uid = uidHex();
  return true;
}

bool readPage(byte page, byte* buffer) {
  byte size = 18;
  return rfid.MIFARE_Read(page, buffer, &size) == MFRC522::STATUS_OK;
}

bool writePage(byte page, byte block[4]) {
  return rfid.MIFARE_Write(page, block, 4) == MFRC522::STATUS_OK;
}

void finishCard() {
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void readName() {
  String uid;
  if (!getCard(uid)) return sendErr("No card detected. Place a card on the reader.");

  byte buf[18];
  if (!readPage(NAME_START_PAGE, buf)) {
    finishCard();
    return sendErr("Failed to read card memory.");
  }

  byte len = min(buf[0], (byte)NAME_MAX_LEN);
  String name;
  byte got = 0, offset = 1, page = NAME_START_PAGE;

  while (got < len) {
    while (offset < 4 && got < len) {
      name += (char)buf[offset++];
      got++;
    }
    if (got >= len) break;
    page++;
    offset = 0;
    if (!readPage(page, buf)) {
      finishCard();
      return sendErr("Failed to read card memory.");
    }
  }

  finishCard();
  sendOk(uid, name);
}

void writeName(String name) {
  name.trim();
  if (!name.length()) return sendErr("Name is empty.");
  if (name.length() > NAME_MAX_LEN) return sendErr("Name is too long (max 48 characters).");

  String uid;
  if (!getCard(uid)) return sendErr("No card detected. Place a card on the reader.");

  byte data[NAME_MAX_BYTES] = {0};
  byte len = name.length();
  data[0] = len;

  for (byte i = 0; i < len; i++) data[i + 1] = (byte)name.charAt(i);

  byte pages = (len + 1 + 3) / 4;
  for (byte p = 0; p < pages; p++) {
    byte block[4] = {0, 0, 0, 0};
    for (byte i = 0; i < 4; i++) {
      byte index = p * 4 + i;
      if (index < len + 1) block[i] = data[index];
    }
    if (!writePage(NAME_START_PAGE + p, block)) {
      finishCard();
      return sendErr("Failed to write card memory.");
    }
  }

  finishCard();
  sendOk(uid, name);
}

void handleCommand(String line) {
  line.trim();
  if (line == "READ") return readName();
  if (line.startsWith("WRITE|")) return writeName(line.substring(6));
  sendErr("Unknown command.");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();

  Serial.println("READY");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      if (inputLine.length()) handleCommand(inputLine);
      inputLine = "";
    } else if (c != '\r') {
      inputLine += c;
    }
  }
}
