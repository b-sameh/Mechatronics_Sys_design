#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "ESP32QRCodeReader.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Camera Settings

ESP32QRCodeReader reader(CAMERA_MODEL_AI_THINKER, FRAMESIZE_QVGA);
#define FLASH_LED_PIN 4

// Wifi - HiveMQ Settings
const char* ssid = "Hussien Basha"; 
const char* password = "012345678";        
const char* mqtt_server = "ab08b93ba41c424c9a36ad7495893579.s1.eu.hivemq.cloud";
const char* mqtt_user   = "Mech.design.project";
const char* mqtt_pass   = "Hussein2862004";

// for secure connection for hivemq

WiFiClientSecure espClient;
PubSubClient client(espClient);

// global variables for camera

bool isPaused = false;      //for pausing the scanning
unsigned long pauseStartTime = 0;   // for recording pause time 

bool isSystemAwake = false;  // for sleep of the camera 

// Wifi Connection function

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

// Camera sleep function 

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim(); // cleans the message

  // topic name check

  if (String(topic) == "trigger") {
    
    if (message == "start") {
      isSystemAwake = true;
      Serial.println(">>> COMMAND: START. Camera is WAKING UP and scanning!");
      
      // empties the memory of the cam for any left data (buffer)
      
      struct QRCodeData dummy;
      while (reader.receiveQrCode(&dummy, 10)) { }
      
    } 
    else if (message == "stop") {
      isSystemAwake = false;
      Serial.println(">>> COMMAND: STOP. Camera is SLEEPING to cool down...");
    }
  }
}

// MQTT connection function
void reconnect() {
  while (!client.connected()) {

    if (client.connect("ESP32_Scanner_Hussein", mqtt_user, mqtt_pass)) {
      Serial.println("Connected to HiveMQ");
      
      // subscribing to trigger topic

      client.subscribe("trigger");
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // so the board wont restart if V < 3.3v
  
  Serial.begin(115200);
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  reader.setup();
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_pixformat(s, PIXFORMAT_GRAYSCALE);  // GRAY PHOTOS
    s->set_contrast(s, 2);                    
    s->set_sharpness(s, 2);                   
    s->set_denoise(s, 1);                     
    s->set_gain_ctrl(s, 1); 
    s->set_exposure_ctrl(s, 1);
  }
  reader.beginOnCore(1); // running the scanner on core 1 

  setup_wifi();
  espClient.setInsecure(); 
  client.setServer(mqtt_server, 8883);
  
  //connection of mqtt with callback function

  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }

  // makes the esp listens to node red at all times

  client.loop();

  // Sleep logic

  if (!isSystemAwake) {
    delay(50); // complete rest for the cam
    return; 
  }

  struct QRCodeData qrCodeData;

  // pause for 5 seconds

  if (isPaused) {
    if (millis() - pauseStartTime >= 5000) { 
      while (reader.receiveQrCode(&qrCodeData, 10)) { }
      isPaused = false; 
      Serial.println("Scanner ACTIVE again.");
    }
    return; 
  }

  // qr scanning
  
  if (reader.receiveQrCode(&qrCodeData, 100)) {
    if (qrCodeData.valid) {
      String decodedWord = (const char *)qrCodeData.payload;
      decodedWord.trim();

      Serial.println(decodedWord);
      client.publish("system/scan", decodedWord.c_str());

      digitalWrite(FLASH_LED_PIN, HIGH);
      delay(100); 
      digitalWrite(FLASH_LED_PIN, LOW);
      
      isPaused = true;
      pauseStartTime = millis();
    }
  }
  
  delay(10); 
}