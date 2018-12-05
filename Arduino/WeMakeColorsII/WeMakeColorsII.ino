
String softwareName = "WeMakeColorsII";
String softwareVersion = "1.3.0"; // refactor char to Strings (except wifimanager 
String software = "";
   
//Wi-Fi
#include <ESP8266WiFi.h>  // ESP8266 core 2.4.2
WiFiClient  wifi;

//Wi-FiManager
#include "FS.h"
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> // 0.14 

#define MAX_PARAM 40
#define MQTT_MAX MQTT_MAX_PACKET_SIZE

String thingId = "";
String appId = "WMCII";

#define THING_NAME_DEFAULT ""
char THING_NAME[MAX_PARAM] = THING_NAME_DEFAULT;
String s_thingName = "";

char MQTT_SERVER[MAX_PARAM] = "wmc.marcobrianza.it";
String s_mqttServer = "";

char MQTT_USERNAME[MAX_PARAM] = "";
String s_mqttUsername = "";

char MQTT_PASSWORD[MAX_PARAM] = "";
String s_mqttPassword = "";

// name, prompt, default, length
WiFiManagerParameter wfm_thingName("thingName", "Thing Name", THING_NAME, sizeof(THING_NAME));
WiFiManagerParameter wfm_mqttServer("mqttServer", "MQTT Server", MQTT_SERVER, sizeof(MQTT_SERVER));
WiFiManagerParameter wfm_mqttUsername("mqttUsername", "MQTT Username", MQTT_USERNAME, sizeof(MQTT_USERNAME));
WiFiManagerParameter wfm_mqttPassword("mqttPassword", "MQTT Password", MQTT_PASSWORD, sizeof(MQTT_PASSWORD));

//OTA
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
String OTA_PASSWORD = "12345678";

//http update
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

String MD5_URL = "http://iot.marcobrianza.it/art/WeMakeColorsII.md5.txt";
String FW_URL = "http://iot.marcobrianza.it/art/WeMakeColorsII.ino.d1_mini.bin";

//MQTT
WiFiClient wifiClient;
#include <PubSubClient.h> // version 2.7.0 in PubSubClient.h change #define MQTT_MAX_PACKET_SIZE 512 
#include <ArduinoJson.h> // version 5.13.3 
PubSubClient mqttClient(wifiClient);

int MQTT_PORT = 1883;

//LED
#include <FastLED.h> // version  3.2.1 

//presence
unsigned long last_color_t = 0;
int inputPin = A0;

int NEW_COLOR_TIME = 1000;
int LOOP_DELAY = 40;

// test device
#define BOOT_TEST_LIGHT 2
#define BOOT_RESET 3
#define BOOT_DEFAULT_AP 4

#define TEST_TIME 30000 // 30 seconds

//default ssid password
String SSID = "colors";
String PASSWORD = "colors01";

//beat
#define BEAT_INTERVAL 900 // 900 seconds is 15 minutes

//led builtin
#define LED_ON LOW
#define LED_OFF HIGH

#include <Ticker.h>
Ticker T_mqtt_beat;
Ticker T_globalBrighness;

void setup() {

  setupLEDs();
  showAllLeds(64, 64, 64);

  Serial.begin(115200);  Serial.println();
  software = softwareName + " - " + softwareVersion + " - " + ESP.getCoreVersion() + " - " + ESP.getSketchMD5();// + " - " + String (__DATE__) + " - " + String(__TIME__);;
  Serial.println(software);
  thingId=getTHING_ID(appId);
  Serial.println("thingId: "+thingId);
  WiFi.hostname(s_thingName);

  byte c = bootCount();
  Serial.print("\nboot count=");
  Serial.println(c);

  if (c == BOOT_TEST_LIGHT) {
    Serial.println("Test mode");
    testDevice();
  }

  switch  (c) {
    case BOOT_DEFAULT_AP:
      writeAttribute("thingName", "");
      writeAttribute("mqttServer", "");
      writeAttribute("mqttUsername", "");
      writeAttribute("mqttPassword", "");
      connectWifi();
      break;
    case BOOT_RESET:
      writeAttribute("thingName", "");
      writeAttribute("mqttServer", "");
      writeAttribute("mqttUsername", "");
      writeAttribute("mqttPassword", "");
      connectWifi_or_AP(true);
      break;
    default:
      connectWifi_or_AP(false);
  }


  autoUpdate();
  setupParameters();
  setupMqtt();
  setupOTA();
  setupMdns();
  setupLightLevel();

  T_mqtt_beat.attach(BEAT_INTERVAL, publishBeat);
  T_globalBrighness.attach(1, setGlobalBrightness);

}

void loop() {

  unsigned long now = millis();

  if (!mqttClient.connected())  reconnectMQTT();
  mqttClient.loop();

  if (lightChange() && (now - last_color_t > NEW_COLOR_TIME)) {
    CHSV c = newRndColor();
    setMyLED(c);
    publishRandomColor(c);
    last_color_t = now;
  }

  ArduinoOTA.handle();
  delay(LOOP_DELAY);

}

void testDevice() {

  while (millis() < TEST_TIME) {
    int a = analogRead(inputPin);
    int v = a / 4;
    if (v > 255) v = 255;
    Serial.println(v);
    showAllLeds(v, v, v);
    delay(LOOP_DELAY);
  }
}

//----WiFi manager main function ---- needs to be in the main sketch sice Arduino IDE 1.8.7

void connectWifi_or_AP(bool force_config) {
  digitalWrite(LED_BUILTIN, LOW);

  //  WiFi.disconnect();
  //  delay(100);

  WiFiManager wifiManager;
  wifiManager.setDebugOutput(true);
  wifiManager.setAPStaticIPConfig(IPAddress(1, 1, 1, 1), IPAddress(1, 1, 1, 1), IPAddress(255, 255, 255, 0));
  wifiManager.setMinimumSignalQuality(50); //default is 8
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&wfm_thingName);
  wifiManager.addParameter(&wfm_mqttServer);
  wifiManager.addParameter(&wfm_mqttUsername);
  wifiManager.addParameter(&wfm_mqttPassword);


  if ( force_config == true) { //config must be done
    WiFi.disconnect();
    wifiManager.resetSettings(); //reset saved settings
    wifiManager.setConfigPortalTimeout(0);
    wifiManager.startConfigPortal(thingId.c_str());
  } else
  {
    wifiManager.setConfigPortalTimeout(300); //5 minutes
    wifiManager.autoConnect(thingId.c_str());
  }

  boolean led = false;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_BUILTIN, led);
    led = !led;
  }

  //if you get here you have connected to the WiFi
  Serial.print("connected to network: ");
  Serial.println(WiFi.SSID());

  digitalWrite(LED_BUILTIN, HIGH);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
}
