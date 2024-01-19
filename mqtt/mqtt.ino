#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include "wifi_utils.h"
#include "OneWire.h"
#include "DallasTemperature.h"
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <HTTPClient.h>

// Flask server to run http requests
const char* serverName = "https://waterbnb-22311680.onrender.com/get_access_status/";

// Manage http and mqtt requests to avoid overlap
unsigned long lastHttpCheck = 0;
const unsigned long httpCheckInterval = 1000; // 1 second interval for HTTP request

unsigned long lastMqttPublish = 0;
const unsigned long mqttPublishInterval = 10000; // 10 seconds interval for MQTT publish

// Manage sensor information check frequency
unsigned long lastSensorCheck = 0;
const unsigned long sensorCheckInterval = 1000; // 1 second interval for sensor information check

// Pool ID used to check granted status for that particular pool
String piscine_id = "P_22311680";


const int ledPin = 2; // LED Pin
const int lightSensorPin = 33;

// LED Strip Definitions
const int ledStripPin = 12; // LED strip connected to GPIO 13
#define NUMLEDS 5
Adafruit_NeoPixel strip(NUMLEDS, ledStripPin, NEO_GRB + NEO_KHZ800);

// Define colors
const uint32_t GREEN = strip.Color(0, 255, 0);  // Green color
const uint32_t YELLOW = strip.Color(255, 255, 0); // Yellow color
const uint32_t RED = strip.Color(255, 0, 0);    // Red color

// Manage red led time
bool lastGrantedState = false; // Tracks the last known 'granted' state
bool granted = true;
unsigned long redLedStartTime = 0;
const unsigned long redLedDuration = 3000; // 3 seconds
bool isRedLedOn = false;

// Manage mqtt publish frequency
const unsigned long publish_freq = 10000; // 10 seconds
unsigned long publishStartTime = 0;

// Count number of mqtt messages received in order to decide wether hotspot should be updated to true
const int nbMqttMessagesThresh = 10;
int nbMqttMessagesReceived = 0;

// Temperature 
OneWire oneWire(23); // For using a oneWire entity on port 23
DallasTemperature tempSensor(&oneWire);
float temperature = 0;

// MQTT initialization
#define MQTT_HOST IPAddress(192, 168, 1, XXX)
const char* mqtt_server = "test.mosquitto.org"; // MQTT server

#define TOPIC_TEMP "uca/iot/piscine"
#define TOPIC_GRANTED "uca/iot/tajine_granted"


WiFiClient espClient;
PubSubClient mqttclient(espClient);
String hostname = "CouscousESP32";

// Structure for storing environmental data in JSON format
typedef struct {
  // Status
  int luminosity;
  float temperature; 
  String regulationState; // Regulation status: "RUNNING" or "HALT"
  bool fireDetected = false;
  String coolerState = "NOP"; // "ON" or "OFF"
  String heaterState = "NOP"; // "ON" or "OFF"

  // Location
  String room = "NOP";
  /*
  float longitude = 7.208682642480482;
  float latitude = 43.661376231734295;
  String address = "Aéroport Nice Côte d'azur";
  */
  float longitude = 7.034842606738261;
  float latitude = 43.60032583080971;
  String address = "Route de Mougins";

  

  // Regulation
  float highThreshold = 0;
  float lowThreshold = 0;

  // Info
  String ident = "P_22311680";
  String loc = "NOP";
  String user = hostname;

  // Network 
  String uptime = "NOP";
  String ssid = "NOP";
  String mac = "NOP";
  String ip = "NOP";

  // Report host
  String target_ip = "NOP";
  int target_port = 0;
  int sp = 0;

  // Pool section
  bool hotspot = true;
  bool occupied = false; 
} EspModel;

EspModel esp;


#define USE_SERIAL Serial

#define EARTH_RADIUS 6371.0 // Earth radius in Km
const int light_threshold = 1000;
char payload[1024];

void setup() {
  Serial.begin(9600);
  while (!Serial); // wait for a serial connection, needed for native USB port only

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  tempSensor.begin();

  strip.begin();
  strip.show();

  wifi_connect_multi(hostname);
  wifi_printstatus(0);

  esp.mac = WiFi.macAddress();
  esp.ip = WiFi.localIP().toString();
  esp.ssid = WiFi.SSID();

  // Create a JSON document
  StaticJsonDocument<1024> jdoc;
  setJdoc(jdoc);
  
  // Serialize JSON to a string
  serializeJson(jdoc, payload);

  mqttclient.setServer(mqtt_server, 1883);
  mqttclient.setCallback(mqtt_pubcallback);
  mqttclient.setBufferSize(1024);
}



void loop() {
  unsigned long currentMillis = millis();

  // MQTT Subscription
  mqtt_subscribe_mytopics();

  // MQTT Publish (non-blocking)
  if (currentMillis - lastMqttPublish > mqttPublishInterval) {
    lastMqttPublish = currentMillis;
    publishMqttData();
  }

  // HTTP Check (non-blocking)
  if (currentMillis - lastHttpCheck > httpCheckInterval) {
    lastHttpCheck = currentMillis;
    checkAccessStatus();
  }


  // Sensor readings and pool occupation check
  handleSensorReadings();

  // MQTT client loop
  mqttclient.loop();
}


// ################# MQTT functions #################

void mqtt_subscribe_mytopics() {
  while (!mqttclient.connected()) {
    USE_SERIAL.print("Attempting MQTT connection...");
    String mqttclientId = "ESP32-" + WiFi.macAddress();
    if (mqttclient.connect(mqttclientId.c_str(), NULL, NULL)) {
      USE_SERIAL.println("connected");
      mqttclient.subscribe(TOPIC_TEMP, 1);
      mqttclient.subscribe(TOPIC_GRANTED, 1);
    } else {
      USE_SERIAL.print("failed, rc=");
      USE_SERIAL.print(mqttclient.state());
      USE_SERIAL.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void publishMqttData() {
  StaticJsonDocument<1024> jdoc;
  setJdoc(jdoc);
  serializeJson(jdoc, payload);
  
  USE_SERIAL.print("Publish payload : "); USE_SERIAL.print(payload);
  USE_SERIAL.print(" on topic : "); USE_SERIAL.println(TOPIC_TEMP);
  
  mqttclient.publish(TOPIC_TEMP, payload);
}


void mqtt_pubcallback(char* topic, byte* payload, unsigned int length) {
  USE_SERIAL.print("Message arrived on topic : ");
  USE_SERIAL.println(topic);
  USE_SERIAL.print("=> ");

  // Convert payload to String
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  USE_SERIAL.println(message);

  if (strcmp(topic, TOPIC_TEMP) == 0){
  
    // Parse the JSON document
    DynamicJsonDocument jsonDoc(1024);
    DeserializationError error = deserializeJson(jsonDoc, message);
  
    // Check for parsing errors
    if (error) {
      USE_SERIAL.print(F("deserializeJson() failed: "));
      USE_SERIAL.println(error.c_str());
      return;
    }
  
    // Mettre à jour les données ESP32 reçues
    String senderMacAddress = jsonDoc["net"]["mac"];
    
    if (senderMacAddress == esp.mac){
      USE_SERIAL.print("My data has been received!!! Nothing to process!");
    }
    else {
      // Extract temp and location information from JSON
      float senderTemp = jsonDoc["status"]["temperature"];
      float senderLatitude = jsonDoc["location"]["gps"]["lat"];
      float senderLongitude = jsonDoc["location"]["gps"]["lon"];  
    
      // Calculate distance using Haversine formula
      float distance = calculateHaversineDistance(esp.latitude, esp.longitude, senderLatitude, senderLongitude);
    
      USE_SERIAL.print("Distance to sender: ");
      USE_SERIAL.print(distance, 2); // Print with 2 decimal places for precision
      USE_SERIAL.println(" km");

      // Return to hotspot status after a while if no one is hotter around
      if (nbMqttMessagesReceived == nbMqttMessagesThresh) {
        esp.hotspot = true;
        nbMqttMessagesReceived = 0;
      }
      
      
      if(distance <= 10){
        float senderTemp = jsonDoc["status"]["temperature"];
        if(senderTemp > esp.temperature){
          esp.hotspot = false;
        }
      }
    }
  
  
    // Increment mqtt messages received
    nbMqttMessagesReceived = nbMqttMessagesReceived + 1;
    // Update LED according to hotspot status
    updateHotspotLed(esp.hotspot);
  }
  
}


// ################# Utils #################


// Update pin LED according to hotspot status
void updateHotspotLed(bool hotspotStatus) {
  if (hotspotStatus) {
    digitalWrite(ledPin, HIGH); // Turn on LED if hotspot
  } else {
    digitalWrite(ledPin, LOW); // Turn off LED if not hotspot
  }
}

// Function to calculate Haversine distance between two points
float calculateHaversineDistance(float lat1, float lon1, float lat2, float lon2) {
  // Convert latitude and longitude from degrees to radians
  lat1 = radians(lat1);
  lon1 = radians(lon1);
  lat2 = radians(lat2);
  lon2 = radians(lon2);

  // Haversine formula
  float dlat = lat2 - lat1;
  float dlon = lon2 - lon1;
  float a = pow(sin(dlat / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(dlon / 2), 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));

  // Distance in kilometers
  float distance = EARTH_RADIUS * c;

  return distance;
}




// Function to get temperature from the sensor
float getTemperature() {
  tempSensor.requestTemperatures();
  return tempSensor.getTempCByIndex(0);
}

void verify_pool_occupation(){
    int sensorValue;
    sensorValue = analogRead(lightSensorPin); // Read analog input on ADC1_CHANNEL_5 (GPIO 33)
    esp.luminosity = sensorValue;

    if(sensorValue < light_threshold){
      esp.occupied = true;
    }else{
      esp.occupied = false;
    }
}

// Function to populate JSON data structure
void setJdoc(StaticJsonDocument<1024>& jdoc) {
  JsonObject status = jdoc.createNestedObject("status");
  status["temperature"] = esp.temperature;
  status["light"] = esp.luminosity; 
  status["heat"] = esp.heaterState;
  status["cold"] = esp.coolerState;
  status["fire"] = esp.fireDetected;
  status["regul"] = esp.regulationState;

  JsonObject location = jdoc.createNestedObject("location");
  location["room"] = esp.room;
  JsonObject gps = location.createNestedObject("gps");
  gps["lat"] = esp.latitude;
  gps["lon"] = esp.longitude;
  location["address"] = esp.address;

  JsonObject regulation = jdoc.createNestedObject("regul");
  regulation["ht"] = esp.highThreshold;
  regulation["lt"] = esp.lowThreshold;

  JsonObject info = jdoc.createNestedObject("info");
  info["ident"] = esp.ident;
  info["loc"] = esp.loc;
  info["user"] = esp.user;

  JsonObject network = jdoc.createNestedObject("net");
  network["uptime"] = esp.uptime;
  network["ssid"] = esp.ssid;
  network["mac"] = esp.mac;
  network["ip"] = esp.ip;

  JsonObject reportHost = jdoc.createNestedObject("reporthost");
  reportHost["target_ip"] = esp.target_ip;
  reportHost["target_port"] = esp.target_port;
  reportHost["sp"] = esp.sp;

  JsonObject piscine = jdoc.createNestedObject("piscine"); // Corrected variable name
  piscine["hotspot"] = esp.hotspot;
  piscine["occuped"] = esp.occupied; // Corrected variable name
}




// ############## Led strip ##############

// Function to set the color of the entire strip
void setColor(uint32_t color) {
    for(int i = 0; i < strip.numPixels(); i++) {
        strip.setPixelColor(i, color);
    }
    strip.show();
}




// Update LED color according to granted and occupied status
void updatePoolStatus() {
    unsigned long currentMillis = millis();

    USE_SERIAL.println("GRANTED in update pool status");
    USE_SERIAL.println(granted);
    USE_SERIAL.println("REDLEDON in update pool status");
    USE_SERIAL.println(isRedLedOn);
    
    // Handle Red LED (if access is not granted and Red LED is not already on)
    if (!granted && !isRedLedOn) {
        setColor(RED); // Turn on Red LED
        redLedStartTime = currentMillis;
        isRedLedOn = true;
        return; // Skip further processing to maintain Red LED state
    }

    // Check if Red LED duration has elapsed
    if (isRedLedOn && (currentMillis - redLedStartTime > redLedDuration)) {
        isRedLedOn = false; // Turn off Red LED
    }

    // Update LED color based on Occupied status if Red LED is not on
    if (!isRedLedOn) {
        if (esp.occupied) {
            setColor(YELLOW); // Occupied - Yellow
        } else {
            setColor(GREEN); // Not Occupied - Green
        }
    }
}


// Function to make HTTP GET request
void checkAccessStatus() {
  HTTPClient http;
  http.begin(String(serverName) + piscine_id); 
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    String payload = http.getString();
    deserializeAndApplyPayload(payload);  // Implement this function
  } else {
    Serial.print("Error on HTTP request: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}


// Update pool status if access granted
void deserializeAndApplyPayload(const String& payload) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, payload);
  String granted_result = doc["granted"];

  USE_SERIAL.println("############### Granted result: ###############");
  USE_SERIAL.println(granted_result);

  if (granted_result == "YES"){
    granted = true;
  } else {
    granted = false;
  }

  updatePoolStatus();

  
}


void handleSensorReadings() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastSensorCheck > sensorCheckInterval) {
    temperature = getTemperature();
    esp.temperature = temperature;
    verify_pool_occupation();
    updatePoolStatus();
  }
  
}



// ################# Tentative pour stocker les piscines dans une map #################

// Fonction pour mettre à jour les données d'un ESP32 spécifique
/*
void updateEspData(String macAddress, float temp, float lat, float lon) {
  EspData data = {temp, lat, lon, millis()};
  espDataMap[macAddress] = data;
  printEspDataMap(); // To debug
}
*/
// Used to debug
/*
void printEspDataMap() {
  Serial.println("Affichage des données des ESP32 :");
  for (const auto& kv : espDataMap) {
    String macAddress = kv.first;
    EspData data = kv.second;

    Serial.print("ESP32 MAC: "); Serial.println(macAddress);
    Serial.print("  - Température: "); Serial.println(data.temperature);
    Serial.print("  - Latitude: "); Serial.println(data.latitude, 6); // 6 décimales pour la précision
    Serial.print("  - Longitude: "); Serial.println(data.longitude, 6);
    Serial.print("  - Timestamp: "); Serial.println(data.timestamp);
    Serial.println(); // Ligne vide pour une meilleure lisibilité
  }
}

*/

// Fonction pour nettoyer les données obsolètes
/*
void cleanupOldData() {
  unsigned long currentTime = millis();
  if (currentTime - lastCleanup > dataLifetime) {
    for (auto it = espDataMap.begin(); it != espDataMap.end(); ) {
      if (currentTime - it->second.timestamp > dataLifetime) {
        it = espDataMap.erase(it);
      } else {
        ++it;
      }
    }
    lastCleanup = currentTime;
  }
}
*/


// Fonction pour déterminer si cet ESP32 est un hotspot
/*
void checkIfHotspot() {
  bool isHotspot = true;
  for (const auto& kv : espDataMap) {
    EspData data = kv.second;
    float distance = calculateHaversineDistance(esp.latitude, esp.longitude, data.latitude, data.longitude);
    
    USE_SERIAL.print("Distance to sender: ");
    USE_SERIAL.print(distance, 2); // Print with 2 decimal places for precision
    USE_SERIAL.println(" km");
  
    if (distance <= 10.0 && data.temperature > esp.temperature) {
      isHotspot = false;
      break;
    }
  }

  esp.hotspot = isHotspot;
}

/*
 int getPoolStatus(bool occupied, bool accessGranted) {
  int statusPool;

  if (occupied == false) {
    statusPool = 1;
  }
  else if (occupied == true) {
    statusPool = 2;
  }

  USE_SERIAL.println("############### Access granted: ###############");
  USE_SERIAL.println(accessGranted);
  // TODO access granted
  if (accessGranted == false) {
    
    statusPool = 3;
  }
  

  return statusPool;
}
 */
