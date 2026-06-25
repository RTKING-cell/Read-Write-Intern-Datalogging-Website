/*
 * Intern RFID Reader — MIFARE Ultralight name storage
 *
 * Hardware: Arduino Uno/Nano + MFRC522 RFID module
 *
 * Wiring (default):
 *   MFRC522    Arduino
 *   SDA (SS)   D10
 *   SCK        D13
 *   MOSI       D11
 *   MISO       D12
 *   IRQ        (not connected)
 *   GND        GND
 *   RST        D9
 *   3.3V       3.3V  (do not use 5V)
 *
 * Library: MFRC522 by GithubCommunity (Arduino Library Manager)
 *
 * Serial protocol @ 115200 baud:
 *   Host -> Arduino: READ
 *   Host -> Arduino: WRITE|<name>
 *   Arduino -> Host: READY
 *   Arduino -> Host: OK|<uid>|<name>
 *   Arduino -> Host: ERR|<message>
 *
 * Name storage on card (pages 4–15):
 *   Byte 0 of page 4 = name length (0–48)
 *   Remaining bytes = UTF-8 name text
 */

#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN 9
#define SS_PIN 10

#define NAME_START_PAGE 4
#define NAME_MAX_LEN 48
#define NAME_MAX_BYTES (NAME_MAX_LEN + 1)

MFRC522 mfrc522(SS_PIN, RST_PIN);

String inputLine;

void sendOk(const String &uid, const String &name) {
  Serial.print(F("OK|"));
  Serial.print(uid);
  Serial.print(F("|"));
  Serial.println(name);
}

void sendErr(const __FlashStringHelper *message) {
  Serial.print(F("ERR|"));
  Serial.println(message);
}

String uidToHex(MFRC522::Uid &uid) {
  String hex = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) {
      hex += '0';
    }
    hex += String(uid.uidByte[i], HEX);
  }
  hex.toUpperCase();
  return hex;
}

bool isUltralightFamily(MFRC522::PICC_Type piccType) {
  return piccType == MFRC522::PICC_TYPE_MIFARE_UL ||
         piccType == MFRC522::PICC_TYPE_MIFARE_MINI;
}

bool readCardUidAndType(String &uid, MFRC522::PICC_Type &piccType) {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return false;
  }

  piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  uid = uidToHex(mfrc522.uid);
  return true;
}

bool readPage(byte page, byte *buffer) {
  byte bufferSize = 18;
  MFRC522::StatusCode status = mfrc522.MIFARE_Read(page, buffer, &bufferSize);
  return status == MFRC522::STATUS_OK;
}

bool writePageBlock(byte page, const byte block[4]) {
  MFRC522::StatusCode status = mfrc522.MIFARE_Write(page, block, 4);
  return status == MFRC522::STATUS_OK;
}

bool readNameFromCard(String &name) {
  String uid;
  MFRC522::PICC_Type piccType;

  if (!readCardUidAndType(uid, piccType)) {
    sendErr(F("No card detected. Place a card on the reader."));
    return false;
  }

  if (!isUltralightFamily(piccType)) {
    mfrc522.PICC_HaltA();
    sendErr(F("Unsupported card type. Use MIFARE Ultralight."));
    return false;
  }

  byte pageBuffer[18];
  if (!readPage(NAME_START_PAGE, pageBuffer)) {
    mfrc522.PICC_HaltA();
    sendErr(F("Failed to read card memory."));
    return false;
  }

  byte nameLen = pageBuffer[0];
  if (nameLen > NAME_MAX_LEN) {
    nameLen = NAME_MAX_LEN;
  }

  name = "";
  byte collected = 0;
  byte pageOffset = 1;
  byte currentPage = NAME_START_PAGE;

  while (collected < nameLen) {
    while (pageOffset < 4 && collected < nameLen) {
      name += (char)pageBuffer[pageOffset++];
      collected++;
    }

    if (collected >= nameLen) {
      break;
    }

    currentPage++;
    pageOffset = 0;

    if (!readPage(currentPage, pageBuffer)) {
      mfrc522.PICC_HaltA();
      sendErr(F("Failed to read card memory."));
      return false;
    }
  }

  mfrc522.PICC_HaltA();
  sendOk(uid, name);
  return true;
}

bool writeNameToCard(const String &nameText) {
  String uid;
  MFRC522::PICC_Type piccType;

  if (!readCardUidAndType(uid, piccType)) {
    sendErr(F("No card detected. Place a card on the reader."));
    return false;
  }

  if (!isUltralightFamily(piccType)) {
    mfrc522.PICC_HaltA();
    sendErr(F("Unsupported card type. Use MIFARE Ultralight."));
    return false;
  }

  byte nameLen = nameText.length();
  if (nameLen > NAME_MAX_LEN) {
    nameLen = NAME_MAX_LEN;
  }

  byte data[NAME_MAX_BYTES];
  data[0] = nameLen;
  for (byte i = 0; i < nameLen; i++) {
    data[i + 1] = (byte)nameText.charAt(i);
  }

  byte totalBytes = nameLen + 1;
  byte pagesNeeded = (totalBytes + 3) / 4;

  for (byte pageIndex = 0; pageIndex < pagesNeeded; pageIndex++) {
    byte block[4];
    for (byte i = 0; i < 4; i++) {
      byte dataIndex = pageIndex * 4 + i;
      block[i] = dataIndex < totalBytes ? data[dataIndex] : 0;
    }

    if (!writePageBlock(NAME_START_PAGE + pageIndex, block)) {
      mfrc522.PICC_HaltA();
      sendErr(F("Failed to write card memory."));
      return false;
    }
  }

  mfrc522.PICC_HaltA();
  sendOk(uid, nameText.substring(0, nameLen));
  return true;
}

void handleCommand(const String &line) {
  if (line == "READ") {
    String name;
    readNameFromCard(name);
    return;
  }

  if (line.startsWith("WRITE|")) {
    String name = line.substring(6);
    name.trim();

    if (name.length() == 0) {
      sendErr(F("Name is empty."));
      return;
    }

    if (name.length() > NAME_MAX_LEN) {
      sendErr(F("Name is too long (max 48 characters)."));
      return;
    }

    writeNameToCard(name);
    return;
  }

  sendErr(F("Unknown command."));
}

void setup() {
  Serial.begin(115200);

  SPI.begin();
  mfrc522.PCD_Init();
  delay(50);

  Serial.println(F("READY"));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n') {
      inputLine.trim();
      if (inputLine.length() > 0) {
        handleCommand(inputLine);
      }
      inputLine = "";
    } else if (c != '\r') {
      inputLine += c;
    }
  }
}
