/*
 * FTP Serveur for ESP8266 / ESP32
 * based on FTP Serveur for Arduino Due and Ethernet shield (W5100) or WIZ820io (W5200)
 * based on Jean-Michel Gallego's work
 * modified to work with esp8266 SPIFFS by David Paiva david@nailbuster.com
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
//  2020: modified by @EnRav

#include "ESP32FtpServer.h"

#include <WiFi.h>
#include <WiFiClient.h>


#define FTP_DEBUG


FtpServer::FtpServer():
    m_pCommandServer(NULL),
    m_pDataServer(NULL)
{

}


FtpServer::~FtpServer()
{
    if (m_pCommandServer) 
    {
        delete m_pCommandServer;
        m_pCommandServer = NULL;
    }

    if (m_pDataServer) 
    {
        delete m_pDataServer;
        m_pDataServer = NULL;
    }
}


bool FtpServer::begin(String uname, String pword, fs::FS &fs)
{
    bool result = false;

    if (m_pDataServer == NULL)
    {
        m_pDataServer = new WiFiServer(FTP_DATA_PORT_PASV);
    }

    if (m_pCommandServer == NULL)
    {
        m_pCommandServer = new WiFiServer(FTP_CTRL_PORT);
    }

    if ((m_pCommandServer) && (m_pDataServer))
    {
        // Tells the ftp server to begin listening for incoming connection
        m_User      = uname;
        m_Password  = pword;

        // tell the server where the files come from
        m_fs = &fs; 

        m_pCommandServer->begin();
        delay(10);

        m_pDataServer->begin();
        delay(10);

        millisTimeOut = (uint32_t)FTP_TIME_OUT * 60 * 1000;
        millisDelay = 0;
        cmdStatus = CmdStatus::DISCONNECT;
        iniVariables();

        result = true;
    }

    return result;
}


void FtpServer::iniVariables()
{
  // Default for data port
  dataPort = FTP_DATA_PORT_PASV;
  
  // Default Data connection is Active
  dataPassiveConn = true;
  
  // Set the root directory
  strcpy(cwdName, "/" );

  rnfrCmd = false;
  transferStatus = 0;
}


int FtpServer::handleFTP()
{
    //
    if((int32_t) ( millisDelay - millis() ) > 0 )
    {
        return 0;
    }

    // allow only one connection at a time
    if ((m_pCommandServer) && (m_pCommandServer->hasClient())) 
    {
        client.stop();
	    client = m_pCommandServer->available();
    }
  
    if( cmdStatus == CmdStatus::DISCONNECT )
    {
        if( client.connected())
        {
            disconnectClient();
        }
        cmdStatus = CmdStatus::PREPARATION;
    }
    else if( cmdStatus == CmdStatus::PREPARATION )                      // cancel all existing connections
    {
        abortTransfer();
        iniVariables();

	    log_i("Ftp server waiting for connection on port %u", FTP_CTRL_PORT);
        
        cmdStatus = CmdStatus::IDLE;
    }
    else if( cmdStatus == CmdStatus::IDLE )                             // FTP server idle
    {   		
        if( client.connected() )                                        // A client connected
        {
            clientConnected();      
            millisEndConnection = millis() + 10 * 1000 ;                // wait client id during 10 s.
            cmdStatus = CmdStatus::STANDBY;

            log_d("Client connected!");
        }
    }
    else if( readChar() > 0 )                                           // got response
    {
        if( cmdStatus == CmdStatus::STANDBY )            // Ftp server waiting for user identity
        {
            if( userIdentity() )
            {
                cmdStatus = CmdStatus::AUTHENTICATE;
            }
            else
            {
                cmdStatus = CmdStatus::DISCONNECT;
            }
        }
        else if( cmdStatus == CmdStatus::AUTHENTICATE )       // Ftp server waiting for user registration
        {
            if( userPassword() )
            {
                cmdStatus = CmdStatus::READY;
                millisEndConnection = millis() + millisTimeOut;
            }
            else
            {
                cmdStatus = CmdStatus::DISCONNECT;
            }
        }
        else if( cmdStatus == CmdStatus::READY )       // Ftp server waiting for user command
        {
            if( ! processCommand())
            {
                cmdStatus = CmdStatus::DISCONNECT;
            }
            else
            {
                millisEndConnection = millis() + millisTimeOut;
            }
        }  
    }
    else if (!client.connected() || !client)
    {
	    cmdStatus = CmdStatus::PREPARATION;
        
        log_d("client disconnected");   
    }

    if( transferStatus == 1 )         // Retrieve data
    {
        if( ! doRetrieve())
        {
            transferStatus = 0;
        }
    }
    else if( transferStatus == 2 )    // Store data
    {
        if( ! doStore())
        {
            transferStatus = 0;
        }
    }
    else if( cmdStatus > CmdStatus::STANDBY && ! ((int32_t) ( millisEndConnection - millis() ) > 0 ))
    {
	    client.println("530 Timeout");
        millisDelay = millis() + 200;    // delay of 200 ms
        cmdStatus = CmdStatus::DISCONNECT;
    }

    return    transferStatus!=0
           || cmdStatus     != CmdStatus::DISCONNECT;
}


void FtpServer::clientConnected()
{
    log_d("Client connected!");
  
    client.println( "220--- Welcome to FTP for ESP32 ---");
    client.println( "220---   By EnRav   ---");
    client.println( "220 --   Version " + String(FTP_SERVER_VERSION) + "   --");
    iCL = 0;
}


void FtpServer::disconnectClient()
{
    log_i(" Disconnecting client");

    abortTransfer();
    client.println("221 Goodbye");
    client.stop();
}

boolean FtpServer::userIdentity()
{	
    log_d("User cmd \"%s\" data \"%s\"", command, parameters);

    if( strcmp( command, "USER" ))
    {
        client.println( "500 Syntax error");
    }
    else if( strcmp( parameters, m_User.c_str() ))
    {
        client.println( "530 user not found");
    }
    else
    {
        client.println( "331 OK. Password required");
        strcpy( cwdName, "/" );
        return true;
    }

    millisDelay = millis() + 100;  // delay of 100 ms
    return false;
}

boolean FtpServer::userPassword()
{
    log_d("User cmd \"%s\" data \"%s\"", command, parameters);

    if( strcmp( command, "PASS" ))
    {
        client.println( "500 Syntax error");
    }
    else if( strcmp( parameters, m_Password.c_str() ))
    {
        client.println( "530 ");
    }
    else
    {
        log_d( "OK. Waiting for commands.");    
        client.println( "230 OK.");
        return true;
    }

    millisDelay = millis() + 100;  // delay of 100 ms
    return false;
}


uint8_t FtpServer::isConnected() 
{
    return client.connected();
}

boolean FtpServer::processCommand()
{
    ///////////////////////////////////////
    //                                   //
    //      ACCESS CONTROL COMMANDS      //
    //                                   //
    ///////////////////////////////////////

    log_d("cmd \"%s\"", command);

    //
    //  CDUP - Change to Parent Directory 
    //
    if( ! strcmp( command, "CDUP" ))
    {   
        //if we are not in the root directory
        if (strcmp(cwdName, "/"))
        {
            uint32_t length = strlen(cwdName);

            while((length) && (cwdName[length] != '/'))
            {
                --length;
            }

            if (length)
            {
                cwdName[length] = '\0';
            }
            else
            {
                cwdName[length+1] = '\0';
            }
            
        }

        log_d("CWD \"%s\"", cwdName);

	    client.println("250 Ok. Current directory is \"" + String(cwdName) + "\"");
    }

    //
    //  CWD - Change Working Directory
    //
    else if( ! strcmp( command, "CWD" ))
    {
        if( strcmp( parameters, "." ) == 0 )  // 'CWD .' is the same as PWD command
        {
            client.println( "257 \"" + String(cwdName) + "\" is your current directory");
        }
        else 
        {      
            log_d("CWD P=%s CWD=%s", parameters, cwdName);
    
            String dir;

            if (parameters[0]=='/')
            {
                dir = parameters;
            }
            else if (!strcmp(cwdName,"/"))
            {
                dir = String("/") + parameters;
            }
            else
            {
                dir = String(cwdName) + "/" + parameters;
            }        

            if (m_fs->exists(dir)) 
            {
                strcpy(cwdName, dir.c_str());
                client.println( "250 CWD Ok. Current directory is \"" + String(dir) + "\"");
                log_i("250 CWD Ok. Current directory is \"%s\"", dir.c_str());
            }
            else
            {
                client.println( "550 directory or file does not exist \"" + String(parameters) + "\"");
                log_i( "550 directory or file does not exist \"%s\"", parameters);
            }
        }
    
    }

    //
    //  PWD - Print Directory
    //
    else if( ! strcmp( command, "PWD" ))
    {
        client.println( "257 \"" + String(cwdName) + "\" is your current directory");
    }

    //
    //  QUIT
    //
    else if( ! strcmp( command, "QUIT" ))
    {
        disconnectClient();
        return false;
    }

  ///////////////////////////////////////
  //                                   //
  //    TRANSFER PARAMETER COMMANDS    //
  //                                   //
  ///////////////////////////////////////

    //
    //  MODE - Transfer Mode 
    //
    else if( ! strcmp( command, "MODE" ))
    {
        if( ! strcmp( parameters, "S" ))
        {
            client.println( "200 S Ok");  
        // else if( ! strcmp( parameters, "B" ))
        //  client.println( "200 B Ok\r\n";
        }
        else
        {
        client.println( "504 Only S(tream) is suported");
        }
    }

    //
    //  PASV - Passive Connection management
    //
    else if( ! strcmp( command, "PASV" ))
    {
        if (data.connected())
        {
            data.stop();
        }
    	dataIp = WiFi.localIP();	
	    dataPort = FTP_DATA_PORT_PASV;

    	log_i("Connection management set to passive");
        log_i( "Data port set to %u", dataPort);
   
        client.println( "227 Entering Passive Mode ("+ String(dataIp[0]) + "," + String(dataIp[1])+","+ String(dataIp[2])+","+ String(dataIp[3])+","+String( dataPort >> 8 ) +","+String ( dataPort & 255 )+").");
        dataPassiveConn = true;
    }

    //
    //  PORT - Data Port
    //
    else if( ! strcmp( command, "PORT" ))
    {
	    if (data)
        {
            data.stop();
        }
    
        // get IP of data client
        dataIp[ 0 ] = atoi( parameters );
        char * p = strchr( parameters, ',' );
        for( uint8_t i = 1; i < 4; i ++ )
        {
            dataIp[ i ] = atoi( ++ p );
            p = strchr( p, ',' );
        }

        // get port of data client
        dataPort = 256 * atoi( ++ p );
        p = strchr( p, ',' );
        dataPort += atoi( ++ p );
        if( p == NULL )
        {
            client.println( "501 Can't interpret parameters");
        }
        else
        {      
		    client.println("200 PORT command successful");
        dataPassiveConn = false;
        }
    }

    //
    //  STRU - File Structure
    //
    else if( ! strcmp( command, "STRU" ))
    {
        if( ! strcmp( parameters, "F" ))
        {
            client.println( "200 F Ok");
        }
        else
        {
            client.println( "504 Only F(ile) is suported");
        }
    }

    //
    //  TYPE - Data Type
    //
    else if( ! strcmp( command, "TYPE" ))
    {
        if( ! strcmp( parameters, "A" ))
        {
            client.println( "200 TYPE is now ASII");
        }
        else if( ! strcmp( parameters, "I" ))
        {
            client.println( "200 TYPE is now 8-bit binary");
        }
        else
        {
            client.println( "504 Unknow TYPE");
        }
    }

    ///////////////////////////////////////
    //                                   //
    //        FTP SERVICE COMMANDS       //
    //                                   //
    ///////////////////////////////////////

    //
    //  ABOR - Abort
    //
    else if( ! strcmp( command, "ABOR" ))
    {
        abortTransfer();
        client.println( "226 Data connection closed");
    }

    //
    //  DELE - Delete a File 
    //
    else if( ! strcmp( command, "DELE" ))
    {
        char path[ FTP_CWD_SIZE ];
        if( strlen( parameters ) == 0 )
        {
            client.println( "501 No file name");
        }
        else if( makePath( path ))
        {
            if( ! m_fs->exists( path ))
            {
                client.println( "550 File " + String(parameters) + " not found");
            }
            else
            {
                if( m_fs->remove( path ))
                {
                    client.println( "250 Deleted " + String(parameters) );
                }
                else
                {
                    client.println( "450 Can't delete " + String(parameters));
                }
            }
        }
    }

    //
    //  LIST - List 
    //
    else if( ! strcmp( command, "LIST" ))
    {
        if( ! dataConnect())
        {
            client.println( "425 No data connection");
        }
        else
        {
            client.println( "150 Accepted data connection");
            uint16_t nm = 0;
            File dir = m_fs->open(cwdName);
            if((!dir)||(!dir.isDirectory()))
            {
                client.println( "550 Can't open directory " + String(cwdName) );
            }
            else
            {
                File file = dir.openNextFile();
                while( file)
                {
    			    String fn, fs;
                    fn = file.name();
    			    fn.remove(0, 1);

  			        log_d("File Name = %s", fn.c_str());
      		        
                    fs = String(file.size());
                    
                    if(file.isDirectory())
                    {
                        data.println( "01-01-2000  00:00AM <DIR> " + fn);
                    }
                    else 
                    {
                        data.println( "01-01-2000  00:00AM " + fs + " " + fn);
                    }
                    nm ++;
                    file = dir.openNextFile();
                }

                client.println( "226 " + String(nm) + " matches total");
            }
            data.stop();
        }
    }

    //
    //  MLSD - Listing for Machine Processing (see RFC 3659)
    //
    else if( ! strcmp( command, "MLSD" ))
    {
        if( ! dataConnect())
        {
            client.println( "425 No data connection MLSD");
        }
        else
        {
	        client.println( "150 Accepted data connection");
            uint16_t nm = 0;
            File dir= m_fs->open(cwdName);
        
            if((!dir)||(!dir.isDirectory()))
            {
                client.println( "550 Can't open directory " +String(cwdName) );
            }
            else
            {    
                File file = dir.openNextFile();

                while( file)
                {
                    String fn,fs;
                    fn = file.name();
                    fn.remove(0, strlen(cwdName));

                    if(fn[0] == '/') 
                    {
                        fn.remove(0, 1);
                    }
            
                    fs = String(file.size());
                    
                    if(file.isDirectory())
                    {
                        data.println( "Type=dir;Size=" + fs + ";"+"modify=20000101000000;" +" " + fn);
                    } 
                    else 
                    {            
                    data.println( "Type=file;Size=" + fs + ";"+"modify=20000101000000;" +" " + fn);
                    }

                    nm ++;
                    file = dir.openNextFile();
                }

                client.println( "226-options: -a -l");
                client.println( "226 " + String(nm) + " matches total");
            }

            data.stop();
        }
    }

    //
    //  NLST - Name List
    //
    else if (!strcmp(command, "NLST"))
    {
        if (!dataConnect())
            client.println("425 No data connection");
        else
        {
            client.println("150 Accepted data connection");
            uint16_t nm = 0;
            File dir = m_fs->open(cwdName);
            if (!m_fs->exists(cwdName))
                client.println("550 Can't open directory " + String(parameters));
            else
            {
                File file = dir.openNextFile();
                //        while( dir.next())
                while (file)
                {
                    //          data.println( dir.fileName());
                    data.println(file.name());
                    nm++;
                    file = dir.openNextFile();
                }
                client.println("226 " + String(nm) + " matches total");
            }
            data.stop();
        }
    }

    //
    //  NOOP
    //
    else if( ! strcmp( command, "NOOP" ))
    {
        // dataPort = 0;
        client.println( "200 Zzz...");
    }

    //
    //  RETR - Retrieve
    //
    else if (!strcmp(command, "RETR"))
    {
        char path[FTP_CWD_SIZE];
        if (strlen(parameters) == 0)
        {
            client.println("501 No file name");
        }
        else if (makePath(path))
        {
            m_file = m_fs->open(path, "r");
            if (!m_file)
            {
                client.println("550 File " + String(parameters) + " not found");
            }
            else if (!m_file)
            {
                client.println("450 Can't open " + String(parameters));
            }
            else if (!dataConnect())
            {
                client.println("425 No data connection");
            }
            else
            {
                log_i("Sending %s", parameters);

                client.println("150-Connected to port " + String(dataPort));
                client.println("150 " + String(m_file.size()) + " bytes to download");
                millisBeginTrans = millis();
                bytesTransferred = 0;
                transferStatus = 1;
            }
        }
    }

    //
    //  STOR - Store
    //
    else if( ! strcmp( command, "STOR" ))
    {
        char path[ FTP_CWD_SIZE ];
        if( strlen( parameters ) == 0 )
        {
            client.println( "501 No file name");
        }
        else if( makePath( path ))
        {
		    m_file = m_fs->open(path, "w");
            if( !m_file)
            {
                client.println( "451 Can't open/create " +String(parameters) );
            }
            else if( ! dataConnect())
            {
                client.println( "425 No data connection");
                m_file.close();
            }
            else
            {                
                log_d( "Receiving %s", parameters);
             
                client.println( "150 Connected to port " + String(dataPort));
                millisBeginTrans = millis();
                bytesTransferred = 0;
                transferStatus = 2;
            }
        }
    }

    //
    //  MKD - Make Directory
    //
    else if( ! strcmp( command, "MKD" ))
    {
        log_d("MKD P=\"%s\" CWD=\"%s\"", parameters, cwdName);
        
        String dir;

        if (!strcmp(cwdName,"/"))
        {
            dir = String("/") + parameters;
        }
        else
        {
            dir = String(cwdName) +"/" + parameters;
        }

        log_i("try to create  \"%s\"", dir);
    
        
        if (m_fs->mkdir(dir.c_str()))
        {
            client.println( "257 \"" + String(parameters) + "\" - Directory successfully created");  
        }
        else
        {
	        client.println( "502 Can't create \"" + String(parameters));  
        }
    }

    //
    //  RMD - Remove a Directory 
    //
    else if( ! strcmp( command, "RMD" ))
    {
        log_d("RMD P=\"%s\" CWD=\"%s\"", parameters, cwdName);

        String dir;

        if (!strcmp(cwdName,"/"))
        {
            dir = String("/") + parameters;
        }
        else
        {
            dir = String(cwdName) +"/" + parameters;
        }
    
        if (m_fs->rmdir(dir.c_str()))
        {
            client.println( "250 RMD command successful");  
        }
        else
        {
	        client.println( "502 Can't delete \"" + String(parameters));  //not support on espyet
        }
    }

    //
    //  RNFR - Rename From 
    //
    else if( ! strcmp( command, "RNFR" ))
    {
        buf[ 0 ] = 0;

        if( strlen( parameters ) == 0 )
        {
            client.println( "501 No file name");
        }
        else if( makePath( buf ))
        {
            if( ! m_fs->exists( buf ))
            {
                client.println( "550 File " +String(parameters)+ " not found");
            }
            else
            {
        #ifdef FTP_DEBUG
		  Serial.println("Renaming " + String(buf));
        #endif
                client.println( "350 RNFR accepted - file exists, ready for destination");     
                rnfrCmd = true;
            }
        }
    }

    //
    //  RNTO - Rename To 
    //
    else if( ! strcmp( command, "RNTO" ))
    {  
        char path[ FTP_CWD_SIZE ];
        
        if( strlen( buf ) == 0 || ! rnfrCmd )
        {
            client.println( "503 Need RNFR before RNTO");
        }
        else if( strlen( parameters ) == 0 )
        {
            client.println( "501 No file name");
        }
        else if( makePath( path ))
        {
            if( m_fs->exists( path ))
            {
                client.println( "553 " +String(parameters)+ " already exists");
            }
            else
            {          
                log_d("Renaming \"%s\" to \"%s\"", buf, path);            
                
                if( m_fs->rename( buf, path ))
                {
                    client.println( "250 File successfully renamed or moved");
                }
                else
				{
                    client.println( "451 Rename/move failure");
                }
            }
        }
        rnfrCmd = false;
    }

    ///////////////////////////////////////
    //                                   //
    //   EXTENSIONS COMMANDS (RFC 3659)  //
    //                                   //
    ///////////////////////////////////////

    //
    //  FEAT - New Features
    //
    else if( ! strcmp( command, "FEAT" ))
    {
        client.println( "211-Extensions suported:");
        client.println( " MLSD");
        client.println( "211 End.");
    }

    //
    //  MDTM - File Modification Time (see RFC 3659)
    //
    else if (!strcmp(command, "MDTM"))
    {
	    client.println("550 Unable to retrieve time");
    }

    //
    //  SIZE - Size of the file
    //
    else if( ! strcmp( command, "SIZE" ))
    {
        char path[ FTP_CWD_SIZE ];
        if ( strlen( parameters ) == 0 )
        {
            client.println( "501 No file name");
        }
        else if( makePath( path ))
	    {
		    m_file = m_fs->open(path, "r");
            if(!m_file)
            {
                client.println( "450 Can't open " + String(parameters) );
            }
            else
            {
                client.println( "213 " + String(m_file.size()));
                m_file.close();
            }
        }
    }

    //
    //  SITE - System command
    //
    else if( ! strcmp( command, "SITE" ))
    {
        client.println( "500 Unknow SITE command " +String(parameters) );      
    }

    //
    //  Unrecognized commands ...
    //
    else
    {
        client.println( "500 Unknow command");
    }
  
    return true;
}

boolean FtpServer::dataConnect()
{
  unsigned long startTime = millis();
  //wait 5 seconds for a data connection
  if (!data.connected())
  {
    while (!m_pDataServer->hasClient() && millis() - startTime < 10000)
//    while (!dataServer.available() && millis() - startTime < 10000)
	  {
		  //delay(100);
		  yield();
	  }
    if (m_pDataServer->hasClient()) {
//    if (dataServer.available()) {
		  data.stop();
		  data = m_pDataServer->available();
			#ifdef FTP_DEBUG
		      Serial.println("ftpdataserver client....");
			#endif
		
	  }
  }

  return data.connected();

}

boolean FtpServer::doRetrieve()
{
    //int16_t nb = file.readBytes((uint8_t*) buf, FTP_BUF_SIZE );
    int16_t nb = m_file.readBytes(buf, FTP_BUF_SIZE);
    if (nb > 0)
    {
        data.write((uint8_t *)buf, nb);
        bytesTransferred += nb;
        return true;
    }
    closeTransfer();
    return false;
}


boolean FtpServer::doStore()
{
    if (data.connected())
    {
        uint32_t readCount = data.available() ;

        // do not read more bytes than available
        if (readCount > FTP_BUF_SIZE)
        {
            readCount = FTP_BUF_SIZE;
        };
        
        size_t nb = data.readBytes((uint8_t *)buf, readCount);

        if (nb > 0)
        {
            size_t written = m_file.write((uint8_t *)buf, nb);

            if (written != nb)
            {
                log_e("Bytes written (%d) differs from available bytes (%d)", written, nb);
            }

            bytesTransferred += nb;
        }
        else
        {
            Serial.println(".");
        }
        return true;
    }
    closeTransfer();
    return false;
}

void FtpServer::closeTransfer()
{
    uint32_t deltaT = (int32_t)(millis() - millisBeginTrans);
    if (deltaT > 0 && bytesTransferred > 0)
    {
        client.println("226-File successfully transferred");
        client.println("226 " + String(deltaT) + " ms, " + String(bytesTransferred / deltaT) + " kbytes/s");
    }
    else
    {
        client.println("226 File successfully transferred");
    }

    m_file.close();
    data.stop();
}

void FtpServer::abortTransfer()
{
    if (transferStatus > 0)
    {
        m_file.close();
        data.stop();
        client.println("426 Transfer aborted");
        log_w("Transfer aborted!");
    }
    transferStatus = 0;
}

// Read a char from client connected to ftp server
//
//  update cmdLine and command buffers, iCL and parameters pointers
//
//  return:
//    -2 if buffer cmdLine is full
//    -1 if line not completed
//     0 if empty line received
//    length of cmdLine (positive) if no empty line received 
int8_t FtpServer::readChar()
{
    int8_t rc = -1;

    if( client.available())
    {
        char c = client.read();
	 

        if( c == '\\' )
        {
            c = '/';
        }
        if( c != '\r' )
        {
            if( c != '\n' )
            {
                if( iCL < FTP_CMD_SIZE )
                {
                    cmdLine[ iCL ++ ] = c;
                }
                else
                {
                    rc = -2; //  Line too long
                }
            }
            else
            {
                cmdLine[ iCL ] = 0;
                command[ 0 ] = 0;
                parameters = NULL;

                // empty line?
                if( iCL == 0 )
                {
                    rc = 0;
                }
                else
                {
                    rc = iCL;
                    // search for space between command and parameters
                    parameters = strchr( cmdLine, ' ' );
                    if( parameters != NULL )
                    {
                        if( parameters - cmdLine > 4 )
                        {
                            rc = -2; // Syntax error
                        }
                        else
                        {
                            strncpy( command, cmdLine, parameters - cmdLine );
                            command[ parameters - cmdLine ] = 0;
              
                            while( * ( ++ parameters ) == ' ' )
                            ;
                        }
                    }
                    else if( strlen( cmdLine ) > 4 )
                    {
                        rc = -2; // Syntax error.
                    }
                    else
                    {
                        strcpy( command, cmdLine );
                    }
                    iCL = 0;
                }
            }
        }


        if( rc > 0 )
        {
            for( uint8_t i = 0 ; i < strlen( command ); i ++ )
            {
                command[ i ] = toupper( command[ i ] );
            }
        }

        if( rc == -2 )
        {
            iCL = 0;
            client.println( "500 Syntax error");
        }
    }
    return rc;
}

// Make complete path/name from cwdName and parameters
//
// 3 possible cases: parameters can be absolute path, relative path or only the name
//
// parameters:
//   fullName : where to store the path/name
//
// return:
//    true, if done
boolean FtpServer::makePath( char * fullName )
{
    return makePath( fullName, parameters );
}


boolean FtpServer::makePath( char * fullName, char * param )
{
    if (param == NULL)
    {
        param = parameters;
    }

    // Root or empty?
    if (strcmp(param, "/") == 0 || strlen(param) == 0)
    {
        strcpy(fullName, "/");
        return true;
    }

    // If relative path, concatenate with current dir
    if (param[0] != '/')
    {
        strcpy(fullName, cwdName);
        if (fullName[strlen(fullName) - 1] != '/')
        {
            strncat(fullName, "/", FTP_CWD_SIZE);
        }
        strncat(fullName, param, FTP_CWD_SIZE);
    }
    else
    {
        strcpy(fullName, param);
    }

    // If ends with '/', remove it
    uint16_t strl = strlen(fullName) - 1;
    if (fullName[strl] == '/' && strl > 1)
    {
        fullName[strl] = 0;
    }    
    if (strlen(fullName) < FTP_CWD_SIZE)
    {
        return true;
    }

    client.println("500 Command line too long");
    return false;
}

// Calculate year, month, day, hour, minute and second
//   from first parameter sent by MDTM command (YYYYMMDDHHMMSS)
//
// parameters:
//   pyear, pmonth, pday, phour, pminute and psecond: pointer of
//     variables where to store data
//
// return:
//    0 if parameter is not YYYYMMDDHHMMSS
//    length of parameter + space

uint8_t FtpServer::getDateTime( uint16_t * pyear, uint8_t * pmonth, uint8_t * pday,
                                uint8_t * phour, uint8_t * pminute, uint8_t * psecond )
{
    char dt[15];

    // Date/time are expressed as a 14 digits long string
    //   terminated by a space and followed by name of file
    if (strlen(parameters) < 15 || parameters[14] != ' ')
        return 0;
    for (uint8_t i = 0; i < 14; i++)
        if (!isdigit(parameters[i]))
            return 0;

    strncpy(dt, parameters, 14);
    dt[14] = 0;
    *psecond = atoi(dt + 12);
    dt[12] = 0;
    *pminute = atoi(dt + 10);
    dt[10] = 0;
    *phour = atoi(dt + 8);
    dt[8] = 0;
    *pday = atoi(dt + 6);
    dt[6] = 0;
    *pmonth = atoi(dt + 4);
    dt[4] = 0;
    *pyear = atoi(dt);
    return 15;
}

// Create string YYYYMMDDHHMMSS from date and time
//
// parameters:
//    date, time 
//    tstr: where to store the string. Must be at least 15 characters long
//
// return:
//    pointer to tstr

char * FtpServer::makeDateTimeStr( char * tstr, uint16_t date, uint16_t time )
{
    sprintf(tstr, "%04u%02u%02u%02u%02u%02u",
            ((date & 0xFE00) >> 9) + 1980, (date & 0x01E0) >> 5, date & 0x001F,
            (time & 0xF800) >> 11, (time & 0x07E0) >> 5, (time & 0x001F) << 1);
    return tstr;
}
