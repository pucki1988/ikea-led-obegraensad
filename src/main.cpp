#include <Arduino.h>
#include <SPI.h>

#ifdef ESP82666
/* Fix duplicate defs of HTTP_GET, HTTP_POST, ... in ESPAsyncWebServer.h */
#define WEBSERVER_H
#endif
#include <WiFiManager.h>

#ifdef ESP32
#include <ESPmDNS.h>
#endif
#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif

#include "PluginManager.h"


#include "plugins/BreakoutPlugin.h"
#include "plugins/CirclePlugin.h"
#include "plugins/DrawPlugin.h"
#include "plugins/FireworkPlugin.h"
#include "plugins/GameOfLifePlugin.h"
#include "plugins/LinesPlugin.h"
#include "plugins/RainPlugin.h"
#include "plugins/SnakePlugin.h"
#include "plugins/StarsPlugin.h"
#include "plugins/PongClockPlugin.h"
#include "plugins/DDPPlugin.h"

#ifdef ENABLE_SERVER
#include "plugins/AnimationPlugin.h"
#include "plugins/BigClockPlugin.h"
#include "plugins/ClockPlugin.h"
#include "plugins/WeatherPlugin.h"
#include "plugins/AnimationPlugin.h"
#include "plugins/TickingClockPlugin.h"
#endif

#include "asyncwebserver.h"
#include "ota.h"
#include "screen.h"
#include "secrets.h"
#include "websocket.h"
#include "messages.h"

unsigned long previousMillis = 0;
unsigned long interval = 30000;

PluginManager pluginManager;
SYSTEM_STATUS currentStatus = NONE;
WiFiManager wifiManager;

unsigned long lastConnectionAttempt = 0;
const unsigned long connectionInterval = 10000;

const bool dimMode = SCREEN_DIM_MODE;


const char* fritzUser = "andi";
const char* fritzPassword = "schalke04";

void connectToWiFi()
{
  // if a WiFi setup AP was started, reboot is required to clear routes
  bool wifiWebServerStarted = false;
  wifiManager.setWebServerCallback(
      [&wifiWebServerStarted]()
      { wifiWebServerStarted = true; });

  wifiManager.setHostname(WIFI_HOSTNAME);

#if defined(IP_ADDRESS) && defined(GWY) && defined(SUBNET) && defined(DNS1)
  auto ip = IPAddress();
  ip.fromString(IP_ADDRESS);

  auto gwy = IPAddress();
  gwy.fromString(GWY);

  auto subnet = IPAddress();
  subnet.fromString(SUBNET);

  auto dns = IPAddress();
  dns.fromString(DNS1);

  wifiManager.setSTAStaticIPConfig(ip, gwy, subnet, dns);
#endif

  wifiManager.setConnectRetries(10);
  wifiManager.setConnectTimeout(10);
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.autoConnect(WIFI_MANAGER_SSID);

#ifdef ESP32
  if (MDNS.begin(WIFI_HOSTNAME))
  {
    MDNS.addService("http", "tcp", 80);
    MDNS.setInstanceName(WIFI_HOSTNAME);
  }
  else
  {
    Serial.println("Could not start mDNS!");
  }
#endif

  if (wifiWebServerStarted)
  {
    // Reboot required, otherwise wifiManager server interferes with our server
    Serial.println("Done running WiFi Manager webserver - rebooting");
    ESP.restart();
  }

  lastConnectionAttempt = millis();
}

String authenticate() {
  HTTPClient http;
  http.begin("http://fritz.box/login_sid.lua");
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    
    String response = http.getString();
    int challengeStart = response.indexOf("<Challenge>") + 11;
    int challengeEnd = response.indexOf("</Challenge>");
    String challenge = response.substring(challengeStart, challengeEnd);
    Serial.println("Challenge: " + challenge);

    // Berechnung des Antwort-Hashes
  String combined = challenge + "-" + fritzPassword;
  MD5Builder md5;
  md5.begin();
  md5.add(combined);
  md5.calculate();
  String responseHash = md5.toString();

  // Anfrage mit Challenge und Hash zur Authentifizierung
  String loginURL = "http://fritz.box/login_sid.lua";
  http.begin(loginURL);
  httpCode = http.POST("username=" + String(fritzUser) + "&response=" + challenge + "-" + responseHash);
  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Fehler bei der Authentifizierung");
    return "";
  }

  // Session-ID extrahieren
  response = http.getString();
  int sidStart = response.indexOf("<SID>") + 5;
  int sidEnd = response.indexOf("</SID>");
  String sessionID = response.substring(sidStart, sidEnd);
  Serial.println("Session ID: " + sessionID);

  return sessionID;
  }
  return "";
}

void scanNetworkDevices() {
  String sessionID = authenticate();
  if (sessionID != "" && sessionID != "0000000000000000") {
    HTTPClient http;
    http.begin("http://fritz.box/query.lua?sid=" + sessionID + "&network=devicelist");
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String deviceList = http.getString();
      if (deviceList.indexOf("24:95:2f:d6:a3:da") >= 0) {
        Serial.println("Zielgerät ist im Netzwerk verbunden!");
      } else {
        Serial.println("Zielgerät nicht gefunden.");
      }
    }
  }
    
}

void setup()
{
  Serial.begin(115200);

  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_ENABLE, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

// server
#ifdef ENABLE_SERVER
  connectToWiFi();

  // set time server
  configTzTime(TZ_INFO, NTP_SERVER);

  initOTA(server);
  initWebsocketServer(server);
  initWebServer();
#endif

  Screen.setup();

  pluginManager.addPlugin(new DrawPlugin());
  pluginManager.addPlugin(new BreakoutPlugin());
  pluginManager.addPlugin(new SnakePlugin());
  pluginManager.addPlugin(new GameOfLifePlugin());
  pluginManager.addPlugin(new StarsPlugin());
  pluginManager.addPlugin(new LinesPlugin());
  pluginManager.addPlugin(new CirclePlugin());
  pluginManager.addPlugin(new RainPlugin());
  pluginManager.addPlugin(new FireworkPlugin());
  pluginManager.addPlugin(new PongClockPlugin());

#ifdef ENABLE_SERVER
  pluginManager.addPlugin(new BigClockPlugin());
  pluginManager.addPlugin(new ClockPlugin());
  pluginManager.addPlugin(new WeatherPlugin());
  pluginManager.addPlugin(new AnimationPlugin());
  pluginManager.addPlugin(new TickingClockPlugin());
  pluginManager.addPlugin(new DDPPlugin());
#endif

  pluginManager.init();
}

void loop()
{

  Messages.scrollMessageEveryMinute();
  if(dimMode){
    Screen.checkDimMode();
  }
  
  pluginManager.runActivePlugin();

  if (WiFi.status() != WL_CONNECTED && millis() - lastConnectionAttempt > connectionInterval)
  {
    Serial.println("Lost connection to Wi-Fi. Reconnecting...");
    connectToWiFi();
  }

#ifdef ENABLE_SERVER
  cleanUpClients();
#endif
 //scanNetworkDevices();
 
  delay(10);
}
