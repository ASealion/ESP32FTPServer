#include <WiFi.h>
#include <WiFiClient.h>
#include <SD.h>
#include "ESP32FtpServer.h"

const char *ssid = "blablabla...";     //WiFi SSID
const char *password = "blablabla..."; //WiFi Password


FtpServer ftpSrv; //set #define FTP_DEBUG in ESP32FtpServer.h to see ftp verbose on serial

void setup(void)
{
    pinMode(21, OUTPUT); 
    digitalWrite(21, HIGH);

    Serial.begin(115200);
    SPI.begin(18, 19, 23);

    WiFi.disconnect(); //added to start with the wifi off, avoid crashing
    WiFi.mode(WIFI_OFF);

    delay(250);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.println("");

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    /////FTP Setup, ensure SD is started before ftp;  /////////
    if (SD.begin(13)) 
    {
    Serial.println("SD opened!");

    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE){
        Serial.println("No SD card attached");
    }

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    ftpSrv.begin("esp32","esp32");    //username, password for ftp.  set ports in ESP32FtpServer.h  (default 21, 50009 for PASV)
  }
}

void loop(void){
    ftpSrv.handleFTP();        //make sure in loop you call handleFTP()!!   
    delay(10);
}
