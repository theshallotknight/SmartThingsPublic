#include <ESP8266WiFi.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>
#include <ESP8266SSDP.h>
#include <ESP8266HTTPUpdateServer.h>
ESP8266HTTPUpdateServer httpUpdater;

const char * projectName = "SmartLife RGBW Controller";
String softwareVersion = "2.0.5";
const char compile_date[] = __DATE__ " " __TIME__;

int currentRED   = 0;
int currentGREEN = 0;
int currentBLUE  = 0;
int currentW1    = 0;
int currentW2    = 0;
int lastRED   = 0;
int lastGREEN = 0;
int lastBLUE  = 0;
int lastW1    = 1023;
int lastW2    = 0;

String logStatus = "";

boolean needUpdate = true;

unsigned long connectionFailures;

#define FLASH_EEPROM_SIZE 4096
extern "C" {
#include "spi_flash.h"
}
extern "C" uint32_t _SPIFFS_start;
extern "C" uint32_t _SPIFFS_end;
extern "C" uint32_t _SPIFFS_page;
extern "C" uint32_t _SPIFFS_block;

#define PWM_VALUE 63
int gamma_table[PWM_VALUE+1] = {
    0,16,32,48,64,80,96,112,128,144,160,176,192,208,224,240,256,272,288,304,320,336,
    352,368,384,400,416,432,448,464,480,496,528,544,560,576,592,608,624,640,656,672,
    688,704,720,736,752,768,784,800,816,832,848,864,880,896,912,928,944,960,976,992,
    1008,1023
};

// RGB FET
#define redPIN    15 //12
#define greenPIN  13 //15
#define bluePIN   12 //13

// W FET
#define w1PIN     14
#define w2PIN     4

// onbaord green LED D1
#define LEDPIN    5
// onbaord red LED D2
#define LED2PIN   1

#define KEY_PIN   0

// note 
// TX GPIO2 @Serial1 (Serial ONE)
// RX GPIO3 @Serial    

String program = "";
String program_off = "";
boolean run_program;
int program_step = 1;
int program_counter = 1;
int program_wait = 0;
int program_loop = false;
String program_number = "0";
unsigned long previousMillis = millis();
unsigned long failureTimeout = millis();
unsigned long timerwd;
unsigned long currentmillis=0;
boolean fade = true;
int repeat = 1;
int repeat_count = 1;

//stores if the switch was high before at all
int state = LOW;
//stores the time each button went high or low
unsigned long current_high;
unsigned long current_low;

String       s_Current_WIFISSID                   = "";
String       s_Current_WIFIPW                     = "";

#define DEFAULT_HAIP                   "0.0.0.0"
#define DEFAULT_HAPORT                 39500
#define DEFAULT_RESETWIFI              false
#define DEFAULT_POS                    0
#define DEFAULT_CURRENT STATE          ""
#define DEFAULT_IP                     "0.0.0.0"
#define DEFAULT_GATEWAY                "0.0.0.0"
#define DEFAULT_SUBNET                 "0.0.0.0"
#define DEFAULT_DNS                    "0.0.0.0"
#define DEFAULT_USE_STATIC             false
#define DEFAULT_LONG_PRESS             false
#define DEFAULT_REALLY_LONG_PRESS      false
#define DEFAULT_USE_PASSWORD           false
#define DEFAULT_USE_PASSWORD_CONTROL   false
#define DEFAULT_COLOR                  ""
#define DEFAULT_BAD_BOOT_COUNT         0
#define DEFAULT_RED                    0
#define DEFAULT_GREEN                  0
#define DEFAULT_BLUE                   0
#define DEFAULT_W1                     1023
#define DEFAULT_W2                     0
#define DEFAULT_DISABLE_J3_RESET       false

#define DEFAULT_PASSWORD               ""


struct SettingsStruct
{
  byte          haIP[4];
  unsigned int  haPort;
  boolean       resetWifi;
  int           powerOnState;
  boolean       currentState;
  byte          IP[4];
  byte          Gateway[4];
  byte          Subnet[4];
  byte          DNS[4];
  boolean       useStatic;
  boolean       longPress;
  boolean       reallyLongPress;
  boolean       usePassword;
  boolean       usePasswordControl;
  char          defaultColor[10];
  int           defaultTransition;
  int           badBootCount;
  boolean       disableJ3Reset;
} Settings;

struct SecurityStruct
{
  char          Password[26];
  int           settingsVersion;
  char          ssid[33];
  char          pass[33];
} SecuritySettings;


#define LEDoff digitalWrite(LEDPIN,HIGH)
#define LEDon digitalWrite(LEDPIN,LOW)

#define LED2off digitalWrite(LED2PIN,HIGH)
#define LED2on digitalWrite(LED2PIN,LOW)

int led_delay_red = 0;
int led_delay_green = 0;
int led_delay_blue = 0;
int led_delay_w1 = 0;
int led_delay_w2 = 0;
#define time_at_colour 1300 
unsigned long TIME_LED_RED = 0;
unsigned long TIME_LED_GREEN = 0;
unsigned long TIME_LED_BLUE = 0;
unsigned long TIME_LED_W1 = 0;
unsigned long TIME_LED_W2 = 0;
int RED, GREEN, BLUE, W1, W2; 
int RED_A = 0;
int GREEN_A = 0; 
int BLUE_A = 0;
int W1_A = 0;
int W2_A = 0;

byte mac[6];

// Start WiFi Server
std::unique_ptr<ESP8266WebServer> server;

void handleRoot() {
    server->send(200, "application/json", "{\"message\":\"SmartLife RGBW Controller\"}");
}

void getWIFIConfig() {
  boolean saveSettings = false;
  struct station_config conf;
  wifi_station_get_config(&conf);
  const char            * ssidcstr = reinterpret_cast<const char*>(conf.ssid);
  s_Current_WIFISSID = String(ssidcstr);
  const char            * passphrasecstr = reinterpret_cast<const char*>(conf.password);
  s_Current_WIFIPW = String(passphrasecstr);

  logStatus += "getWIFIConfig: ssid: [";
  logStatus += s_Current_WIFISSID;
  logStatus += "] password: [";
  logStatus += s_Current_WIFIPW;
  logStatus += "]";
  Serial.print("getWIFIConfig: ssid: [");
  Serial.print(s_Current_WIFISSID);
  Serial.print("] password: [");
  Serial.print(s_Current_WIFIPW);
  Serial.println("]");

  if (s_Current_WIFISSID != "") {
    logStatus += "wifiConnectionManagerKT: pre-connection check Found SSID in last wifi config, save to EEPROM...";
    Serial.println("wifiConnectionManagerKT: pre-connection check Found SSID in last wifi config, save to EEPROM...");
    if (s_Current_WIFISSID != SecuritySettings.ssid)
    {
      logStatus += "SecuritySettings.ssid does not match. Need to save to EEPROM";
      logStatus += "SecuritySettings.ssid:";
      logStatus += SecuritySettings.ssid;
      saveSettings = true;
      strncpy(SecuritySettings.ssid, s_Current_WIFISSID.c_str(), sizeof(SecuritySettings.ssid));
    }
    if (s_Current_WIFIPW != SecuritySettings.pass)
    {
      logStatus += "SecuritySettings.pass does not match. Need to save to EEPROM";
      logStatus += "SecuritySettings.pass:";
      logStatus += SecuritySettings.pass;
      saveSettings = true;
      strncpy(SecuritySettings.pass, s_Current_WIFIPW.c_str(), sizeof(SecuritySettings.pass));
    }
    logStatus += "wifiConnectionManagerKT: pre-connection check Save_System_EEPROM...";
    Serial.println("wifiConnectionManagerKT: pre-connection check Save_System_EEPROM...");
    if (saveSettings == true)
    {
      SaveSettings();
    }
  } else if (SecuritySettings.ssid != "") {
    logStatus += "wifiConnectionManagerKT: pre-connection check found values in EEPROM but not in last WIFI config??? Try to set WiFi config from EEPROM...";
    Serial.println("wifiConnectionManagerKT: pre-connection check found values in EEPROM but not in last WIFI config??? Try to set WiFi config from EEPROM...");
    //WiFi.begin(s_EEPROM_WIFISSID.c_str(), s_EEPROM_WIFIPW.c_str() );
    //WiFi.printDiag(Serial);      //Remove this line if you do not want to see WiFi password printed
  } else {
    logStatus += "wifiConnectionManagerKT: no WIFI config found in either last config or EEPROM.";
    Serial.println("wifiConnectionManagerKT: no WIFI config found in either last config or EEPROM.");
  }
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += (server->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";
  for (uint8_t i = 0; i < server->args(); i++) {
    message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
  }
  server->send(404, "text/plain", message);
}

boolean changeColor(String color, int channel, boolean fade, boolean all = false)
{
  boolean success = false;

  switch (channel)
  {
    case 0: // Off
    {

      RED = 0;
      GREEN = 0;
      BLUE = 0;
      W1 = 0;
      W2 = 0;

      ::fade = fade;
      
      change_LED();
      success = true;
      break;
    }
    case 1: //R channel
    {
      RED = getScaledValue(color.substring(0, 2));

      if(all == false){
        W1 = 0;
        W2 = 0;
      }

      lastRED = RED;
      lastGREEN = GREEN;
      lastBLUE = BLUE;
      lastW1 = W1;
      lastW2 = W2;

      ::fade = fade;
      
      change_LED();
      success = true;
      break;
    }
    case 2: //G channel
    {
      GREEN = getScaledValue(color.substring(0, 2));

      if(all == false){
        W1 = 0;
        W2 = 0;
      }

      lastRED = RED;
      lastGREEN = GREEN;
      lastBLUE = BLUE;
      lastW1 = W1;
      lastW2 = W2;
      
      ::fade = fade;
      
      change_LED();
      success = true;
      break;
    }
    case 3: //B channel
    {
      BLUE = getScaledValue(color.substring(0, 2));
      
      if(all == false){
        W1 = 0;
        W2 = 0;
      }

      lastRED = RED;
      lastGREEN = GREEN;
      lastBLUE = BLUE;
      lastW1 = W1;
      lastW2 = W2;
      
      ::fade = fade;
      
      change_LED();
      success = true;
      break;
    }
    case 4: //White1 channel
    {
      if(all == false){
        RED = 0;
        GREEN = 0;
        BLUE = 0;
        W2 = 0;
      }

      W1 = getScaledValue(color.substring(0, 2));

      lastRED = RED;
      lastGREEN = GREEN;
      lastBLUE = BLUE;
      lastW1 = W1;
      lastW2 = W2;

      ::fade = fade;
      
      change_LED();
      success = true;
      break;
    }
    case 5: //White2 channel
    {
      if(all == false){
        RED = 0;
        GREEN = 0;
        BLUE = 0;
        W2 = 0;
      }

      W2 = getScaledValue(color.substring(0, 2));

      lastRED = RED;
      lastGREEN = GREEN;
      lastBLUE = BLUE;
      lastW1 = W1;
      lastW2 = W2;
      
      ::fade = fade;
      
      change_LED();
      success = true;
      break;
    }
    case 6: //RGB channel
    {
      if (color == "xxxxxx") {
        RED = rand_interval(0,1023);
        GREEN = rand_interval(0,1023);
        BLUE = rand_interval(0,1023);
      } else {
        RED = getScaledValue(color.substring(0, 2));
        GREEN = getScaledValue(color.substring(2, 4));
        BLUE = getScaledValue(color.substring(4, 6));
      }
      if(all == false){
        W1 = 0;
        W2 = 0;
      }

      lastRED = RED;
      lastGREEN = GREEN;
      lastBLUE = BLUE;
      lastW1 = W1;
      lastW2 = W2;
      
      ::fade = fade;
      
      change_LED();
      success = true;
      break;
    }
    case 99: // On
    {

      RED = lastRED;
      GREEN = lastGREEN;
      BLUE = lastBLUE;
      W1 = lastW1;
      W2 = lastW2;

      ::fade = fade;
      
      change_LED();
      success = true;
      break;
    }
    
  }
  return success;

}

unsigned int rand_interval(unsigned int min, unsigned int max)
{
    int r;
    const unsigned int range = 1 + max - min;
    const unsigned int buckets = RAND_MAX / range;
    const unsigned int limit = buckets * range;

    /* Create equal size buckets all in a row, then fire randomly towards
     * the buckets until you land in one of them. All buckets are equally
     * likely. If you land off the end of the line of buckets, try again. */
    do
    {
        r = rand();
    } while (r >= limit);

    return min + (r / buckets);
}

void relayToggle(){
  if(digitalRead(KEY_PIN) == LOW){
      current_low = millis();
      state = LOW;
  }
  if(digitalRead(KEY_PIN == HIGH) && state == LOW)
    {
      current_high = millis();
      if((current_high - current_low) > 20 && (current_high - current_low) < 10000)
      {
        state = HIGH;
        run_program = false;
        if(getHex(RED) + getHex(GREEN) + getHex(BLUE) + getHex(W1) + getHex(W2) == "0000000000") {
          String hexString(Settings.defaultColor);
          if(hexString == "") {
            changeColor("0000000000", 99, true);
          } else if(hexString == "Previous") {
            changeColor("0000000000", 99, true);
          } else {
            if(hexString.startsWith("w~")) {
              String hex = hexString.substring(2, 4);
              changeColor(hex, 4, true); 
            }else if(hexString.startsWith("x~")){
              String hex = hexString.substring(2, 4);
              changeColor(hex, 4, false);
            }else if(hexString.startsWith("f~")){
              String hex = hexString.substring(2, 8);
              changeColor(hex, 6, true);
            }else{
              String hex = hexString.substring(2, 8);
              changeColor("0000000000", 99, true);
            }
          }
        } else {
          changeColor("0000000000", 0, true);
        }
        needUpdate = true;
      }
      else if((current_high - current_low) >= 10000 && (current_high - current_low) < 20000)
      {
        if (!Settings.disableJ3Reset){
          Settings.longPress = true;
          SaveSettings();
          ESP.restart();
        }
      }
      else if((current_high - current_low) >= 20000 && (current_high - current_low) < 60000)
      {
        if (!Settings.disableJ3Reset){
          Settings.reallyLongPress = true;
          SaveSettings();
          ESP.restart();
        }
      }
    }
}

const char * endString(int s, const char *input) {
   int length = strlen(input);
   if ( s > length ) s = length;
   return const_cast<const char *>(&input[length-s]);
}

void change_LED()
{
  int diff_red = abs(RED-RED_A);
  if(diff_red > 0){
    led_delay_red = time_at_colour / abs(RED-RED_A); 
  }else{
    led_delay_red = time_at_colour / 1023; 
  }
  
  int diff_green = abs(GREEN-GREEN_A);
  if(diff_green > 0){
    led_delay_green = time_at_colour / abs(GREEN-GREEN_A);
  }else{
    led_delay_green = time_at_colour / 1023; 
  }
  
  int diff_blue = abs(BLUE-BLUE_A);
  if(diff_blue > 0){
    led_delay_blue = time_at_colour / abs(BLUE-BLUE_A); 
  }else{
    led_delay_blue = time_at_colour / 1023; 
  }
  
  int diff_w1 = abs(W1-W1_A);
  if(diff_w1 > 0){
    led_delay_w1 = time_at_colour / abs(W1-W1_A); 
  }else{
    led_delay_w1 = time_at_colour / 1023; 
  }
  
  int diff_w2 = abs(W2-W2_A);
  if(diff_w2 > 0){
    led_delay_w2 = time_at_colour / abs(W2-W2_A); 
  }else{
    led_delay_w2 = time_at_colour / 1023; 
  }
}

void LED_RED()
{
  if (fade){
    if(RED != RED_A){
      if(RED_A > RED) RED_A = RED_A - 1;
      if(RED_A < RED) RED_A++;
      analogWrite(redPIN, RED_A);
      currentRED=RED_A;
    }
  } else {
    RED_A = RED;
    analogWrite(redPIN, RED);
    currentRED=RED;
  }
}

void LED_GREEN()
{
  if (fade){
    if(GREEN != GREEN_A){
      if(GREEN_A > GREEN) GREEN_A = GREEN_A - 1;
      if(GREEN_A < GREEN) GREEN_A++;
      analogWrite(greenPIN, GREEN_A);
      currentGREEN=GREEN_A;
    }
  } else {
      GREEN_A = GREEN;
      analogWrite(greenPIN, GREEN);
      currentGREEN=GREEN;
  }
}
  
void LED_BLUE()
{
  if (fade){
    if(BLUE != BLUE_A){
      if(BLUE_A > BLUE) BLUE_A = BLUE_A - 1;
      if(BLUE_A < BLUE) BLUE_A++;
      analogWrite(bluePIN, BLUE_A);
      currentBLUE=BLUE_A;
    }
  } else {
      BLUE_A = BLUE;
      analogWrite(bluePIN, BLUE);
      currentBLUE=BLUE;
  }
}

void LED_W1()
{
  if (fade){
    if(W1 != W1_A){
      if(W1_A > W1) W1_A = W1_A - 1;
      if(W1_A < W1) W1_A++;
      analogWrite(w1PIN, W1_A);
      currentW1=W1_A;
    }
  } else {
      W1_A = W1;
      analogWrite(w1PIN, W1);
      currentW1=W1;
  }
}

void LED_W2()
{
  if (fade){
    if(W2 != W2_A){
      if(W2_A > W2) W2_A = W2_A - 1;
      if(W2_A < W2) W2_A++;
      analogWrite(w2PIN, W2_A);
      currentW2=W2_A;
    }
  } else {
      W2_A = W2;
      analogWrite(w2PIN, W2);
      currentW2=W2;
  }
}

int convertToInt(char upper,char lower)
{
  int uVal = (int)upper;
  int lVal = (int)lower;
  uVal = uVal >64 ? uVal - 55 : uVal - 48;
  uVal = uVal << 4;
  lVal = lVal >64 ? lVal - 55 : lVal - 48;
  return uVal + lVal;
}

String getStatus(){
    if(getHex(RED) + getHex(GREEN) + getHex(BLUE) + getHex(W1) + getHex(W2) == "0000000000") {
      return "{\"rgb\":\"" + getHex(RED) + getHex(GREEN) + getHex(BLUE) + "\", \"r\":\"" + getHex(RED) + "\", \"g\":\"" + getHex(GREEN) + "\", \"b\":\"" + getHex(BLUE) + "\", \"w1\":\"" + getHex(W1) + "\", \"w2\":\"" + getHex(W2) + "\", \"power\":\"off\", \"running\":\"false\", \"program\":\"" + program_number + "\", \"uptime\":\"" + uptime() + "\", \"version\":\"" + softwareVersion + "\", \"date\":\"" + compile_date + "\"}";
    } else if(run_program){
      return "{\"running\":\"true\", \"program\":\"" + program_number + "\", \"power\":\"on\", \"uptime\":\"" + uptime() + "\"}";
    }else{
      return "{\"rgb\":\"" + getHex(RED) + getHex(GREEN) + getHex(BLUE) + "\", \"r\":\"" + getHex(RED) + "\", \"g\":\"" + getHex(GREEN) + "\", \"b\":\"" + getHex(BLUE) + "\", \"w1\":\"" + getHex(W1) + "\", \"w2\":\"" + getHex(W2) + "\", \"power\":\"on\", \"running\":\"false\", \"program\":\"" + program_number + "\", \"uptime\":\"" + uptime() + "\", \"version\":\"" + softwareVersion + "\", \"date\":\"" + compile_date + "\"}";
    }
}

int getScaledValue(String hex){
  hex.toUpperCase();
  char c[2];
  hex.toCharArray(c,3);
  long value = convertToInt(c[0],c[1]);

  int intValue = map(value,0,255,0,PWM_VALUE); 
  intValue = constrain(intValue,0,PWM_VALUE);

  return gamma_table[intValue];

}

int getInt(String hex){
  hex.toUpperCase();
  char c[2];
  hex.toCharArray(c,3);
  return convertToInt(c[0],c[1]);
}

String getHex(int value){
  if(value >= 1020){
    return "ff"; 
  }else{
    return padHex(String(round(value*4/16), HEX));
  }
}

String getStandard(int value){
  if(value >= 1020){
    return "255"; 
  }else{
    return String(round(value*4/16));
  }
}

String padHex(String hex){
  if(hex.length() == 1){
    hex = "0" + hex;
  }
  return hex;
}

/*********************************************************************************************\
 * Tasks each 5 minutes
\*********************************************************************************************/
void runEach5Minutes()
{
  //timerwd = millis() + 1800000;
  timerwd = millis() + 300000;

  sendStatus();
  
}

boolean sendStatus(){
  String authHeader = "";
  boolean success = false;
  char host[20];
  sprintf_P(host, PSTR("%u.%u.%u.%u"), Settings.haIP[0], Settings.haIP[1], Settings.haIP[2], Settings.haIP[3]);
  
  //client.setTimeout(1000);
  if (Settings.haIP[0] + Settings.haIP[1] + Settings.haIP[2] + Settings.haIP[3] == 0) { // HA host is not configured
    return false;
  }
  if (connectionFailures >= 3) { // Too many errors; Trying not to get stuck
    if (millis() - failureTimeout < 1800000) {
      return false;
    } else {
      failureTimeout = millis();
    }
  }
  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  if (!client.connect(host, Settings.haPort))
  {
    connectionFailures++;
    return false;
  }
  if (connectionFailures)
    connectionFailures = 0;

  // We now create a URI for the request
  String url = F("/");
  //url += event->idx;

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + ":" + Settings.haPort + "\r\n" + authHeader + 
               "Content-Type: application/json;charset=utf-8\r\n" +
               "Server: " + projectName + "\r\n" +
               "Connection: close\r\n\r\n" +
               getStatus() + "\r\n");

  unsigned long timer = millis() + 200;
  while (!client.available() && millis() < timer)
    delay(1);

  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line.substring(0, 15) == "HTTP/1.1 200 OK")
    {
      success = true;
    }
    delay(1);
  }
  
  client.flush();
  client.stop();

  return success;
  
}

/********************************************************************************************\
  Convert a char string to IP byte array
  \*********************************************************************************************/
boolean str2ip(char *string, byte* IP)
{
  byte c;
  byte part = 0;
  int value = 0;

  for (int x = 0; x <= strlen(string); x++)
  {
    c = string[x];
    if (isdigit(c))
    {
      value *= 10;
      value += c - '0';
    }

    else if (c == '.' || c == 0) // next octet from IP address
    {
      if (value <= 255)
        IP[part++] = value;
      else
        return false;
      value = 0;
    }
    else if (c == ' ') // ignore these
      ;
    else // invalid token
      return false;
  }
  if (part == 4) // correct number of octets
    return true;
  return false;
}

void SaveSettings(void)
{
  SaveToFlash(0, (byte*)&Settings, sizeof(struct SettingsStruct));
  SaveToFlash(32768, (byte*)&SecuritySettings, sizeof(struct SecurityStruct));
}

boolean LoadSettings()
{
  LoadFromFlash(0, (byte*)&Settings, sizeof(struct SettingsStruct));
  LoadFromFlash(32768, (byte*)&SecuritySettings, sizeof(struct SecurityStruct));
}

/********************************************************************************************\
  Save data to flash
  \*********************************************************************************************/
void SaveToFlash(int index, byte* memAddress, int datasize)
{
  if (index > 33791) // Limit usable flash area to 32+1k size
  {
    return;
  }
  uint32_t _sector = ((uint32_t)&_SPIFFS_start - 0x40200000) / SPI_FLASH_SEC_SIZE;
  uint8_t* data = new uint8_t[FLASH_EEPROM_SIZE];
  int sectorOffset = index / SPI_FLASH_SEC_SIZE;
  int sectorIndex = index % SPI_FLASH_SEC_SIZE;
  uint8_t* dataIndex = data + sectorIndex;
  _sector += sectorOffset;

  // load entire sector from flash into memory
  noInterrupts();
  spi_flash_read(_sector * SPI_FLASH_SEC_SIZE, reinterpret_cast<uint32_t*>(data), FLASH_EEPROM_SIZE);
  interrupts();

  // store struct into this block
  memcpy(dataIndex, memAddress, datasize);

  noInterrupts();
  // write sector back to flash
  if (spi_flash_erase_sector(_sector) == SPI_FLASH_RESULT_OK)
    if (spi_flash_write(_sector * SPI_FLASH_SEC_SIZE, reinterpret_cast<uint32_t*>(data), FLASH_EEPROM_SIZE) == SPI_FLASH_RESULT_OK)
    {
      //Serial.println("flash save ok");
    }
  interrupts();
  delete [] data;
  //String log = F("FLASH: Settings saved");
  //addLog(LOG_LEVEL_INFO, log);
}


/********************************************************************************************\
  Load data from flash
  \*********************************************************************************************/
void LoadFromFlash(int index, byte* memAddress, int datasize)
{
  uint32_t _sector = ((uint32_t)&_SPIFFS_start - 0x40200000) / SPI_FLASH_SEC_SIZE;
  uint8_t* data = new uint8_t[FLASH_EEPROM_SIZE];
  int sectorOffset = index / SPI_FLASH_SEC_SIZE;
  int sectorIndex = index % SPI_FLASH_SEC_SIZE;
  uint8_t* dataIndex = data + sectorIndex;
  _sector += sectorOffset;

  // load entire sector from flash into memory
  noInterrupts();
  spi_flash_read(_sector * SPI_FLASH_SEC_SIZE, reinterpret_cast<uint32_t*>(data), FLASH_EEPROM_SIZE);
  interrupts();

  // load struct from this block
  memcpy(memAddress, dataIndex, datasize);
  delete [] data;
}



String uptime()
{
 currentmillis = millis();
 long days=0;
 long hours=0;
 long mins=0;
 long secs=0;
 secs = currentmillis/1000; //convect milliseconds to seconds
 mins=secs/60; //convert seconds to minutes
 hours=mins/60; //convert minutes to hours
 days=hours/24; //convert hours to days
 secs=secs-(mins*60); //subtract the coverted seconds to minutes in order to display 59 secs max 
 mins=mins-(hours*60); //subtract the coverted minutes to hours in order to display 59 minutes max
 hours=hours-(days*24); //subtract the coverted hours to days in order to display 23 hours max
 

  if (days>0) // days will displayed only if value is greater than zero
 {
   return String(days) + " days and " + String(hours) + ":" + String(mins) + ":" + String(secs);
 }else{
   return String(hours) + ":" + String(mins) + ":" + String(secs);
 }
}



void EraseFlash()
{
  uint32_t _sectorStart = (ESP.getSketchSize() / SPI_FLASH_SEC_SIZE) + 1;
  uint32_t _sectorEnd = _sectorStart + (ESP.getFlashChipRealSize() / SPI_FLASH_SEC_SIZE);

  for (uint32_t _sector = _sectorStart; _sector < _sectorEnd; _sector++)
  {
    noInterrupts();
    if (spi_flash_erase_sector(_sector) == SPI_FLASH_RESULT_OK)
    {
      interrupts();
      Serial.print(F("FLASH: Erase Sector: "));
      Serial.println(_sector);
      delay(10);
    }
    interrupts();
  }
}

void ZeroFillFlash()
{
  // this will fill the SPIFFS area with a 64k block of all zeroes.
  uint32_t _sectorStart = ((uint32_t)&_SPIFFS_start - 0x40200000) / SPI_FLASH_SEC_SIZE;
  uint32_t _sectorEnd = _sectorStart + 16 ; //((uint32_t)&_SPIFFS_end - 0x40200000) / SPI_FLASH_SEC_SIZE;
  uint8_t* data = new uint8_t[FLASH_EEPROM_SIZE];

  uint8_t* tmpdata = data;
  for (int x = 0; x < FLASH_EEPROM_SIZE; x++)
  {
    *tmpdata = 0;
    tmpdata++;
  }


  for (uint32_t _sector = _sectorStart; _sector < _sectorEnd; _sector++)
  {
    // write sector to flash
    noInterrupts();
    if (spi_flash_erase_sector(_sector) == SPI_FLASH_RESULT_OK)
      if (spi_flash_write(_sector * SPI_FLASH_SEC_SIZE, reinterpret_cast<uint32_t*>(data), FLASH_EEPROM_SIZE) == SPI_FLASH_RESULT_OK)
      {
        interrupts();
        Serial.print(F("FLASH: Zero Fill Sector: "));
        Serial.println(_sector);
        delay(10);
      }
  }
  interrupts();
  delete [] data;
}

void addHeader(boolean showMenu, String& str)
{
  boolean cssfile = false;

  str += F("<script language=\"javascript\"><!--\n");
  str += F("function dept_onchange(frmselect) {frmselect.submit();}\n");
  str += F("//--></script>");
  str += F("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/><title>");
  str += projectName;
  str += F("</title>");

  str += F("<style>");
  str += F("* {font-family:sans-serif; font-size:12pt;}");
  str += F("h1 {font-size:16pt; color:black;}");
  str += F("h6 {font-size:10pt; color:black; text-align:center;}");
  str += F(".button-menu {background-color:#ffffff; color:#000000; margin: 10px; text-decoration:none}");
  str += F(".button-link {padding:5px 15px; background-color:#000000; color:#fff; border:solid 1px #fff; text-decoration:none}");
  str += F(".button-menu:hover {background:#ddddff;}");
  str += F(".button-link:hover {background:#707070;}");
  str += F("th {padding:10px; background-color:black; color:#ffffff;}");
  str += F("td {padding:7px;}");
  str += F("table {color:black;}");
  str += F(".div_l {float: left;}");
  str += F(".div_r {float: right; margin: 2px; padding: 1px 10px; border-radius: 7px; background-color:#080; color:white;}");
  str += F(".div_br {clear: both;}");
  str += F("</style>");


  str += F("</head>");
  str += F("<center>");
  
}

void addFooter(String& str)
{
  str += F("<h6><a href=\"http://smartlife.tech\">smartlife.tech</a></h6></body></center>");
}

void addMenu(String& str)
{
  str += F("<a class=\"button-menu\" href=\".\">Main</a>");
  str += F("<a class=\"button-menu\" href=\"advanced\">Advanced</a>");
  str += F("<a class=\"button-menu\" href=\"control\">Control</a>");
  str += F("<a class=\"button-menu\" href=\"update\">Firmware</a>"); 
}

void setup()
{
  
  pinMode(LEDPIN, OUTPUT);  
  pinMode(LED2PIN, OUTPUT);  
  
  pinMode(redPIN, OUTPUT);
  pinMode(greenPIN, OUTPUT);
  pinMode(bluePIN, OUTPUT);
  pinMode(w1PIN, OUTPUT);
  pinMode(w2PIN, OUTPUT);    
  pinMode(KEY_PIN, INPUT_PULLUP);
  attachInterrupt(KEY_PIN, relayToggle, CHANGE);
  
  // Setup console
  Serial1.begin(115200);
  delay(10);
  Serial1.println();
  Serial1.println();

  LoadSettings();

  if (Settings.badBootCount == 0) {
  switch (Settings.powerOnState)
  {
    case 0: //Switch Off on Boot
    {
      break;
    }
    case 1: //Switch On on Boot
    {
      String hexString(Settings.defaultColor);
      if(hexString == "") {
        changeColor("0000000000", 99, false);
      } else if(hexString == "Previous") {
        changeColor("0000000000", 99, false);
      } else {
        if(hexString.startsWith("w~")) {
          String hex = hexString.substring(2, 4);
          changeColor(hex, 4, false); 
        }else if(hexString.startsWith("x~")){
          String hex = hexString.substring(2, 4);
          changeColor(hex, 4, false);
        }else if(hexString.startsWith("f~")){
          String hex = hexString.substring(2, 8);
          changeColor(hex, 6, false);
        }else{
          String hex = hexString.substring(2, 8);
          changeColor("0000000000", 99, false);
        }
      }
      LED_RED();
      LED_GREEN();
      LED_BLUE();
      LED_W1();
      LED_W2();
      break;
    }
    case 2: //Saved State on Boot
    {
      if(Settings.currentState == true){
         
      }
      else {
        
      }
      break;
    }
    default : //Optional
    {
       
    }
  }
  }

  if (Settings.badBootCount == 1){ changeColor("ff", 2, false); LED_GREEN(); }
  if (Settings.badBootCount == 2){ changeColor("ff", 3, false); LED_BLUE(); }
  if (Settings.badBootCount >= 3){ changeColor("ff", 1, false); LED_RED(); }

  Settings.badBootCount += 1;
  SaveSettings();
  
  delay(5000);

  if (Settings.badBootCount > 3) {
    Settings.reallyLongPress = true;
  }

  if(Settings.longPress == true){
    for (uint8_t i = 0; i < 3; i++) {
      LEDoff;
      delay(250);
      LEDon;
      delay(250);
    }
    Settings.longPress = false;
    Settings.useStatic = false;
    Settings.resetWifi = true;
    SaveSettings();
    LEDoff;
  }

  if(Settings.reallyLongPress == true){
    for (uint8_t i = 0; i < 5; i++) {
      LEDoff;
      delay(1000);
      LEDon;
      delay(1000);
    }
    EraseFlash();
    ZeroFillFlash();
    ESP.restart();
  }

  //analogWrite(greenPIN, 0);
  //analogWrite(bluePIN, 0);
  //analogWrite(redPIN, 0);

  boolean saveSettings = false;

  if (Settings.badBootCount != 0){
    Settings.badBootCount = 0;
    saveSettings = true;
  }

  if (SecuritySettings.settingsVersion < 201){
    str2ip((char*)DEFAULT_HAIP, Settings.haIP);

    Settings.haPort = DEFAULT_HAPORT;

    Settings.resetWifi = DEFAULT_RESETWIFI;

    SecuritySettings.settingsVersion = 201;

    saveSettings = true;
  }

  if (SecuritySettings.settingsVersion < 202){
    
    Settings.powerOnState = DEFAULT_POS;

    str2ip((char*)DEFAULT_IP, Settings.IP);
    str2ip((char*)DEFAULT_SUBNET, Settings.Subnet);

    str2ip((char*)DEFAULT_GATEWAY, Settings.Gateway);

    Settings.useStatic = DEFAULT_USE_STATIC;

    Settings.usePassword = DEFAULT_USE_PASSWORD;

    Settings.usePasswordControl = DEFAULT_USE_PASSWORD_CONTROL;

    Settings.longPress = DEFAULT_LONG_PRESS;
    Settings.reallyLongPress = DEFAULT_REALLY_LONG_PRESS;

    strncpy(SecuritySettings.Password, DEFAULT_PASSWORD, sizeof(SecuritySettings.Password));

    SecuritySettings.settingsVersion = 202;

    saveSettings = true;
  }

  if (SecuritySettings.settingsVersion < 203){

    strncpy(Settings.defaultColor, DEFAULT_COLOR, sizeof(Settings.defaultColor));

    SecuritySettings.settingsVersion = 203;

    saveSettings = true;
  }

  if (SecuritySettings.settingsVersion < 204){
    
    Settings.badBootCount = DEFAULT_BAD_BOOT_COUNT;

    SecuritySettings.settingsVersion = 204;

    saveSettings = true;
  }

  if (SecuritySettings.settingsVersion < 205){
    
    Settings.disableJ3Reset = DEFAULT_DISABLE_J3_RESET;

    SecuritySettings.settingsVersion = 205;

    saveSettings = true;
  }

  
  if (saveSettings == true){
    SaveSettings();
  }
  
  WiFiManager wifiManager;

  //wifiManager.setConnectTimeout(60);
  //wifiManager.setConfigPortalTimeout(180);

  if(Settings.useStatic == true){
    wifiManager.setSTAStaticIPConfig(Settings.IP, Settings.Gateway, Settings.Subnet);
  }

  if (Settings.resetWifi == true){
    wifiManager.resetSettings();
    Settings.resetWifi = false;
    SaveSettings();
  }

  LEDon;
  LED2off;
  
  WiFi.macAddress(mac);
  String apSSID = "espRGBW." + String(mac[0],HEX) + String(mac[1],HEX) + String(mac[2],HEX) + String(mac[3],HEX) + String(mac[4],HEX) + String(mac[5],HEX);

  //getWIFIConfig();

  if(SecuritySettings.pass && SecuritySettings.ssid)
  {
    logStatus += "Found ssid and pass in EEPROM settings. Telling wifiManager to use them. [" + String(SecuritySettings.ssid) + "] [" + String(SecuritySettings.pass) + "]";
    wifiManager.setCredentials(SecuritySettings.ssid, SecuritySettings.pass);
  }

  if (!wifiManager.autoConnect(apSSID.c_str(), "configme")) {
    Serial.println("failed to connect, we should reset as see if it connects");
    LED2on;
    delay(500);
    LED2off;
    delay(3000);
    LED2on;
    delay(500);
    LED2off;
    ESP.restart();
  }

  getWIFIConfig();

  LED2on;
  
  Serial1.println("");
  server.reset(new ESP8266WebServer(WiFi.localIP(), 80));

  //server->on("/", handleRoot);

  server->on("/reset", []() {
    server->send(200, "application/json", "{\"message\":\"wifi settings are being removed\"}");
    Settings.reallyLongPress = true;
    SaveSettings();
    ESP.restart();
  });


  server->on("/description.xml", HTTP_GET, [](){
      SSDP.schema(server->client());
    });

  server->on("/reboot", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    server->send(200, "application/json", "{\"message\":\"device is rebooting\"}");
    ESP.restart();
  });

  server->on("/", []() {

    char tmpString[64];

    String reply = "";
    char str[20];
    addHeader(true, reply);
    reply += F("<table>");
    reply += F("<TH colspan='2'>"); 
    reply += projectName; 
    reply += F(" Main");
    reply += F("<TR><TD><TD><TR><TD colspan='2'>");
    addMenu(reply);

    reply += F("<TR><TD><TD><TR><TD>Main:");
    
    reply += F("<TD><a href='/advanced'>Advanced Config</a><BR>");
    reply += F("<a href='/control'>Control</a><BR>");
    reply += F("<a href='/update'>Firmware Update</a><BR>");
    reply += F("<a href='http://tiny.cc/wzwzdy'>Documentation</a><BR>");
    reply += F("<a href='/reboot'>Reboot</a><BR>");

    reply += F("<TR><TD>JSON Endpoints:");

    reply += F("<TD><a href='/status'>status</a><BR>");
    reply += F("<a href='/config'>config</a><BR>");
    reply += F("<a href='/rgb'>rgb</a><BR>");
    reply += F("<a href='/r'>r</a><BR>");
    reply += F("<a href='/g'>g</a><BR>");
    reply += F("<a href='/b'>b</a><BR>");
    reply += F("<a href='/w1'>w1</a><BR>");
    reply += F("<a href='/w2'>w2</a><BR>");
    reply += F("<a href='/off'>off</a><BR>");
    reply += F("<a href='/program'>program</a><BR>");
    reply += F("<a href='/stop'>stop</a><BR>");
    reply += F("<a href='/info'>info</a><BR>");
    reply += F("<a href='/reboot'>reboot</a><BR>");

    reply += F("</table>");
    addFooter(reply);
    server->send(200, "text/html", reply);
  });

  server->on("/info", []() {
    server->send(200, "application/json", "{\"version\":\"" + softwareVersion + "\", \"date\":\"" + compile_date + "\", \"mac\":\"" + padHex(String(mac[0],HEX)) + padHex(String(mac[1],HEX)) + padHex(String(mac[2],HEX)) + padHex(String(mac[3],HEX)) + padHex(String(mac[4],HEX)) + padHex(String(mac[5],HEX)) + "\"}");  
  });

  server->on("/program", []() {
    program = server->arg("value");
    repeat = server->arg("repeat").toInt();
    program_number = server->arg("number");
    program_off = server->arg("off");
    repeat_count = 1;
    program_wait = 0;
    run_program = true;
    server->send(200, "application/json", "{\"running\":\"true\", \"program\":\"" + program_number + "\", \"power\":\"on\"}");
  });

  server->on("/stop", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    run_program = false;
    server->send(200, "application/json", "{\"program\":\"" + program_number + "\", \"running\":\"false\"}");
  });

  server->on("/off", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    String transition = server->arg("transition");
    run_program = false;
    changeColor("00000000", 0, (transition != "false"));

    server->send(200, "application/json", getStatus());
  });

  server->on("/on", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    String transition = server->arg("transition");
    run_program = false;

    String hexString(Settings.defaultColor);
    if(hexString == "") {
      changeColor("0000000000", 99, (transition != "false"));
    } else if(hexString == "Previous") {
      changeColor("0000000000", 99, (transition != "false"));
    } else {
      if(hexString.startsWith("w~")) {
        String hex = hexString.substring(2, 4);
        changeColor(hex, 4, (transition != "false")); 
      }else if(hexString.startsWith("x~")){
        String hex = hexString.substring(2, 4);
        changeColor(hex, 4, (transition != "false"));
      }else if(hexString.startsWith("f~")){
        String hex = hexString.substring(2, 8);
        changeColor(hex, 6, (transition != "false"));
      }else{
        String hex = hexString.substring(2, 8);
        changeColor("0000000000", 99, (transition != "false"));
      }
    }
    
    server->send(200, "application/json", getStatus());
  });
  

  server->on("/status", []() {
    server->send(200, "application/json", getStatus() + logStatus);  
  });

    server->on("/config", []() {
    char tmpString[64];
    boolean success = false;
    String haIP = server->arg("haip");
    String haPort = server->arg("haport");
    String powerOnState = server->arg("pos");
    String dColor = server->arg("dcolor");
    
    if (haIP.length() != 0)
    {
      haIP.toCharArray(tmpString, 26);
      success = str2ip(tmpString, Settings.haIP);
    }
    if (haPort.length() != 0)
    {
      Settings.haPort = haPort.toInt();
      success = true;
    }
    if (powerOnState.length() != 0)
    {
      Settings.powerOnState = powerOnState.toInt();
      success = true;
    }
    if (dColor.length() != 0)
    {
      strncpy(Settings.defaultColor, dColor.c_str(), sizeof(Settings.defaultColor));
      success = true;
    }

    if (success == true) {
      SaveSettings();
      server->send(200, "application/json", "{\"haIP\":\"" + haIP + "\", \"haPort\":\"" + haPort +  "\", \"success\":\"true\"}");
    } else {
      server->send(200, "application/json", "{\"success\":\"false\"}");
    }
  });

  server->on("/advanced", []() {

    if (SecuritySettings.Password[0] != 0 && Settings.usePassword == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }

    char tmpString[64];
    String haIP = server->arg("haip");
    String haPort = server->arg("haport");
    String powerOnState = server->arg("pos");
    String ip = server->arg("ip");
    String gateway = server->arg("gateway");
    String subnet = server->arg("subnet");
    String dns = server->arg("dns");
    String usestatic = server->arg("usestatic");
    String usepassword = server->arg("usepassword");
    String usepasswordcontrol = server->arg("usepasswordcontrol");
    String username = server->arg("username");
    String password = server->arg("password");
    String disableJ3reset = server->arg("disableJ3reset");

    if (haPort.length() != 0)
    {
      Settings.haPort = haPort.toInt();
    }

    if (powerOnState.length() != 0)
    {
      Settings.powerOnState = powerOnState.toInt();
    }
    
    if (haIP.length() != 0)
    {
      haIP.toCharArray(tmpString, 26);
      str2ip(tmpString, Settings.haIP);
    }


    if (ip.length() != 0 && subnet.length() != 0)
    {
      ip.toCharArray(tmpString, 26);
      str2ip(tmpString, Settings.IP);
      subnet.toCharArray(tmpString, 26);
      str2ip(tmpString, Settings.Subnet);
    }

    if (gateway.length() != 0)
    {
      gateway.toCharArray(tmpString, 26);
      str2ip(tmpString, Settings.Gateway);
    }

    if (dns.length() != 0)
    {
      dns.toCharArray(tmpString, 26);
      str2ip(tmpString, Settings.DNS);
    }

    if (usestatic.length() != 0)
    {
      Settings.useStatic = (usestatic == "yes");
    }

    if (usepassword.length() != 0)
    {
      Settings.usePassword = (usepassword == "yes");
    }

    if (usepasswordcontrol.length() != 0)
    {
      Settings.usePasswordControl = (usepasswordcontrol == "yes");
    }

    if (password.length() != 0)
    {
      strncpy(SecuritySettings.Password, password.c_str(), sizeof(SecuritySettings.Password));
    }

    if (disableJ3reset.length() != 0)
    {
      Settings.disableJ3Reset = (disableJ3reset == "true");
    }
    
    SaveSettings();
    
    String reply = "";
    char str[20];
    addHeader(true, reply);

    reply += F("<script src='http://ajax.googleapis.com/ajax/libs/jquery/1/jquery.min.js'></script>");
    
    reply += F("<form name='frmselect' class='form' method='post'><table>");
    reply += F("<TH colspan='2'>");
    reply += projectName;
    reply += F(" Settings");
    reply += F("<TR><TD><TD><TR><TD colspan='2'>");
    addMenu(reply);

    reply += F("<TR><TD><TD><TR><TD>Password Protect<BR><BR>Configuration:<TD><BR><BR>");

    reply += F("<input type='radio' name='usepassword' value='yes'");
    if (Settings.usePassword)
      reply += F(" checked ");
    reply += F(">Yes");
    reply += F("</input>");

    reply += F("<input type='radio' name='usepassword' value='no'");
    if (!Settings.usePassword)
      reply += F(" checked ");
    reply += F(">No");
    reply += F("</input>");

    reply += F("<TR><TD>Control:<TD>");

    reply += F("<input type='radio' name='usepasswordcontrol' value='yes'");
    if (Settings.usePasswordControl)
      reply += F(" checked ");
    reply += F(">Yes");
    reply += F("</input>");

    reply += F("<input type='radio' name='usepasswordcontrol' value='no'");
    if (!Settings.usePasswordControl)
      reply += F(" checked ");
    reply += F(">No");
    reply += F("</input>");

    reply += F("<TR><TD>\"admin\" Password:<TD><input type='password' id='user_password' name='password' value='");
    SecuritySettings.Password[25] = 0;
    reply += SecuritySettings.Password;

    reply += F("'><input type='checkbox' id='showPassword' name='show' value='Show'> Show");

    reply += F("<script type='text/javascript'>");

    reply += F("$(\"#showPassword\").click(function() {");
    reply += F("var showPasswordCheckBox = document.getElementById(\"showPassword\");");
    reply += F("$('.form').find(\"#user_password\").each(function() {");
    reply += F("if(showPasswordCheckBox.checked){");
    reply += F("$(\"<input type='text' />\").attr({ name: this.name, value: this.value, id: this.id}).insertBefore(this);");
    reply += F("}else{");
    reply += F("$(\"<input type='password' />\").attr({ name: this.name, value: this.value, id: this.id }).insertBefore(this);");
    reply += F("}");
    reply += F("}).remove();");
    reply += F("});");

    reply += F("$(document).ready(function() {");
    reply += F("$(\"#user_password_checkbox\").click(function() {");
    reply += F("if ($('input.checkbox_check').attr(':checked')); {");
    reply += F("$(\"#user_password\").attr('type', 'text');");
    reply += F("}});");
    reply += F("});");

    reply += F("</script>");
    
    reply += F("<TR><TD>Static IP:<TD>");

    reply += F("<input type='radio' name='usestatic' value='yes'");
    if (Settings.useStatic)
      reply += F(" checked ");
    reply += F(">Yes");
    reply += F("</input>");

    reply += F("<input type='radio' name='usestatic' value='no'");
    if (!Settings.useStatic)
      reply += F(" checked ");
    reply += F(">No");
    reply += F("</input>");
        
    
    reply += F("<TR><TD>IP:<TD><input type='text' name='ip' value='");
    sprintf_P(str, PSTR("%u.%u.%u.%u"), Settings.IP[0], Settings.IP[1], Settings.IP[2], Settings.IP[3]);
    reply += str;

    reply += F("'><TR><TD>Subnet:<TD><input type='text' name='subnet' value='");
    sprintf_P(str, PSTR("%u.%u.%u.%u"), Settings.Subnet[0], Settings.Subnet[1], Settings.Subnet[2], Settings.Subnet[3]);
    reply += str;

    reply += F("'><TR><TD>Gateway:<TD><input type='text' name='gateway' value='");
    sprintf_P(str, PSTR("%u.%u.%u.%u"), Settings.Gateway[0], Settings.Gateway[1], Settings.Gateway[2], Settings.Gateway[3]);
    reply += str;

    //reply += F("'><TR><TD>DNS:<TD><input type='text' name='dns' value='");
    //sprintf_P(str, PSTR("%u.%u.%u.%u"), Settings.DNS[0], Settings.DNS[1], Settings.DNS[2], Settings.DNS[3]);
    //reply += str;
    
    reply += F("'><TR><TD>HA Controller IP:<TD><input type='text' name='haip' value='");
    sprintf_P(str, PSTR("%u.%u.%u.%u"), Settings.haIP[0], Settings.haIP[1], Settings.haIP[2], Settings.haIP[3]);
    reply += str;
  
    reply += F("'><TR><TD>HA Controller Port:<TD><input type='text' name='haport' value='");
    reply += Settings.haPort;
    reply += F("'>");
  
    byte choice = Settings.powerOnState;
    reply += F("<TR><TD>Boot Up State:<TD><select name='");
    reply += "pos";
    reply += "'>";
    if (choice == 0){
      reply += F("<option value='0' selected>Off</option>");
    } else {
      reply += F("<option value='0'>Off</option>");
    }
    if (choice == 1){
      reply += F("<option value='1' selected>On</option>");
    } else {
      reply += F("<option value='1'>On</option>");
    }
    //if (choice == 2){
    //  reply += F("<option value='2' selected>Previous State</option>");
    //} else {
    //  reply += F("<option value='2'>Previous State</option>");
    //}
    reply += F("</select>");

    reply += F("<TR><TD>Disable J3 Reset:<TD>");

    reply += F("<input type='radio' name='disableJ3reset' value='true'");
    if (Settings.disableJ3Reset)
      reply += F(" checked ");
    reply += F(">Yes");
    reply += F("</input>");

    reply += F("<input type='radio' name='disableJ3reset' value='false'");
    if (!Settings.disableJ3Reset)
      reply += F(" checked ");
    reply += F(">No");
    reply += F("</input>");
    
    reply += F("<TR><TD><TD><input class=\"button-link\" type='submit' value='Submit'>");
    reply += F("</table></form>");
    addFooter(reply);
    server->send(200, "text/html", reply);
  });

  server->on("/control", []() {

    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }

    String hexR = server->arg("r");
    String hexG = server->arg("g");
    String hexB = server->arg("b");
    String hexW1 = server->arg("w1");
    String hexW2 = server->arg("w2");
    String color = server->arg("color");
    String power = server->arg("power");

    if (color.length() != 0){
      changeColor(color.substring(1, 7), 6, false, true);
    }
    else if (power == "off"){
      changeColor("00", 1, false);
      changeColor("00", 2, false);
      changeColor("00", 3, false);
      changeColor("00", 4, false);
      changeColor("00", 5, false);
    } else {
      if (hexR.length() != 0) {
        changeColor(padHex(String(hexR.toInt(), HEX)), 1, false, true);
      }
      if (hexG.length() != 0) {
        changeColor(padHex(String(hexG.toInt(), HEX)), 2, false, true);
      }
      if (hexB.length() != 0) {
        changeColor(padHex(String(hexB.toInt(), HEX)), 3, false, true);
      }
      if (hexW1.length() != 0) {
        changeColor(padHex(String(hexW1.toInt(), HEX)), 4, false, true);
      } 
      if (hexW2.length() != 0) {
        changeColor(padHex(String(hexW2.toInt(), HEX)), 5, false, true);
      }
    }

    String reply = "";
    char str[20];
    addHeader(true, reply);

    reply += F("<table>");
    reply += F("<TH colspan='2'>");
    reply += projectName;
    reply += F(" Control");
    reply += F("<TR><TD><TD><TR><TD colspan='2'>");
    addMenu(reply);

    reply += F("</TR><form name='colorselect' class='form' method='post'>");
    reply += F("<TR><TD><TD>");
    reply += F("<TR><TD>");
    reply += F("<input type='color' name='color' value='#");
    reply += getHex(RED) + getHex(GREEN) + getHex(BLUE); 
    reply += F("'><TD>");
    reply += F("<input class=\"button-link\" type='submit' value='Set Color'></TR>");
    reply += F("</form>");
    reply += F("<TR><TD><TD></TR>");
    reply += F("<form name='frmselect' class='form' method='post'>");
  
    reply += F("<TR><TD><font color='red'>R</font><TD><input type='range' name='r' min='0' max='255' value='");
    reply += getStandard(RED); 
    reply += F("'>");
    reply += F("<TR><TD><font color='green'>G</font><TD><input type='range' name='g' min='0' max='255' value='");
    reply += getStandard(GREEN);
    reply += F("'>");
    reply += F("<TR><TD><font color='blue'>B</font><TD><input type='range' name='b' min='0' max='255' value='");
    reply += getStandard(BLUE);   
    reply += F("'>");
    reply += F("<TR><TD>W1<TD><input type='range' name='w1' min='0' max='255' value='");
    reply += getStandard(W1);
    reply += F("'>");
    reply += F("<TR><TD>W2<TD><input type='range' name='w2' min='0' max='255' value='");
    reply += getStandard(W2);
    reply += F("'>");
    reply += F("<TR><TD><TD></TR>");
    reply += F("<TR><TD><input class=\"button-link\" type='submit' value='Set Color'></TD></form>");
    reply += F("<form name='powerOff' method='post'>");
    reply += F("<TD><input type='hidden' name='power' value='off'>");
    reply += F("<input class=\"button-link\" type='submit' value='Power Off'>");
    reply += F("</table></form>");
    addFooter(reply);

    needUpdate = true;
    
    server->send(200, "text/html", reply);
  });

  server->on("/rgb", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    run_program = false;
    String hexRGB = server->arg("value");
    String channels = server->arg("channels");
    String transition = server->arg("transition");
    
    String r, g, b;

    r = hexRGB.substring(0, 2);
    g = hexRGB.substring(2, 4);
    b = hexRGB.substring(4, 6);
  
    changeColor(hexRGB, 6, (transition != "false"), (channels != "true"));
    
    server->send(200, "application/json", getStatus());
    
  });


  server->on("/w1", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    run_program = false;
    String hexW1 = server->arg("value");
    String channels = server->arg("channels");
    String transition = server->arg("transition");
  
    changeColor(hexW1, 4, (transition != "false"), (channels != "true"));

    server->send(200, "application/json", getStatus());

  });

  server->on("/w2", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    run_program = false;
    String hexW2 = server->arg("value");
    String channels = server->arg("channels");
    String transition = server->arg("transition");
  
    changeColor(hexW2, 5, (transition != "false"), (channels != "true"));
    
    server->send(200, "application/json", getStatus());

  });

  server->on("/r", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    run_program = false;
    String r = server->arg("value");
    String channels = server->arg("channels");
    String transition = server->arg("transition");
  
    changeColor(r, 1, (transition != "false"), (channels != "true"));
    
    server->send(200, "application/json", getStatus());

  });

  server->on("/g", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    run_program = false;
    String g = server->arg("value");
    String channels = server->arg("channels");
    String transition = server->arg("transition");
  
    changeColor(g, 2, (transition != "false"), (channels != "true"));
    
    server->send(200, "application/json", getStatus());

  });

  server->on("/b", []() {
    if (SecuritySettings.Password[0] != 0 && Settings.usePasswordControl == true)
    {
      if(!server->authenticate("admin", SecuritySettings.Password))
        return server->requestAuthentication();
    }
    run_program = false;
    String b = server->arg("value");
    String channels = server->arg("channels");
    String transition = server->arg("transition");
  
    changeColor(b, 3, (transition != "false"), (channels != "true"));
    
    server->send(200, "application/json", getStatus());

  });

  if (ESP.getFlashChipRealSize() > 524288){
    if (Settings.usePassword == true && SecuritySettings.Password[0] != 0){
      httpUpdater.setup(&*server, "/update", "admin", SecuritySettings.Password);
      httpUpdater.setProjectName(projectName);
    } else {
      httpUpdater.setup(&*server);
      httpUpdater.setProjectName(projectName);
    }
  }

  server->onNotFound(handleNotFound);

  server->begin();

  Serial.printf("Starting SSDP...\n");
  SSDP.setSchemaURL("description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName(projectName);
  SSDP.setSerialNumber(ESP.getChipId());
  SSDP.setURL("index.html");
  SSDP.setModelName(projectName);
  SSDP.setModelNumber("H801_SL");
  SSDP.setModelURL("http://smartlife.tech");
  SSDP.setManufacturer("Smart Life Automated");
  SSDP.setManufacturerURL("http://smartlife.tech");
  SSDP.begin();

  Serial.println("HTTP server started");
  Serial.println(WiFi.localIP());
  
}

void loop()
{
  server->handleClient();

  if(needUpdate == true && run_program == false){
    sendStatus();
    needUpdate = false;
  }

  if(run_program){
    if (millis() - previousMillis >= program_wait) {
      char *dup = strdup(program.c_str());
      const char *value = strtok( dup, "_" );
      const char *program_details = "";
      program_counter = 1;
      
      while(value != NULL)
      {
        if(program_counter == program_step){
          program_details = value;
        }
        program_counter = program_counter + 1;
        value = strtok(NULL, "_");
      }
      String hexString(program_details);
      
      if(hexString.startsWith("w~")) {
        String hexProgram = hexString.substring(2, 4);
        if (hexString.indexOf("-",5) >= 0) {
          program_wait = rand_interval(hexString.substring(5, hexString.indexOf("-") - 1).toInt(), hexString.substring(hexString.indexOf("-") + 1, hexString.length()).toInt());
        } else {
          program_wait = hexString.substring(5, hexString.length()).toInt();
        }
        changeColor(hexProgram, 4, true); 
      }else if(hexString.startsWith("x~")){
        String hexProgram = hexString.substring(2, 4);
        if (hexString.indexOf("-",5) >= 0) {
          program_wait = rand_interval(hexString.substring(5, hexString.indexOf("-") - 1).toInt(), hexString.substring(hexString.indexOf("-") + 1, hexString.length()).toInt());
        } else {
          program_wait = hexString.substring(5, hexString.length()).toInt();
        }
        changeColor(hexProgram, 4, false);
      }else if(hexString.startsWith("f~")){
        String hexProgram = hexString.substring(2, 8);
        if (hexString.indexOf("-",9) >= 0) {
          program_wait = rand_interval(hexString.substring(9, hexString.indexOf("-") - 1).toInt(), hexString.substring(hexString.indexOf("-") + 1, hexString.length()).toInt());
        } else {
          program_wait = hexString.substring(9, hexString.length()).toInt();
        }
        changeColor(hexProgram, 6, true);
      }else{
        String hexProgram = hexString.substring(2, 8);
        if (hexString.indexOf("-",9) >= 0) {
          program_wait = rand_interval(hexString.substring(9, hexString.indexOf("-") - 1).toInt(), hexString.substring(hexString.indexOf("-") + 1, hexString.length()).toInt());
        } else {
          program_wait = hexString.substring(9, hexString.length()).toInt();
        }
        changeColor(hexProgram, 6, false);
      }
      
      previousMillis = millis();
      program_step = program_step + 1;

      if (program_step >= program_counter && repeat == -1){
        program_step = 1;
      }else if(program_step >= program_counter && repeat_count < repeat){
        program_step = 1;
        repeat_count = repeat_count + 1;
      }else if(program_step > program_counter){
        program_step = 1;
        run_program = false;
        if(program_off == "true"){
          changeColor("000000", 6, false);
          changeColor("00", 4, false);
        }
        sendStatus();
      }
  
      free(dup);
    }
  }

  if(millis() - TIME_LED_RED >= led_delay_red){
    TIME_LED_RED = millis();
    LED_RED();
  }
  
  if(millis() - TIME_LED_GREEN >= led_delay_green){
    TIME_LED_GREEN = millis();
    LED_GREEN();
  }
  
  if(millis() - TIME_LED_BLUE >= led_delay_blue){
    TIME_LED_BLUE = millis();
    LED_BLUE();
  }
  
  if(millis() - TIME_LED_W1 >= led_delay_w1){
    TIME_LED_W1 = millis();
    LED_W1();
  }
  
  if(millis() - TIME_LED_W2 >= led_delay_w2){
    TIME_LED_W2 = millis();
    LED_W2();
  }

  if (millis() > timerwd)
      runEach5Minutes();
}
