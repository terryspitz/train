// based on   http://www.arduino.cc/en/Tutorial/LiquidCrystal

#include <LiquidCrystal.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <pins_arduino.h>

const char* ssid     = "BTHub4-xxxx";                     // wifi ssid
const char* password = "<password here>";                 // wifi password

WiFiClientSecure client;


//see https://api.tfl.gov.uk/swagger/ui/index.html?url=/swagger/docs/v1#!/StopPoint/StopPoint_Search to find your station / stop
const char* tflServer = "api.tfl.gov.uk";
const char* arrivalsUrl = "/StopPoint/"
							"940GZZLUPNR" //pinner arrivals
							"/Arrivals?"
							"app_id=<id>&app_key=<key>";  //credentials from https://api-portal.tfl.gov.uk/admin 
const char* platform = "Southbound";

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(D4, D5, D0, D1, D2, D3);

void setup() 
{
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connect to Wifi");
  lcd.setCursor(0, 1);  // set the cursor to column 0, lineNo 1 (note: lineNo 1 is the second row, since counting begins with 0):
  lcd.print(ssid);

  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  
  //connect to wifi
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);


  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    lcd.setCursor(0, 1);
    lcd.print("No Wifi shield");
    // don't continue:
    while (true);
  }

  // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  printWifiStatus();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connected       ");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(1000);
}


void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

String last_row;
bool first = true;
  
void loop()
{
  Serial.println("Start loop()");
  
  //from https://www.arduino.cc/en/Tutorial/WiFiWebClientRepeating
  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:
  //while (client.available()) {
  //  char c = client.read();
  //  Serial.write(c);
  //}

  Serial.println("client.connect");
  if(first) {
    lcd.setCursor(0, 0);
    lcd.print("Connect to TFL  ");
  }
  if (!client.connect(tflServer, 443)) {
    Serial.println("connection failed");
    lcd.setCursor(0, 0);
    lcd.print("Connect to TFL  ");
    lcd.setCursor(0, 1);
    lcd.print("Failed          ");
    delay(5000);
    return;
  }
  if(first) {
    lcd.setCursor(0, 1);
    lcd.print("Connected      ");
  }

  client.print(F("GET "));
  client.print(arrivalsUrl);
  client.println(F(" HTTP/1.1"));
  client.print(F("Host: "));
  client.println(tflServer);
  client.println(F("Connection: close"));
  if (client.println() == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Send to TFL     ");
    lcd.setCursor(0, 1);
    lcd.print("Failed          ");
    Serial.println(F("Failed to send request"));
    delay(2000);
    return;
  }
  Serial.println("Sent GET request");
  if(first) {
    lcd.setCursor(0, 1);
    lcd.print("Sent GET request");
  }

  // Check HTTP status
  char status[100] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    lcd.print(status);
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    delay(5000);
    return;
  }
  else {
    Serial.println("Response: OK");
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    lcd.setCursor(0, 1);
    lcd.print("Invalid header  ");
    Serial.println(F("Invalid response"));
    delay(5000);
    return;
  }

  //  Serial.println(F("--------------"));
  //  while (client.available()) {
  //    char c = client.read();
  //    Serial.write(c);
  //  }
  //  Serial.println(F("--------------"));

  //tfl seems to return a first lineNo before 
  Serial.print(F("First line: "));
  Serial.println(client.readStringUntil('\n'));
  Serial.print(F("Next char: "));
  Serial.println(client.peek());

  // Allocate JsonBuffer and Parse JSON object
  // Use arduinojson.org/assistant to compute the capacity.
  const size_t capacity = 2000;
  DynamicJsonBuffer jsonBuffer(capacity);
  JsonArray& array = jsonBuffer.parseArray(client);
  if (!array.success()) {
    lcd.setCursor(0, 1);
    lcd.print("Parse fail      ");
    Serial.println(F("Parsing failed!"));
    delay(5000);
    return;
  }
  Serial.print(F("Parsed JSON, trains: "));
  Serial.println(array.size());
  for(int i=0; i<array.size(); ++i)
  {
    JsonObject& root = array[i];
    Serial.print(root["platformName"].as<String>());
    Serial.print(" - ");
    Serial.print(root["towards"].as<String>());
    Serial.print(" in ");
    Serial.println(root["timeToStation"].as<int>()/60);
  }
  
  // Extract and display train details.
  // Since the results are not sorted by time, and rather than sorting them first before displaying them
  // we loop repeatedly finding the next largest time to arrival
  int timeShown = 0;
  int lineNo = 1;
  const int LARGEST_TIME = 99999;
  while(true) //breaks out below
  {
    String dest;
    int mins;
    int minTimeToArrival = LARGEST_TIME;
    for(int i=0; i<array.size(); ++i)
    {
      JsonObject& root = array[i];
      if (root["platformName"].as<String>().indexOf(platform) != -1) 
      {
        int timeToStation = root["timeToStation"].as<int>();

        if(timeToStation>timeShown && timeToStation < minTimeToArrival) {
          minTimeToArrival = timeToStation;
          dest = root["towards"].as<String>();
        }
      }
    }
    if(timeShown>0 && minTimeToArrival<LARGEST_TIME)
      delay(2000);
    
    timeShown = minTimeToArrival;
    if(minTimeToArrival == LARGEST_TIME)
      break; //from while loop
    
    //format display with <lineNo no>.<first 11 chars of destination><3 right-aligned chars for minutes to arrive>
    if(dest.length()>11)
      dest = dest.substring(0,11);
    else while(dest.length()<11)
      dest += ' ';

    char buf[3];
    sprintf(buf, "%3d", timeShown/60); //seconds to minutes

    String next_row = String(lineNo) + "." + dest + buf;
    lineNo++;
    
    lcd.setCursor(0, 0);
    lcd.print(last_row);
    last_row = next_row;
    lcd.setCursor(0, 1);
    lcd.print(last_row);
  }

  // Disconnect
  client.stop();
  first = false;
}

