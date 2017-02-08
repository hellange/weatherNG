/************************************************
 *  Testing 7" display with FT810 graphics controller
 *  and NodeMCU processor
 *  
 *  FT81x graphics driver is initially copied from 
 *  jamesbowman / gd2-lib
 *  (https://github.com/jamesbowman/gd2-lib.git)
 *  Reduced and modified to compile with NodeMcu
 *  
 *  Connections:
 *  Connect SPI between nodeMcu and VM810C development board.
 *  Note that poor power supply can cause a lot or problems.
 *  I had best luck (stability) driving them with battery !
 *  
 *       nodemcu
 * CCLK  D5 (GPIO14)
 * CMISO D6 (GDIO12)
 * CMOSI D7 (GPIO13)
 * CCS   D8 (GPIO15)
 * 
 ************************************************/

#include <SPI.h>
#include "GD2.h"
#include "Wire.h"
#include "walk_assets.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "WundergroundClient.h"


// WIFI
const char* WIFI_SSID = "dlink";
const char* WIFI_PWD = "1234567890";
unsigned int localPort = 2390;      // local port to listen for UDP packets

/* Don't hardwire the IP address or we won't get the benefits of the pool.
 *  Lookup the IP address for the host name instead */
//IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;


unsigned long live_counter = 0;
unsigned long last_live_counter = 0;


void setup()
{
  //wdt_disable();
  Serial.begin(9600);
  Serial.println("Initializing WeatherNG graphics controller FT81x...");
  GD.begin(0);
  LOAD_ASSETS(); 
  Serial.println("Done!");
  WiFi.begin(WIFI_SSID, WIFI_PWD);

}

void drawMainText() {

  GD.ColorRGB(255,255,255);
  GD.ColorA(230);

  // put FT81x font 33 in slot 1
  GD.cmd_romfont(1, 33);
  
  // Text centered on screen
  GD.cmd_text(GD.w / 2, GD.h / 2 - 40,   1, OPT_CENTER, "NodeMCU Gameduino2");
  GD.cmd_text(GD.w / 2, GD.h / 2 + 20 , 28, OPT_CENTER, "Processor: NodeMCU V2");
  GD.cmd_text(GD.w / 2, GD.h / 2 + 50 , 28, OPT_CENTER, "Graphics library: Modified Gameduino2");
  GD.cmd_text(GD.w / 2, GD.h / 2 + 80 , 27, OPT_CENTER, "Display controller board: VM810C with FT810");
  GD.cmd_text(GD.w / 2, GD.h / 2 + 110 , 27, OPT_CENTER, "Display: 7'' 800x480");

}


void drawCircle(word x, word y, word pixels) {
  GD.PointSize(16 * pixels);
  GD.Begin(POINTS);
  //GD.Vertex2ii(x, y); // vertex2ii only handles up to 512 pixels !!! Use 2f instead
  GD.Vertex2f(x * 16, y * 16);
}

void drawSprite(int16_t x, int16_t y, byte handle, byte cell) {
  GD.SaveContext();
  GD.Begin(BITMAPS);
  GD.ColorRGB(255,255,255);
  GD.ColorA(255);
  GD.Vertex2ii(x, y, handle, cell);
  GD.RestoreContext();
}

void drawRandomCircles(int nr) {
  GD.SaveContext();
  GD.Begin(POINTS);
  for (int i = 0; i < nr; i++) {
    GD.PointSize(GD.random(16*50));
    GD.ColorRGB(GD.random(256), GD.random(256), GD.random(256));
    GD.ColorA(GD.random(256));
    GD.Vertex2ii(GD.random(800), GD.random(480));
  }
  GD.RestoreContext();
}

// Trying to solve problem with constant watchdog resets
// ref. http://internetofhomethings.com/homethings/?p=396
void delayWithYield(int ms) {
  int i;
  for(i=1;i!=ms;i++) {
    delay(1);
    if(i%50 == 0) {
      ESP.wdtFeed(); 
      yield();
    }
  }
}

bool timeFetched = false;
bool ntpIpFound = false;
unsigned long secsSince1900;
String hour;
String minute;
void checkWifi() {
  //Serial.println("Checking wifi");
     WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED) {
    GD.cmd_text(700, 10,   27, OPT_CENTER, "Initiating wifi...");
  } else {
    GD.cmd_text(700, 10,   27, OPT_CENTER, "Wifi found");

    String MyIp =  String(WiFi.localIP()[0]) + "." + String(WiFi.localIP()[2]) + "." + String(WiFi.localIP()[2]) + "." + String(WiFi.localIP()[3]);
    GD.cmd_text(700, 30,   27, OPT_CENTER, "Connected!");
    GD.cmd_text(700, 55,   27, OPT_CENTER, &MyIp[0]);

    udp.begin(localPort);

    //get a random server from the pool
    if (ntpIpFound == false) {
      Serial.println("finding host by name");
      WiFi.hostByName(ntpServerName, timeServerIP);
       Serial.println(timeServerIP);
       ntpIpFound = true; 
    }
    GD.cmd_text(400, 120,   27, OPT_CENTER, "NTP Server:");

    String theIp =  String(timeServerIP[0]) + "." + String(timeServerIP[1]) + "." + String(timeServerIP[2]) + "." + String(timeServerIP[3]);
    GD.cmd_text(400, 140,   27, OPT_CENTER, &theIp[0]);

    GD.cmd_text(300, 160,   27, OPT_CENTER, "Seconds since Jan 1 1900 : ");
    GD.cmd_number(500, 160,   27, OPT_CENTER, secsSince1900);

    GD.cmd_text(700, 160,   27, OPT_CENTER,& hour[0]);
    GD.cmd_text(719, 160,   27, OPT_CENTER, ":");
    GD.cmd_text(740, 160,   27, OPT_CENTER, &minute[0]);

  }

  if (timeFetched == false || live_counter > last_live_counter + 100) {
    Serial.println("Time not fetched yet");
    timeFetched = getTime();
    if (timeFetched == true) {
      last_live_counter = live_counter;
    }
  }

 
}

void displayTime() {
    GD.cmd_text(300, 160,   27, OPT_CENTER, "Seconds since Jan 1 1900 : ");
    GD.cmd_number(500, 160,   27, OPT_CENTER, secsSince1900);

    GD.cmd_text(700, 160,   27, OPT_CENTER,& hour[0]);
    GD.cmd_text(719, 160,   27, OPT_CENTER, ":");
    GD.cmd_text(740, 160,   27, OPT_CENTER, &minute[0]);
}

int packets = 0;
int sentRequest = false;

bool getTime()
{

    Serial.println("getTime");
    Serial.print("timeServerIP=");
    Serial.println(timeServerIP);



 // if (sentRequest == false) {
        Serial.println("sentingNTPPacket");

      sendNTPpacket(timeServerIP); // send an NTP packet to a time server
      sentRequest = true;
 // }
  // wait to see if a reply is available
  delayWithYield(500);
  
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
    GD.cmd_text(400, 80,   27, OPT_CENTER, "no package yet...");
    packets = packets + 1;
    return false;
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
            GD.cmd_number(30, 100,   27, OPT_CENTER, packets);

        GD.cmd_text(400, 100,   27, OPT_CENTER, "packet length");
                GD.cmd_number(400, 120,   27, OPT_CENTER, (int)cb);


    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Fetched Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);



    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);


    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    hour = String((epoch  % 86400L) / 3600);
    
    Serial.print(':');

    minute = String((epoch  % 3600) / 60);
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
      minute = "0" + minute;
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    
    Serial.println(epoch % 60); // print the second
    
    return true;
  }

}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}


int outsize_screen = 20; // how many pixels outside screen we will use
int walker_figure = 0;
int walker_position = -outsize_screen;



void loop()
{
  GD.ClearColorRGB(0x0000ee);
  GD.Clear();

  drawRandomCircles(100);
  yield();
  drawMainText();

  walker_position = walker_position + 4;
  if (walker_position > 800 + outsize_screen) {
    walker_position = - outsize_screen;
  }

  walker_figure = walker_figure + 1;
  if (walker_figure > 7) {
    walker_figure = 0;
  }
  drawSprite(walker_position, 250, WALK_HANDLE, walker_figure); // animate

  GD.ColorRGB(255,255,255);
  GD.ColorA(255);
  GD.cmd_number(10, 440 , 27, OPT_CENTERY, live_counter);
  GD.cmd_number(300, 460 , 27, OPT_CENTERY, last_live_counter);

  checkWifi();
  displayTime();

  GD.swap();    
  delayWithYield(100);
  live_counter ++;




}
