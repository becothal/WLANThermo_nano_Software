 /*************************************************** 
    Copyright (C) 2016  Steffen Ochs, Phantomias2006

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    HISTORY: Please refer Github History
    
 ****************************************************/

#include <Wire.h>                 // I2C
//#include <SPI.h>                  // SPI
//#include <ESP8266WiFi.h>          // WIFI   // CaptivePortal
//#include <WiFiClientSecure.h>     // HTTPS
#include <TimeLib.h>              // TIME
#include <EEPROM.h>               // EEPROM
#include <FS.h>                   // FILESYSTEM
#include <ArduinoJson.h>          // JSON
//#include <DNSServer.h>    // CaptivePortal
#include <ESP8266mDNS.h>          // mDNS
#include <ESPAsyncTCP.h>          // ASYNCTCP
#include <ESPAsyncWebServer.h>    // https://github.com/me-no-dev/ESPAsyncWebServer/issues/60
#include "AsyncJson.h"            // ASYNCJSON
#include <AsyncMqttClient.h>      // ASYNCMQTT
//#include <StreamString.h>

extern "C" {
#include "user_interface.h"
#include "spi_flash.h"
#include "core_esp8266_si2c.c"
}

extern "C" uint32_t _SPIFFS_start;      // START ADRESS FS
extern "C" uint32_t _SPIFFS_end;        // FIRST ADRESS AFTER FS

// number of items in an array
#define NUMITEMS(arg) ((unsigned int) (sizeof (arg) / sizeof (arg [0])))

#include "c_consts.h"

// CaptivePortal
//const byte DNS_PORT = 53;
//DNSServer dnsServer;

// ++++++++++++++++++++++++++++++++++++++++++++++++++++
// GLOBAL VARIABLES
// ++++++++++++++++++++++++++++++++++++++++++++++++++++

uint16_t MAX1161x_ADDRESS;   // MAX1161x

// CHANNELS
struct ChannelData {
   String name;             // CHANNEL NAME
   float temp;              // TEMPERATURE
   int   match;             // Anzeige im Temperatursymbol
   float max;               // MAXIMUM TEMPERATURE
   float min;               // MINIMUM TEMPERATURE
   byte  typ;               // TEMPERATURE SENSOR
   byte  alarm;             // SET CHANNEL ALARM (0: off, 1:push, 2:summer, 3:all)
   bool  isalarm;           // Limits überschritten   
   byte  showalarm;         // Alarm anzeigen   (0:off, 1:show, 2:first show)
   String color;            // COLOR
   bool repeatalarm;
   int repeat;
   
   uint8_t cnt;
   uint8_t idx;
   float ar[TEMP_MEM_SIZE];
};

ChannelData ch[MAXCHANNELS];

enum {ALARM_OFF,ALARM_PUSH,ALARM_HW,ALARM_ALL};
String alarmname[4] = {"off","push","summer","all"};

// SENSORTYP
String  ttypname[SENSORTYPEN] = {"1000K/Maverick","Fantast-Neu","Fantast","100K/iGrill2","ET-73",
                                 "Perfektion","50K","Inkbird","100K6A1B","Weber_6743",
                                 "Santos","5K3A1B"}; // 

// CHANNEL COLORS
String colors[8] = {"#0C4C88","#22B14C","#EF562D","#FFC100","#A349A4","#804000","#5587A2","#5C7148"};

// PITMASTER
enum {PITOFF, MANUAL, AUTO, AUTOTUNE, DUTYCYCLE, MYAUTO};
enum {SSR, FAN, SERVO, DAMPER, FAN2, SERVO2, NOAR};

struct Pitmaster {
   byte  pid;               // PITMASTER LINKED PID
   float set;               // SET-TEMPERATUR
   byte  active;            // PITMASTER ACTIVITY
   byte  channel;           // PITMASTER LINKED CHANNEL
   float value;             // PITMASTER VALUE IN %
   
   byte io[2];              // PITMASTER LOCAL HARDWARE IO
   byte aktor[2];           // PITMASTER IO ATTACHED AKTOR
   uint16_t dcmin;          // PITMASTER DUTY CYCLE LIMIT MIN
   uint16_t dcmax;          // PITMASTER DUTY CYCLE LIMIT MAX
   
   uint16_t pause;          // PITMASTER PAUSE
   unsigned long last;      // PITMASTER VALUE TIMER
   
   bool event;              // SSR EVENT
   unsigned long stakt;     // SERVO EVENT
   uint16_t msec;           // PITMASTER VALUE IN MILLISEC (SSR) / MICROSEC (SERVO)
   uint16_t nmsec;          // PITMASTER NEW VALUE CACHE 
   unsigned long timer0;    // PITMASTER TIMER VARIABLE (FAN) / (SERVO)
    
   float esum;              // PITMASTER I-PART DIFFERENZ SUM
   float elast;             // PITMASTER D-PART DIFFERENZ LAST
   float Ki_alt;            // PITMASTER I-PART CACHE
   bool disabled;           // PITMASTER DISABLE HEATER
   bool resume;             // PITMASTER Continue after restart 

   bool jump;
};

Pitmaster pitMaster[PITMASTERSIZE];

// PID PROFIL
int pidsize;

struct PID {
  String name;
  byte id;
  byte aktor;                   // 0: SSR, 1:FAN, 2:Servo, 3:Damper
  float Kp;                     // P-FAKTOR ABOVE PSWITCH
  float Ki;                     // I-FAKTOR ABOVE PSWITCH
  float Kd;                     // D-FAKTOR ABOVE PSWITCH
  //float Kp_a;                   // P-FAKTOR BELOW PSWITCH
  //float Ki_a;                   // I-FAKTOR ABOVE PSWITCH
  //float Kd_a;                   // D-FAKTOR ABOVE PSWITCH
  //int Ki_min;                   // MINIMUM VALUE I-PART   // raus ?
  //int Ki_max;                   // MAXIMUM VALUE I-PART   // raus ?
  //float pswitch;                // SWITCHING LIMIT        // raus ?
  float DCmin;                  // PID DUTY CYCLE MIN
  float DCmax;                  // PID DUTY CYCLE MAX
  byte jumppw;            // JUMP POWER  (muss initalisiert werden)
  byte opl;
  byte autotune;
  float jumpth;                  // JUMP THRESHOLD (minimum aus Xp und z.B. 5%)
  
};

PID pid[PIDSIZE];

// AUTOTUNE
struct AutoTune {
   uint32_t set;                // BETRIEBS-TEMPERATUR
   unsigned long time[3];       // TIME VECTOR
   float temp[3];               // TEMPERATURE VECTOR
   float value;                 // CURRENT AUTOTUNE VALUE
   byte run;                    // WAIT FOR AUTOTUNE START: 0:off, 1:init, 2:run
   byte stop;                   // STOP AUTOTUNE: 1: normal, 2: overtemp, 3: timeout               
   float Kp;
   float Ki;
   float Kd;
   float vmax;
   uint8_t max;               // MAXIMAL AUTOTUNE PITMASTER VALUE
};

AutoTune autotune;

// DUTYCYCLE TEST
struct DutyCycle {
  unsigned long timer;          // SHUTDOWN TIMER
  uint16_t value;               // TEST VALUE * 10
  bool dc;                      // WHICH DC: min or max
  byte aktor;                   // WHICH ACTOR
  int8_t saved;                 // PITMASTER ACTIVITY CACHE (-1 .. 100)
};

DutyCycle dutyCycle[PITMASTERSIZE];



// NOTIFICATION
struct Notification {
  byte index;                       // INDEX BIN
  byte ch;                          // CHANNEL BIN
  byte limit;                       // LIMIT: 0 = LOW TEMPERATURE, 1 = HIGH TEMPERATURE
  byte type;                        // TYPE: 0 = NORMAL MODE, 1 = TEST MESSAGE
};

Notification notification;

// SYSTEM
struct System {
   String unit = "C";         // TEMPERATURE UNIT
   byte hwversion;           // HARDWARE VERSION
   bool pitmaster = true;              // PITMASTER ENABLE
   String apname;             // AP NAME
   String host;                     // HOST NAME
   String language;           // SYSTEM LANGUAGE
   
   bool autoupdate;
   byte god;                  // B0: Startpiepser, B1: nobattery  (eventuell noch typ k integrieren)
   bool pitsupply;        
   bool stby;                   // STANDBY
   bool restartnow; 
   bool typk;
   bool damper;
   bool sendSettingsflag;          // SENDSETTINGS FLAG
   const char* www_username = "admin";
   String www_password = "admin";
   String item;
   byte server_state;         // Server Communication: 0:no, 1:yes
   byte cloud_state;          // Cloud Communication: 0: deaktiviert, 1: Fehler, 2: aktiv

   uint8_t ch;                // Amount of active channels
   int8_t piepoff_t;

   bool transform;            // 12V transformation flag
   bool clientlog;

};

System sys;

// UPDATE
struct myUpdate {
  String firmwareUrl;             // UPDATE FIRMWARE LINK
  String spiffsUrl;               // UPDATE SPIFFS LINK
  byte count;                     // UPDATE SPIFFS REPEAT
  int state;                      // UPDATE STATE: -1 = check, 0 = no, 1 = start spiffs, 2 = check after restart, 3 = firmware, 4 = finish
  String get;                     // UPDATE MY NEW VERSION (über Eingabe)
  String version = "false";       // UPDATE SERVER NEW VERSION
  bool autoupdate;                // CHECK UPDATE INFORMATION
  bool prerelease;                // ?
};

myUpdate update;

byte pulsalarm = 1;

// BATTERY
struct Battery {
  int voltage;                    // CURRENT VOLTAGE
  bool charge;                    // CHARGE DETECTION
  int percentage;                 // BATTERY CHARGE STATE in %
  int setreference;              // LOAD COMPLETE SAVE VOLTAGE
  int max;                        // MAX VOLTAGE
  int min;                        // MIN VOLTAGE
  int correction = 0;   
  byte state;                   // 0:LOAD, 1:SHUTDOWN,  3:COMPLETE
  int sim;                        // SIMULATION VOLTAGE
  byte simc;                      // SIMULATION COUNTER
};

Battery battery;
uint32_t vol_sum = 0;
int vol_count = 0;


// IOT
struct IoT {

  #ifdef THINGSPEAK
   String TS_writeKey;          // THINGSPEAK WRITE API KEY
   String TS_httpKey;           // THINGSPEAK HTTP API KEY 
   String TS_userKey;           // THINGSPEAK USER KEY 
   String TS_chID;              // THINGSPEAK CHANNEL ID 
   bool TS_show8;               // THINGSPEAK SHOW SOC
   int TS_int;                  // THINGSPEAK INTERVAL IN SEC
   bool TS_on;                  // THINGSPEAK ON / OFF
  #endif
  
   String P_MQTT_HOST;          // PRIVATE MQTT BROKER HOST
   uint16_t P_MQTT_PORT;        // PRIVATE MQTT BROKER PORT
   String P_MQTT_USER;          // PRIVATE MQTT BROKER USER
   String P_MQTT_PASS;          // PRIVATE MQTT BROKER PASSWD
   byte P_MQTT_QoS;             // PRIVATE MQTT BROKER QoS
   bool P_MQTT_on;              // PRIVATE MQTT BROKER ON/OFF
   int P_MQTT_int;              // PRIVATE MQTT BROKER IN SEC 
   bool CL_on;                  // NANO CLOUD ON / OFF
   String CL_token;             // NANO CLOUD TOKEN
   int CL_int;                  // NANO CLOUD INTERVALL
};

IoT iot;

bool lastUpdateCloud;


struct PushD {
   byte on;                  // NOTIFICATION SERVICE OFF(0)/ON(1)/TEST(2)/CLEAR(3)
   String token;             // API TOKEN
   String id;                // CHAT ID 
   int repeat;               // REPEAT PUSH NOTIFICATION
   byte service;             // SERVICE
  
};

PushD pushd; 

// CLOUD CHART/LOG
struct Chart {
   bool on = false;                  // NANO CHART ON / OFF
};

Chart chart;

// OLED
int current_ch = 0;               // CURRENTLY DISPLAYED CHANNEL     
bool ladenshow = false;           // LOADING INFORMATION?
bool displayblocked = false;                     // No OLED Update
enum {NO, CONFIGRESET, CHANGEUNIT, OTAUPDATE, HARDWAREALARM, IPADRESSE, TUNE, SYSTEMSTART, RESETWIFI, RESETFW};

// OLED QUESTION
struct MyQuestion {
   int typ;    
   int con;            
};

MyQuestion question;

// FILESYSTEM
enum {eCHANNEL, eWIFI, eTHING, ePIT, eSYSTEM, ePUSH, eSERVER, ePRESET};


struct OpenLid {
   bool detected;         // Open Lid Detected
   float ref[5] = {0.0, 0.0, 0.0, 0.0, 0.0};          // Open Lid Temperatur Memory
   float temp;            // Temperatur by Open Lid
   int  count;            // Open Lid Count
};

OpenLid opl;



// https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WiFi/src/ESP8266WiFiType.h

// WIFI
struct Wifi {
  byte mode;                       // WIFI MODE  (0 = OFF, 1 = STA, 2 = AP, 3/4 = Turn off), 5 = DISCONNECT, 6 = CONNECTING, 7 = OPEN
  unsigned long turnoffAPtimer;    // TURN OFF AP TIMER
  byte savedlen;                   // LENGTH SAVED WIFI DATE
  String savedssid[5];             // SAVED SSID
  String savedpass[5];             // SAVED PASSWORD
  int rssi;                        // BUFFER RSSI
  byte savecount;                  // COUNTER
  unsigned long reconnecttime;
  unsigned long scantime;          // LAST SCAN TIME
  bool disconnectAP;               // DISCONNECT AP
  bool revive;
  bool takeAP;
  long timerAP;
  byte neu;                         // SAVE-MODE: (0 = no save, 1 = only sort, 2 = add new)
  unsigned long mqttreconnect;
};
Wifi wifi;

struct HoldSSID {
   unsigned long connect;           // NEW WIFI DATA TIMER  (-1: in Process)
   byte hold;                       // NEW WIFI DATA      
   String ssid;                     // NEW SSID
   String pass;                     // NEW PASSWORD
};
HoldSSID holdssid;

// BUTTONS
byte buttonPins[]={btn_r,btn_l};          // Pins
#define NUMBUTTONS sizeof(buttonPins)
byte buttonState[NUMBUTTONS];     // Aktueller Status des Buttons HIGH/LOW
enum {NONE, FIRSTDOWN, FIRSTUP, SHORTCLICK, DOUBLECLICK, LONGCLICK};
byte buttonResult[NUMBUTTONS];    // Aktueller Klickstatus der Buttons NONE/SHORTCLICK/LONGCLICK
unsigned long buttonDownTime[NUMBUTTONS]; // Zeitpunkt FIRSTDOWN
byte menu_count = 0;                      // Counter for Menu
byte inMenu = 0;
enum {TEMPSUB, PITSUB, SYSTEMSUB, MAINMENU, TEMPKONTEXT, BACK};
bool inWork = 0;
bool isback = 0;
byte framepos[5] = {0, 2, 3, 1, 4};  // TempSub, PitSub, SysSub, TempKon, Back
byte subframepos[4] = {1, 6, 11, 17};    // immer ein Back dazwischen // menutextde ebenfalls anpassen
int current_frame = 0;  
bool flashinwork = true;
float tempor;                       // Zwischenspeichervariable

// WEBSERVER
AsyncWebServer server(80);        // https://github.com/me-no-dev/ESPAsyncWebServer

// TIMER
unsigned long lastUpdateBatteryMode;

// URL
struct ServerData {
   String host;           // nur die Adresse ohne Anhang
   String page;           // alles was nach de, com etc. kommt  
   String typ; 
};

#ifdef THINGSPEAK
ServerData serverurl[5];     // 0:api, 1: note, 2:cloud
enum {APILINK, NOTELINK, CLOUDLINK, TSLINK, HTTPLINK};
#else
ServerData serverurl[3];     // 0:api, 1: note, 2:cloud
enum {APILINK, NOTELINK, CLOUDLINK};
#endif

enum {NOPARA, TESTPARA, SENDTS, THINGHTTP};                       // Config GET/POST Request


rst_info *myResetInfo;

#ifdef MEMORYCLOUD

  #define CLOUDLOGMAX 2   // Cloud 1/3

  // DATALOGGER
  struct Datalogger {
    uint16_t tem[sys.ch];
    //String color[sys.ch];
    //long timestamp;
    uint8_t value;
    uint16_t set;
    byte status;
    uint8_t soc;
  };

  Datalogger cloudlog[CLOUDLOGMAX];
  int cloudcount;

  void saveLog();
  
#endif

// ++++++++++++++++++++++++++++++++++++++++++++++++++++


// ++++++++++++++++++++++++++++++++++++++++++++++++++++
// PRE-DECLARATION

// INIT
void set_serial();                                // Initialize Serial
void set_button();                                // Initialize Buttons
static inline boolean button_input();             // Dectect Button Input
static inline void button_event();                // Response Button Status
void controlAlarm();                              // Control Hardware Alarm
void set_piepser();
void piepserOFF();
void piepserON();      
void pbguard();

// SENSORS
void set_sensor();                                // Initialize Sensors
int  get_adc_average (byte ch);                   // Reading ADC-Channel Average
void get_Vbat();                                   // Reading Battery Voltage
void cal_soc();

// TEMPERATURE (TEMP)
float calcT(int r, byte typ);                     // Calculate Temperature from ADC-Bytes
void get_Temperature();                           // Reading Temperature ADC
void set_channels(bool init);                              // Initialize Temperature Channels
void transform_limits();                          // Transform Channel Limits

// OLED
#include <SSD1306.h>              
#include <OLEDDisplayUi.h>  
SSD1306 display(OLED_ADRESS, SDA, SCL);
OLEDDisplayUi ui     ( &display );

// FRAMES
void drawConnect();                       // Frane while System Start
void drawLoading();                               // Frame while Loading
void drawQuestion(int counter);                    // Frame while Question
void drawMenu();
void set_OLED();                                  // Configuration OLEDDisplay

// FILESYSTEM (FS)
bool loadfile(const char* filename, File& configFile);
bool savefile(const char* filename, File& configFile);
bool checkjson(JsonVariant json, const char* filename);
bool loadconfig(byte count, bool old);
bool setconfig(byte count, const char* data[2]);
bool modifyconfig(byte count, bool neu);
void start_fs();                                  // Initialize FileSystem
void read_serial(char *buffer);                   // React to Serial Input 
int readline(int readch, char *buffer, int len);  // Put together Serial Input


// MEDIAN
void median_add(int value);                       // add Value to Buffer
void mem_clear(int i);

// OTA
void set_ota();                                   // Configuration OTA
void check_api();
void check_http_update();

// WIFI
void set_wifi();                                  // Connect WiFi
void get_rssi();
void reconnect_wifi();
void stop_wifi();
void check_wifi();
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDHCPTimeout, wifiDisconnectHandler, softAPDisconnectHandler;  
void connectToMqtt();
void EraseWiFiFlash();
void connectWiFi();
String connectionStatus (int which);

//MQTT
AsyncMqttClient pmqttClient;
bool sendpmqtt();
bool sendSettings();
void set_pmqtt(bool ini = false);

// EEPROM
void setEE();
void writeEE(const char* json, int len, int startP);
void readEE(char *buffer, int len, int startP);
void clearEE(int startP, int endP);

// PITMASTER
void startautotunePID(byte id);
void pitmaster_control(byte id);
void disableAllHeater();
void disableHeater(byte id, bool hold = false);
void set_pitmaster(bool init);
void set_pid(byte index);
void stopautotune(byte id);
void DC_start(bool dc, byte aktor, int val, byte id);
void open_lid();
void open_lid_init();

// BOT
void set_iot(bool init);
void set_push();
void sendNotification();
String newToken();

#ifdef THINGSPEAK
String collectData();
String createNote();
#endif

// API
int apiindex;
int urlindex;
int parindex;
void urlObj(JsonObject  &jObj);
void dataObj(JsonObject &jObj, bool cloud);
bool sendAPI(int check);
String apiData(int typ);
enum {NOAPI, APIUPDATE, APICLOUD, APIDATA, APISETTINGS, APINOTE, APIALEXA};

void setWebSocket();

// ++++++++++++++++++++++++++++++++++++++++++++++++++++
// BASIC FUNCTIONS

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Server URLs

void setserverurl(bool init = false) {

  serverurl[0].typ  = "api";
  serverurl[1].typ  = "note";
  serverurl[2].typ  = "cloud";

  #ifdef THINGSPEAK
  serverurl[3].typ  = "thingspeak";
  serverurl[4].typ  = "thinghttp";
  #endif

  if (init) return;
  
  serverurl[0].host = APISERVER;
  serverurl[0].page = CHECKAPI;
  
  serverurl[1].host = APISERVER;
  serverurl[1].page = CHECKAPI;
  
  serverurl[2].host = APISERVER;
  serverurl[2].page = CHECKAPI;
  

  #ifdef THINGSPEAK
  serverurl[3].host = THINGSPEAKSERVER;
  serverurl[3].page = SENDTSLINK;
 
  serverurl[4].host = THINGSPEAKSERVER;
  serverurl[4].page = THINGHTTPLINK;
  #endif
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Initialize Serial
void set_serial() {
  Serial.begin(115200);
  DPRINTLN();
  DPRINTLN();
  IPRINTLN(ESP.getResetReason());

  setserverurl(true);       // Initialize Server URL container

  myResetInfo = ESP.getResetInfoPtr();
  //Serial.printf("myResetInfo->reason %x \n", myResetInfo->reason); // reason is uint32
  
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Check why reset
bool checkResetInfo() {

  // Source: Arduino/cores/esp8266/Esp.cpp
  // Source: Arduino/tools/sdk/include/user_interface.h

  switch (myResetInfo->reason) {

    case REASON_DEFAULT_RST: 
    case REASON_SOFT_RESTART:       // SOFTWARE RESTART
    case REASON_EXT_SYS_RST:          // EXTERNAL (FLASH)
    case REASON_DEEP_SLEEP_AWAKE:     // WAKE UP
      return true;  

    case REASON_EXCEPTION_RST:      // EXEPTION
    case REASON_WDT_RST:            // HARDWARE WDT
    case REASON_SOFT_WDT_RST:       // SOFTWARE WDT
      break;
  }

  return false;
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Initialize System-Settings, if not loaded from EE
void set_system() {
  
  String host = HOSTNAME;
  host += String(ESP.getChipId(), HEX);

  sys.ch = MAXCHANNELS;
  sys.host = host;
  sys.apname = APNAME;
  sys.language = "de";
  //sys.fastmode = false;
  sys.hwversion = 1;
  if (update.state == 0) {
    update.get = "false";   // Änderungen am EE während Update
    update.version = "false";
  }
  update.autoupdate = 1;
  update.firmwareUrl = "";          // wird nur von der API befüllt wenn Update da ist
  update.spiffsUrl = "";
  sys.god = false;
  sys.typk = false;
  battery.max = BATTMAX;
  battery.min = BATTMIN;
  battery.setreference = 0;
  sys.pitsupply = false;           // nur mit Mod
  sys.damper = false;
  sys.restartnow = false;

  update.state = -1;  // Kontakt zur API herstellen
}
//int ci=1;
//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Main Timer

os_timer_t Timer1;         
bool osticker = false;
uint16_t oscounter = 0;

void timerCallback(void *pArg) { 
  osticker = true;
  *((int *) pArg) += 1;
} 

void set_ostimer() {
 os_timer_setfn(&Timer1, timerCallback, &oscounter);
 os_timer_arm(&Timer1, 250, true);
}

void maintimer(bool stby = false) {
  if (osticker) { 

    if (stby) {
      if (!(oscounter % 4)) {     // 1 s
        get_Vbat(); 
        if (!sys.stby) ESP.restart();
      }
    } else {

      if (!(oscounter % 1)) {       // 250 ms
        piepserOFF();
        pulsalarm = !pulsalarm;         // OLED
      }

      // Temperature and Battery Measurement Timer
      if (!(oscounter % INTERVALSENSOR)) {     // 1 s
        get_Temperature();                            // Temperature Measurement
        //ci++; if (ci > 3) ci = 1;
        get_Vbat();                                   // Battery Measurement
        if (millis() < BATTERYSTARTUP) cal_soc();     // schnelles aktualisieren beim Systemstart

        controlAlarm();
      }

      if (battery.state == 3 && (sys.god & (1<<5)) && !(oscounter % 40)) pbguard();     // alle 10s

      // RSSI and kumulative Battery Measurement
      if (!(oscounter % INTERVALBATTERYSIM)) {     // 30 s
        get_rssi();                                   // RSSI Measurement 
        cal_soc();                                    // Kumulative Battery Value
      }

      

      // PRIVATE MQTT
      if (!(oscounter % (iot.P_MQTT_int*4))) {   // variable
        if (wifi.mode == 1 && update.state == 0 && iot.P_MQTT_on) sendpmqtt();
      } 

      // NOTIFICATION (kein Timer notwenig)   (kann noch verbessert werden, siehe sendNotification())
      if (!(oscounter % 1)) { 
        if (wifi.mode == 1 && update.state == 0) sendNotification();
      }

      // NANO CLOUD (nach Notification)
      if (!(oscounter % (iot.CL_int*4)) || lastUpdateCloud) {   // variable
        if (wifi.mode == 1 && update.state == 0 && iot.CL_on) {
          if (sendAPI(0)) {
            apiindex = APICLOUD;
            urlindex = CLOUDLINK;
            parindex = NOPARA;
            sendAPI(2);
          } else {
            #ifdef MEMORYCLOUD  
              cloudcount = 0;           // ansonsten von API zurückgesetzt
            #endif 
          }
        }
        lastUpdateCloud = false;
      }

      #ifdef MEMORYCLOUD        // Zurücksetzen einbauen (CL_int, Temp_einheit ...)
      if (!(oscounter % ((iot.CL_int/3)*4))) {    // 
        if (iot.CL_on && cloudcount < CLOUDLOGMAX) saveLog();
      }
      #endif

      #ifdef THINGSPEAK

      // THINGSPEAK
      if (!(oscounter % (iot.TS_int*4))) {   // variable
        if (wifi.mode == 1 && update.state == 0 && iot.TS_on) {
          if (iot.TS_writeKey != "" && iot.TS_chID != "") {
            if (sendAPI(0)) {
              apiindex = NOAPI;
              urlindex = TSLINK;
              parindex = SENDTS;
              sendAPI(2);
            }
          }
        }
      } 

      #endif

      // ALARM REPEAT
      if (!(oscounter % 60)) {     // 15 s
       for (int i=0; i < sys.ch; i++) {
        if (ch[i].isalarm)  {
          if (ch[i].repeat > 1) {
            ch[i].repeat -= 1;
            ch[i].repeatalarm = true;
          }
        } else ch[i].repeat = pushd.repeat;
       }
      }

      // OLED FLASH TIMER
      if (inWork) {
        if (!(oscounter % FLASHINWORK)) {     // 500 ms
        flashinwork = !flashinwork;
        }
      }  
    }
    osticker = false;
    //Serial.println(oscounter);
    if (oscounter == 2400) oscounter = 0;   // 10 min (muss durch 5, 2, 1, 0,5, 0,25 ganzzahlig teilbar sein)
  }
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Standby oder Mess-Betrieb
bool standby_control() {
  if (sys.stby) {

    drawLoading();                    // Refresh Battery State
    if (!ladenshow) {  
      ladenshow = true;
      IPRINTPLN("Standby");
      //stop_wifi();  // führt warum auch immer bei manchen Nanos zu ständigem Restart
      disableAllHeater();             // Stop Pitmaster
      server.reset();                 // Stop Webserver
      piepserOFF();                   // Stop Pieper
    }

    maintimer(1);                     // Check if Standby
    return 1;
  }
  return 0;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Show time
String printDigits(int digits){
  String com;
  if(digits < 10) com = "0";
  com += String(digits);
  return com;
}

String digitalClockDisplay(time_t t){
  String zeit;
  zeit += printDigits(hour(t))+":";
  zeit += printDigits(minute(t))+":";
  zeit += printDigits(second(t))+" ";
  zeit += String(day(t))+".";
  zeit += String(month(t))+".";
  zeit += String(year(t));
  return zeit;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Date String to Date Element
// Quelle: https://github.com/oh1ko/ESP82666_OLED_clock/blob/master/ESP8266_OLED_clock.ino
tmElements_t * string_to_tm(tmElements_t *tme, char *str) {
  // Sat, 28 Mar 2015 13:53:38 GMT

  const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  char *r, *i, *t;
  r = strtok_r(str, " ", &i);

  r = strtok_r(NULL, " ", &i);
  tme->Day = atoi(r);

  r = strtok_r(NULL, " ", &i);
  for (int i = 0; i < 12; i++) {
    if (!strcmp(months[i], r)) {
      tme->Month = i + 1;
      break;
    }
  }
  
  r = strtok_r(NULL, " ", &i);
  tme->Year = atoi(r) - 1970;

  r = strtok_r(NULL, " ", &i);
  t = strtok_r(r, ":", &i);
  tme->Hour = atoi(t);

  t = strtok_r(NULL, ":", &i);
  tme->Minute = atoi(t);

  t = strtok_r(NULL, ":", &i);
  tme->Second = atoi(t);

  return tme;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Nachkommastellen limitieren
float limit_float(float f, int i) {
  if (i >= 0) {
    if (ch[i].temp!=INACTIVEVALUE) {
      f = f + 0.05;                   // damit er "richtig" rundet, bei 2 nachkommastellen 0.005 usw.
      f = (int)(f*10);               // hier wird der float *10 gerechnet und auf int gecastet, so fallen alle weiteren Nachkommastellen weg
      f = f/10;
    } else f = 999;
  } else {
    f = f + 0.005;
    f = (int)(f*100);
    f = f/100;
  }
  return f;
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// MAC-Adresse
String getMacAddress()  {
  uint8_t mac[6];
  char macStr[18] = { 0 };
  WiFi.macAddress(mac);
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return  String(macStr);
}




// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// GET/POST-Request

enum {SERIALNUMBER, DEVICE, HARDWAREVS, SOFTWAREVS, UPDATEVERSION, TSWRITEKEY, THINGHTTPKEY};  // Parameters

// GET/POST Parameter Generator
String createParameter(int para) {

  String command;
  switch (para) {

    case SERIALNUMBER:
      command += F("serial=");
      command += String(ESP.getChipId(), HEX);
      break;

    case DEVICE:
      command += F("&device=nano");
      break;

    case HARDWAREVS:
      command += F("&hw_version=v");
      command += String(sys.hwversion);
      break;

    case SOFTWAREVS:
      command += F("&sw_version=");
      command += FIRMWAREVERSION;
      break;

    case UPDATEVERSION:
      command += F("&version=");
      command += update.get;
      break;
      
    #ifdef THINGSPEAK
    
    case THINGHTTPKEY:
      command += F("api_key=");
      command += iot.TS_httpKey;
      break;

    case TSWRITEKEY:
      command += F("api_key=");
      command += iot.TS_writeKey;
      break;

    #endif
  }

  return command;
}


//enum {NOPARA, TESTPARA, SENDTS, THINGHTTP};                       // Config
enum {GETMETH, POSTMETH};                                                   // Method

// GET/POST Generator
String createCommand(bool meth, int para, const char * link, const char * host, int content) {

  String command;
  command += meth ? F("POST ") : F("GET ");
  command += String(link);
  command += (para != NOPARA) ? "?" : "";

  switch (para) {

    case TESTPARA:
    break;

    #ifdef THINGSPEAK

    case SENDTS:
      command += createParameter(TSWRITEKEY);
      command += collectData();
      break;

    case THINGHTTP:
      command += createParameter(THINGHTTPKEY);
      command += createNote();
      break;

    #endif

    default:
    break;
      
  }

  command += F(" HTTP/1.1\n");

  if (content > 0) {
    command += F("Content-Type: application/json\n");
    command += F("Content-Length: ");
    command += String(content);
    command += F("\n");
  }

  command += F("User-Agent: WLANThermo nano\n");
  command += F("SN: "); command += String(ESP.getChipId(), HEX); command += F("\n"); 
  command += F("Host: ");
  command += String(host);
  command += F("\n\n");

  return  command;
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Calculate Duty Cycle Milliseconds

uint16_t getDC(uint16_t impuls) {
  // impuls = value * 10  // 1.Nachkommastelle
  float val = ((float)(impuls - SERVOPULSMIN*10)/(SERVOPULSMAX - SERVOPULSMIN))*100;
  return (val < 500)?ceil(val):floor(val);   // nach oben : nach unten
}



