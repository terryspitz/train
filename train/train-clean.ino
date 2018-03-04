/* based on   http://www.arduino.cc/en/Tutorial/LiquidCrystal
  LiquidCrystal Library - Hello World
  Demonstrates the use a 16x2 LCD display.
  The circuit:
   LCD RS pin to digital pin 7
   LCD Enable pin to digital pin 8
   LCD D4 pin to digital pin 9
   LCD D5 pin to digital pin 10
   LCD D6 pin to digital pin 11
   LCD D7 pin to digital pin 12
   LCD R/W pin to ground
   LCD VSS pin to ground
   LCD VCC pin to 5V
   10K resistor:
   ends to +5V and ground
   wiper to LCD VO pin (pin 3)

*/

#include <LiquidCrystal.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

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
LiquidCrystal lcd(2, 14, 16, 5, 4, 0);

void setup() 
{
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  lcd.print(ssid);
  //lcd.setCursor(0, 1);  // set the cursor to column 0, line 1 (note: line 1 is the second row, since counting begins with 0):

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
  lcd.setCursor(0, 0);
  lcd.print(WiFi.localIP());
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

String row1, row2;
  
void loop()
{
  Serial.println("Start loop()");
  
  //from https://www.arduino.cc/en/Tutorial/WiFiWebClientRepeating
  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:
  while (client.available()) {
    char c = client.read();
    Serial.write(c);
  }

  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  client.stop();

  lcd.setCursor(0, 1);

  Serial.println("client.connect");
  if (!client.connect(tflServer, 443)) {
    Serial.println("connection failed");
    delay(5000);
    return;
  }

  client.print(F("GET "));
  client.print(arrivalsUrl);
  client.println(F(" HTTP/1.1"));
  client.print(F("Host: "));
  client.println(tflServer);
  client.println(F("Connection: close"));
  if (client.println() == 0) {
    lcd.print("Fail");
    Serial.println(F("Failed to send request"));
    delay(2000);
    return;
  }
  else {
    Serial.println("Sent GET request");
  }

  // Check HTTP status
  char status[1500] = {0};
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
    lcd.print("Invalid");
    Serial.println(F("Invalid response"));
    delay(5000);
    return;
  }

  //tfl seems to return a first line before 
  Serial.println(F("First Line"));
  Serial.println(client.readStringUntil('\n'));

  // Allocate JsonBuffer
  // Use arduinojson.org/assistant to compute the capacity.
  const size_t capacity = 10000;
  DynamicJsonBuffer jsonBuffer(capacity);

  // Parse JSON object
  JsonArray& array = jsonBuffer.parseArray(client);
  if (!array.success()) {
    lcd.print("Parse fail");
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
  const int LARGEST_TIME = 99999;
  while(true) //breaks out below
  {
    String dest;
    int mins;
    int minTimeToArrival = LARGEST_TIME;
    for(int i=0; i<array.size(); ++i)
    {
      JsonObject& root = array[i];
      //if (root["platformName"].as<String>().indexOf(platform) != -1) 
      {
        int timeToStation = root["timeToStation"].as<int>()/60;

        if(timeToStation>timeShown && timeToStation < minTimeToArrival) {
          minTimeToArrival = timeToStation;
          dest = root["towards"].as<String>();
          mins = root["timeToStation"].as<int>()/60;
        }
      }
    }
    timeShown = minTimeToArrival;
    if(minTimeToArrival == LARGEST_TIME)
      break; //from while loop
    
    //format display with first 13 chars of destination, then 3 right-aligned chars for minutes to arrive
    if(dest.length()>13)
      dest = dest.substring(0,13);
    else while(dest.length()<13)
      dest += ' ';

    char buf[3];
    sprintf(buf, "%3d", mins);
    dest += buf;

    row1 = row2;
    row2 = dest;
    
    lcd.setCursor(0, 0);
    lcd.print(row1);
    Serial.print("row1:'");
    Serial.print(row1);
    Serial.println("'");
    lcd.setCursor(0, 1);
    lcd.print(row2);
    Serial.print("row2:'");
    Serial.print(row2);
    Serial.println("'");
    Serial.println(" ");

    delay(1000);
  }

  // Disconnect
  client.stop();
}

