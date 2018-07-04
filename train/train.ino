// based on   http://www.arduino.cc/en/Tutorial/LiquidCrystal

#include <LiquidCrystal.h>
#include <ArduinoJson.h>
#include <pins_arduino.h>

#include <ESP8266WiFi.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager allow phone to configure device wifi connection
//#include <ESP8266HTTPClient.h>  //doesn't support https easily
#include <WiFiClientSecure.h>
#include <BlynkSimpleEsp8266.h>
#include <Blynk/BlynkParam.h>
#include <EEPROM.h>

// Auth Token for Blynk App.
// see http://docs.blynk.cc/#getting-started-getting-started-with-the-blynk-app-4-auth-token
char blynkAuth[] = "<your token here>";

//see https://api.tfl.gov.uk/swagger/ui/index.html?url=/swagger/docs/v1#!/StopPoint/StopPoint_Search to find your station / stop
const char* tflServer = "api.tfl.gov.uk";
const char* tflAuth =	"app_id=3552f227&app_key=adb68a2f910ff5b3af83641537fc90c7";  //credentials from https://api-portal.tfl.gov.uk/admin 

String stations[]={
  "940GZZLUBST", "Baker Street",
  "940GZZLUFYR", "Finchley Road",
  "940GZZLUKSX", "King's Cross",
  "940GZZLUPNR", "Pinner",
  "940GZZLURYL", "Rayners Lane",
};
const int address = 0;
int station = 0;
bool stationChanged = false;

//https://www.arduino.cc/en/Reference/LiquidCrystalConstructor
//LiquidCrystal(rs, enable, d4, d5, d6, d7) 
// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(D4, D3, D2, D1, RX, TX);  //WeMos D1 pins
//LiquidCrystal lcd(D7, D8, D6, D5, D4, D3);  //NodeMCU DevKit pins
WidgetLCD blynkLcd(V2); // Virtual LCD in app

void lcdPrint(String s1, String s2, bool doBlynkLcd = true)
{
  Serial.print(s1);
  Serial.print(", ");
  Serial.println(s2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(s1);
  lcd.setCursor(0, 1);
  lcd.print(s2);
  if(doBlynkLcd)
  {
    blynkLcd.clear();
    blynkLcd.print(0,0,s1);
    blynkLcd.print(0,1,s2);
  }
}

// Called when station changes in Blynk app
BLYNK_WRITE(V1)
{
  station = param.asInt()-1;
  EEPROM.write(address, (byte)station);
  EEPROM.commit(); //see http://esp8266.github.io/Arduino/versions/2.0.0/doc/libraries.html
  stationChanged = true;
  lcdPrint("Station changed", stations[station*2+1], false);
  delay(1000);
}

BLYNK_APP_DISCONNECTED() {
  lcdPrint("Blynk disconnect", "");
}

void configModeCallback (WiFiManager *myWiFiManager)
{
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  lcdPrint("Connect to Wifi", myWiFiManager->getConfigPortalSSID());
}

void setup() 
{
  //Initialize serial and wait for port to open:
  //Serial.begin(115200);  //Serial causes WeMos D1 Mini to reset :(
  //while (!Serial) {}

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setTimeout(180);  //in seconds
  //wifiManager.resetSettings(); //for testing
  
  //fetches configured ssid and password and tries to connect
  if(!wifiManager.autoConnect("BedsideDepartureBoard")) {
    lcdPrint("Failed WiFi connect", "Restarting...");
    delay(3000);
    //reset and try again
    ESP.reset();
    delay(5000);
  }   
  
  lcdPrint("WiFi Connected:", WiFi.SSID());
  delay(1000);

  lcdPrint("Connect to Blynk","...");
  Blynk.config(blynkAuth);
  if(!Blynk.connect())
  {
    lcdPrint("Failed Blynk connect", "Restarting...");
    delay(3000);
    //reset and try again
    ESP.reset();
    delay(5000);
  }   
  lcdPrint("Connect to Blynk","OK");
  delay(1000);

  const size_t MAX_CHARS_ALL_STATIONS = 1000;
  BlynkParamAllocated items(MAX_CHARS_ALL_STATIONS);
  const int stationCount = sizeof(stations)/sizeof(String)/2;
  lcdPrint("Stations", String(stationCount));
  delay(1000);
  for(int i=0; i<stationCount; ++i)
    items.add(stations[2*i+1]);
  Blynk.setProperty(V1, "labels", items);
  EEPROM.begin(4);
  int readStation = EEPROM.read(address);
  if(readStation<stationCount)
    station = readStation;
}


bool first = true;
  
void loop()
{
  Serial.println("Start loop()");
  unsigned long queryMillis = millis();
  unsigned long displayMillis = millis();

  Blynk.run();

  WiFiClientSecure client;
  if(first) {
    lcdPrint("Connect to TFL","...");
    delay(1000);
  }
  if (!client.connect(tflServer, 443)) {
    lcdPrint("Connect to TFL","Failed");
    delay(5000);
    return;
  }
  else if(first) {
    lcdPrint("Connect to TFL","OK");
    delay(1000);
  }

  client.print(F("GET "));
  String arrivalsUrl = "/StopPoint/" + stations[station*2] + "/Arrivals?" + tflAuth;
  client.print(arrivalsUrl);
  client.println(F(" HTTP/1.0"));
  client.print(F("Host: "));
  client.println(tflServer);
  client.println(F("Connection: close"));
  if (client.println() == 0) {
    lcdPrint("Request from TFL","Failed");
    delay(5000);
    return;
  }
  else if(first) {
    lcdPrint("Request from TFL","OK");
    delay(1000);
  }

  // Check HTTP status
  char status[100] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    lcdPrint("Get from TFL","Failed:" + String(status));
    delay(5000);
    return;
  }
  else if(first) {
    lcdPrint("Get from TFL","OK");
    delay(1000);
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    lcdPrint("Get from TFL","Invalid header");
    delay(5000);
    return;
  }

  if(false) { //check data
    Serial.println(F("--------------"));
    while (client.available()) {
      char c = client.read();
      Serial.write(c);
    }
    Serial.println(F("--------------"));
  }
  
  // Extract all train details.
  const int MAX_TRAINS=50;
  int timeToStation[MAX_TRAINS];
  String destination[MAX_TRAINS];
  int trains=0;
  
  while(true)
  {
    //find next object (skip array start/comma)
    while(client.peek()!='{' && client.peek()!=-1)
      client.read();
    if(client.peek()==-1)
      break; //done reading json
    
    // Allocate JsonBuffer and Parse JSON object
    // Use arduinojson.org/assistant to compute the capacity, then add a bit
    const size_t capacity = 10000;
    DynamicJsonBuffer jsonBuffer(capacity);  //allocate on heap
    JsonObject& root = jsonBuffer.parseObject(client);
    if (!root.success()) {
      lcdPrint("Parse TFL data", "Failed");
      Serial.write(client.read());
      delay(5000);
      return;
    }
    
    //dump response to Serial
    Serial.print(F("Train: "));
    Serial.print(root["platformName"].as<String>());
    Serial.print(" - ");
    Serial.print(root["towards"].as<String>());
    Serial.print(" in ");
    Serial.println(root["timeToStation"].as<int>()/60);

    //if (root["platformName"].as<String>().indexOf(platform) != -1) 
    {
      timeToStation[trains] = root["timeToStation"].as<int>();
      destination[trains] = root["towards"].as<String>();
      ++trains;
      if(trains==MAX_TRAINS)
        break;
    }
  }

  if(first) {
    lcdPrint("Parse TFL data","OK");
    delay(1000);
  }
  
  // TFL results are not sorted by time.  To avoid sorting in memory
  // we loop repeatedly finding the next largest time to arrival.
  int timeShown = 0;
  int lineNo = 1;
  const int LARGEST_TIME = 99999;
  String last_row = stations[station*2+1] + "                ";

  while(true) //loop to display all trains, breaks out below
  {
    Blynk.run();
    if(stationChanged)
    {
      stationChanged = false;
      break;
    }

    String dest;
    int mins;
    int minTimeToArrival = LARGEST_TIME;
    //find the next largest timeToArrival bigger than the current timeShown
    for(int i=0; i<trains; ++i)
    {
      if(timeToStation[i]>timeShown && timeToStation[i] < minTimeToArrival) {
        minTimeToArrival = timeToStation[i];
        dest = destination[i];
      }
    }
    timeShown = minTimeToArrival;
    if(minTimeToArrival == LARGEST_TIME) {
      //finished displaying, if 30s since last query do it again 
      if(millis() > queryMillis+30000)
        break; //from while loop
      //else start display loop again
      int timeShown = 0;
      int lineNo = 1;
      continue;
    }
    
    //format display with <lineNo no>.<destination[0:11]> <minutes to arrive>
    String prefix = String(lineNo) + ".";
    String suffix = " " + String(int(timeShown/60));  //seconds to minutes
    int destLength = 16 - prefix.length() - suffix.length();
    if(dest.length()>destLength)
      dest = dest.substring(0,destLength);
    else if(dest.length()<destLength)
      dest += String("                ").substring(0,destLength-dest.length());

    String next_row = prefix + dest + suffix;
    lineNo++;

    //pause after all work done
    while(millis() < displayMillis+2000)
      delay(100);
    displayMillis = millis();

    //scroll up and display
    lcd.setCursor(0, 0);
    lcd.print(last_row);
    blynkLcd.print(0,0,last_row);
    last_row = next_row;
    lcd.setCursor(0, 1);
    lcd.print(last_row);
    blynkLcd.print(0,1,last_row);
  }

  // Disconnect
  client.stop();
  first = false;
}

