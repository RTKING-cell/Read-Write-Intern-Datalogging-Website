/*
===========================================================
Intern RFID Reader / Writer for ESP32
Compatible with:
- Read-Write-Intern-Datalogging-Website
- Web Serial (Chrome/Edge)
- MFRC522
- MIFARE Ultralight

Serial Protocol

PC -> ESP32
-------------
READ
WRITE|John Smith

ESP32 -> PC
-------------
READY
OK|04AABBCC|John Smith
ERR|No card detected

ESP32 Wiring

RC522      ESP32
-------------------------
SDA (SS) -> GPIO5
SCK      -> GPIO18
MOSI     -> GPIO23
MISO     -> GPIO19
RST      -> GPIO22
3.3V     -> 3.3V
GND      -> GND
===========================================================
*/

#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN   5
#define RST_PIN 22

#define FIRST_PAGE 4
#define LAST_PAGE 15

MFRC522 mfrc522(SS_PIN, RST_PIN);

String serialBuffer = "";

const uint8_t writablePages = LAST_PAGE - FIRST_PAGE + 1;
const uint16_t writableBytes = writablePages * 4;

bool cardPresent = false;
String uidToString(MFRC522::Uid *uid)
{
    String result = "";

    for (byte i = 0; i < uid->size; i++)
    {
        if (uid->uidByte[i] < 0x10)
            result += "0";

        result += String(uid->uidByte[i], HEX);
    }

    result.toUpperCase();

    return result;
}

void flushCard()
{
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
}

bool waitForCard(unsigned long timeout = 15000)
{
    unsigned long start = millis();

    while (millis() - start < timeout)
    {
        if (mfrc522.PICC_IsNewCardPresent())
        {
            if (mfrc522.PICC_ReadCardSerial())
            {
                return true;
            }
        }

        delay(20);
    }

    return false;
}

void setup()
{
    Serial.begin(115200);

    SPI.begin(18, 19, 23, SS_PIN);

    mfrc522.PCD_Init();

    delay(100);

    Serial.println("READY");
}

void loop()
{
    while (Serial.available())
    {
        char c = Serial.read();

        if (c == '\n')
        {
            serialBuffer.trim();

            if (serialBuffer.length())
                processCommand(serialBuffer);

            serialBuffer = "";
        }
        else
        {
            serialBuffer += c;
        }
    }
}