/*
*  FTP SERVER FOR ESP32
 * based on FTP Serveur for Arduino Due and Ethernet shield (W5100) or WIZ820io (W5200)
 * based on Jean-Michel Gallego's work
 * modified to work with esp8266 SPIFFS by David Paiva (david@nailbuster.com)
 * modified and extended to work on ESP32 by Mike (developer@mail.enrav.de)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
//  2017: modified by @robo8080
//  2019: modified by @HenrikSte
//  2020: midified by @enrav

/*******************************************************************************
 **                                                                            **
 **                       DEFINITIONS FOR FTP SERVER                           **
 **                                                                            **
 *******************************************************************************/

// Uncomment to print debugging info to console
//#define FTP_DEBUG

#ifndef FTP_SERVERESP_H
#define FTP_SERVERESP_H

#include <FS.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClient.h>

#define FTP_SERVER_VERSION "0.1.0"

#define FTP_CTRL_PORT    21          // Command port on which server is listening
#define FTP_DATA_PORT_PASV 50009     // Data port in passive mode

#define FTP_TIME_OUT  5           // Disconnect client after 5 minutes of inactivity
#define FTP_CMD_SIZE 255 + 8 // max size of a command
#define FTP_CWD_SIZE 255 + 8 // max size of a directory name
#define FTP_FIL_SIZE 255     // max size of a file name
//#define FTP_BUF_SIZE (8192*1)-1 //512   // size of file buffer for read/write
#define FTP_BUF_SIZE 4096 //512   // size of file buffer for read/write

class FtpServer
{
public:
    FtpServer();
    ~FtpServer();

    /**
     * @brief
     * 
     * */
    bool begin(String uname, String pword, fs::FS &fs = SD);

    /** 
     * @brief
     * 
     * */
    int handleFTP();

    /**
     * @brief
     * 
     * */
    uint8_t isConnected();

private:
    void iniVariables();
    void clientConnected();
    void disconnectClient();
    boolean userIdentity();
    boolean userPassword();
    boolean processCommand();
    boolean dataConnect();
    boolean doRetrieve();
    boolean doStore();
    void closeTransfer();
    void abortTransfer();
    boolean makePath(char *fullname);
    boolean makePath(char *fullName, char *param);
    uint8_t getDateTime(uint16_t *pyear, uint8_t *pmonth, uint8_t *pday,
                        uint8_t *phour, uint8_t *pminute, uint8_t *second);
    char *makeDateTimeStr(char *tstr, uint16_t date, uint16_t time);
    int8_t readChar();

    IPAddress dataIp; // IP address of client for data
    WiFiClient client;
    WiFiClient data;

    fs::FS *m_fs; // pointer to the used file system
    File m_file;  //

    boolean dataPassiveConn;
    uint16_t dataPort;
    char buf[FTP_BUF_SIZE];     // data buffer for transfers
    char cmdLine[FTP_CMD_SIZE]; // where to store incoming char from client
    char cwdName[FTP_CWD_SIZE]; // name of current directory
    char command[5];            // command sent by client
    boolean rnfrCmd;            // previous command was RNFR
    char *parameters;           // point to begin of parameters sent by client
    uint16_t iCL;               // pointer to cmdLine next incoming char

    WiFiServer *m_pCommandServer;
    WiFiServer *m_pDataServer;

    enum class CmdStatus
    {
        DISCONNECT,  // 0
        PREPARATION, // 1
        IDLE,        // 2
        STANDBY,     // 3
        AUTHENTICATE,
        READY,
    } cmdStatus;

    int8_t transferStatus;  // status of ftp data transfer
    uint32_t millisTimeOut, // disconnect after 5 min of inactivity
        millisDelay,
        millisEndConnection, //
        millisBeginTrans,    // store time of beginning of a transaction
        bytesTransferred;    //
    String m_User;
    String m_Password;
};

#endif // FTP_SERVERESP_H
