#include <FS.h> //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
//#include <string.h>

#define SWversion     "0.9"
#define authorFirst   "Michael"
#define authorLast    "Sved"
const String  Email = "Michael.Sved@gmail.com";
  
// The port to listen for incoming TCP connections 
#define LISTEN_PORT           80

// Set web server port number to LISTEN_PORT
WiFiServer server(LISTEN_PORT);

// WIFI 
WiFiManager wifiManager;

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
String output4State = "off";
String output5State = "off";


// MQTT Config
//const char* mqtt_broker = "192.168.0.15"; // MQTT Broker IP 
//int mqtt_port = 1883;
WiFiClient espClient;
PubSubClient  MQTT(espClient); // init MQTT Client

//variables for wifimanager
char deviceName[20] = "SmartSwitch_";
char mqtt_port[4];
char mqtt_broker[15];

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config = true");
  shouldSaveConfig = true;
}

//MQTT reconnect timeout
long lastReconnectAttempt = 0;

void setup()
{
  initPins();
  initSerial();
  initName();
  initFS();
  initWiFi();
  initWebSever();
  initMQTT();
}

void initPins()
{
  pinMode(2, INPUT); 
  
  pinMode(4, OUTPUT);
  digitalWrite(4, 0);
  pinMode(5, OUTPUT);
  digitalWrite(5, 0);
}

void initSerial()
{
  Serial.begin(115200);
}

void initName()
{  
  int ChipID_int = ESP.getChipId();
  char ChipID_char[8];
  itoa(ChipID_int, ChipID_char, 16);
  strcat( deviceName, ChipID_char ); 
}

void initFS()
{
  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");


          //read json values into variables
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_broker, json["mqtt_broker"]);
          strcpy(deviceName, json["deviceName"]);

          //debug: did it really do that?
          //Serial.println("1 - Values are:");
          //Serial.println(mqtt_port);
          //Serial.println(mqtt_broker);
          //Serial.println(deviceName);
          
          configFile.close();
                    
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
}

void initWiFi()
{
  Serial.print("Bootpin: ");
  Serial.println(digitalRead(2));
  if(digitalRead(2) == 0)
  {
    Serial.println("Starting AP mode...");
    wifiManager.startConfigPortal(deviceName, "ESPconfig");
    Serial.println("Connected");
  }
  
  //custom variables to save
  // id/name, placeholder/prompt, default, length
  WiFiManagerParameter custom_mqtt_port("Port", "mqtt port", mqtt_port, 4);
  WiFiManagerParameter custom_mqtt_broker("Server", "mqtt server", mqtt_broker, 15);
  WiFiManagerParameter custom_deviceName("Device Name", "device name", deviceName, 20);
  
  // Create an instance of the server
  //WiFiServer server(LISTEN_PORT);



  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_broker);
  wifiManager.addParameter(&custom_deviceName);
  
  wifiManager.setDebugOutput(true);
  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  //first parameter is name of access point, second is the password
  if (!wifiManager.autoConnect(deviceName, "ESPconfig"))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
 
  Serial.println("Connected");

  //read updated parameters
  strcpy(mqtt_broker, custom_mqtt_broker.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(deviceName, custom_deviceName.getValue());

  //debug: did it really do that?
  //Serial.println("2 - Values are:");
  //Serial.println(mqtt_port);
  //Serial.println(mqtt_broker);
  //Serial.println(deviceName);

  //Serial.println("about to save");
  //Serial.println(shouldSaveConfig);
  //save the custom parameters to FS
  if (shouldSaveConfig == true)
  {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    //save collected varibles
    json["mqtt_port"] = mqtt_port;
    json["mqtt_broker"] = mqtt_broker;
    json["deviceName"] = deviceName;
    
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }


  //WiFi.begin(SSID, PASSWORD); // Wifi Connect
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("local ip: ");
  Serial.println(WiFi.localIP());
  Serial.print("device name: ");
  Serial.println(deviceName);
  
}


//init Websever
void initWebSever()
{
  server.begin();
}

// MQTT Broker connection
void initMQTT()
{
  MQTT.setServer(mqtt_broker, atoi(mqtt_port));
  MQTT.setCallback(mqtt_callback);
}

// Receive messages
void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
  String message;
  for (int i = 0; i < length; i++)
  {
    char c = (char)payload[i];
    message += c;
  }

  Serial.print("Topic ");
  Serial.print(topic);
  Serial.print(" | ");
  Serial.println(message);


  
  if(strcmp(topic,"ESP_dev2/GPIO/4") == 0)           //subscribe topic. use strcmp function
  {
    if(message == "1")
    {
      digitalWrite(4, 1);
      MQTT.publish ("ESP_dev2/state/GPIO/4", "1");
    }
    
    if(message == "0")
    {
      digitalWrite(4, 0);
      MQTT.publish ("ESP_dev2/state/GPIO/4", "0");
    }
  }
  
  if(strcmp(topic,"ESP_dev2/GPIO/5") == 0)
    {
      if(message == "1")
      {
        digitalWrite(5, 1);
        MQTT.publish ("ESP_dev2/state/GPIO/5", "1");
      }
      if(message == "0")
      {
        digitalWrite(5, 0);
        MQTT.publish ("ESP_dev2/state/GPIO/5", "0");
      }
    }
  
  message = "";
  Serial.println();
  Serial.flush();
}


  void recconectWiFi()
  {
    while (WiFi.status() != WL_CONNECTED)
    {
    delay(100);
    Serial.print(".");
   }
  }

  void loop()
  {
    WebServer();
    
    if (!MQTT.connected())
    {
      long now = millis();
      if (now - lastReconnectAttempt > 5000)
      {
        lastReconnectAttempt = now;
        // Attempt to reconnect
        Serial.print("Connecting to MQTT Broker:");
        Serial.print(mqtt_broker);
        Serial.print(":");
        Serial.println(mqtt_port);
        if (reconnect())
        {
          lastReconnectAttempt = 0;
          Serial.println("Connected to MQTT Broker");
        }
      }
    }
    else
    {
      // Client connected
    }

    MQTT.loop();
}

boolean reconnect()
{
  if (MQTT.connect("ESP_dev2"))          //client ID
    {
      MQTT.subscribe("ESP_dev2/GPIO/4"); // Topic
      MQTT.subscribe("ESP_dev2/GPIO/5"); // Topic
    }
    return MQTT.connected();
}

void WebServer()
{
  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
            
            // turns the GPIOs on and off
            if (header.indexOf("GET /4/on") >= 0)
            {
              Serial.println("GPIO 4 on");
              output4State = "on";
              digitalWrite(4, HIGH);
            } else if (header.indexOf("GET /4/off") >= 0)
            {
              Serial.println("GPIO 4 off");
              output4State = "off";
              digitalWrite(4, LOW);
            } else if (header.indexOf("GET /5/on") >= 0)
            {
              Serial.println("GPIO 5 on");
              output5State = "on";
              digitalWrite(5, HIGH);
            } else if (header.indexOf("GET /5/off") >= 0)
            {
              Serial.println("GPIO 5 off");
              output5State = "off";
              digitalWrite(5, LOW);
            }



            if (header.indexOf("GET /Reboot") >= 0)
            {
              Serial.println("rebooting device");
              client.stop();
              ESP.restart();
              
            }
            
            if (header.indexOf("GET /WiFiReset") >= 0)
            {
              Serial.println("reseting WiFi Settings");              
              wifiManager.resetSettings();
              Serial.println("rebooting device");
              client.stop();
              ESP.restart();
              
            }
                        
            if (header.indexOf("GET /settingsReset") >= 0)
            {
              Serial.println("reseting All Settings");;
              SPIFFS.remove("/config.json");
              wifiManager.resetSettings();
              Serial.println("rebooting device");
              client.stop();
              ESP.restart();
            }

            

            
            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");
            
            // Web Page Heading
            client.print("<body><h1>");
            client.print(deviceName);
            client.print("</h1>");
            
            client.print("<p>local IP:");
            client.print(WiFi.localIP());
            client.print("</p>");
            client.print("<p></p>"); 

            client.print("<p>MQTT Server:");
            client.print(mqtt_broker);
            client.print(":");
            client.print(mqtt_port);
            client.print("</p>");
            client.print("<p></p>");
            
            // Display current state, and ON/OFF buttons for GPIO 4  
            client.println("<p>GPIO 4 - State " + output4State + "</p>");
            // If the output4State is off, it displays the ON button       
            if (output4State=="off") {
              client.println("<p><a href=\"/4/on\"><button class=\"button\">ON</button></a></p>");
            } else {
              client.println("<p><a href=\"/4/off\"><button class=\"button button2\">OFF</button></a></p>");
            }

            // Display current state, and ON/OFF buttons for GPIO 5  
            client.println("<p>GPIO 5 - State " + output5State + "</p>");
            // If the output5State is off, it displays the ON button       
            if (output5State=="off") {
              client.println("<p><a href=\"/5/on\"><button class=\"button\">ON</button></a></p>");
            } else {
              client.println("<p><a href=\"/5/off\"><button class=\"button button2\">OFF</button></a></p>");
            }
            client.print("<p>----------------------------------</p>");
            client.print("<p><b>Manage SmartSwitch</b></p>");    
            client.println("<p>Restart Device</p>");
            client.println("<p><a href=\"/Reboot\"><button class=\"button\">Restart</button></a></p>");
            client.println("<p>Reset WiFi Settings</p>");
            client.println("<p><a href=\"/WiFiReset\"><button class=\"button\">Reset</button></a></p>");
            client.println("<p>Reset All Settings</p>");
            client.println("<p><a href=\"/settingsReset\"><button class=\"button\">Reset All</button></a></p>");

            client.print("<p></p>");
            client.print("<p></p>");
            client.print("<p></p>");
            client.print("<p></p>");
            client.print("<p>Version ");
            client.print(SWversion);
            client.print(" 2019");
            client.print("/<p>");
            client.print("<p>");
            client.print(authorFirst);
            client.print(" ");
            client.print(authorLast);
            client.print("/<p>");
            client.print("<p>");
            client.print(Email);
            client.print("</p>");
            
    
            client.println("</body></html>");
            
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}
