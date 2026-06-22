#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <esp_now.h>

// ================= WIFI & MQTT CONFIG =================
const char* ssid     = "Bolbol";
const char* password = "12345678";

const char* mqtt_server = "ab08b93ba41c424c9a36ad7495893579.s1.eu.hivemq.cloud";
const int   mqtt_port   = 8883;
const char* mqtt_user   = "Mech.design.project";
const char* mqtt_pass   = "Hussein2862004";

WiFiClientSecure espClient;
PubSubClient client(espClient);

// ================= ESP-NOW CONFIG =================
// MAC of CAR ESP32
uint8_t carPeerAddress[] = {0x68, 0xFE, 0x71, 0x87, 0xDB, 0x60};
esp_now_peer_info_t espNowPeerInfo;

typedef struct struct_message {
  char text[32];
} struct_message;

struct_message espNowData;
struct_message espNowSendData;

// ================= SERVOS =================
Servo servo1;
Servo servo2;
Servo servo3;
Servo servo4;

const int servo1Pin = 5;
const int servo2Pin = 18;
const int servo3Pin = 19;
const int servo4Pin = 21;

int pos1 = 80;
int pos2 = 115;
int pos3 = 70;
int pos4 = 60;

// Default speed for all normal movements
const int STEP_DELAY_MS = 1;
const int STEP_SIZE_DEG = 3;

// Midpoint-only speed
const int MIDPOINT_STEP_DELAY_MS = 1;
const int MIDPOINT_STEP_SIZE_DEG = 1;

// ================= AUTOMATION FLAGS =================
volatile bool runAuto1 = false;
volatile bool runAuto2 = false;
volatile bool runAuto3 = false;
bool autoBusy = false;

// ================= SERVO HELPERS =================
void smoothMoveSingleServoCustom(Servo &servo, int &currentPos, int targetPos, const char* name, int stepSizeDeg, int stepDelayMs) {
  targetPos = constrain(targetPos, 0, 180);
  if (currentPos == targetPos) return;

  int step = (targetPos > currentPos) ? stepSizeDeg : -stepSizeDeg;

  Serial.print("Moving ");
  Serial.print(name);
  Serial.print(" from ");
  Serial.print(currentPos);
  Serial.print(" to ");
  Serial.print(targetPos);
  Serial.print(" | step=");
  Serial.print(stepSizeDeg);
  Serial.print(" delay=");
  Serial.println(stepDelayMs);

  while (currentPos != targetPos) {
    if ((step > 0 && currentPos + step >= targetPos) ||
        (step < 0 && currentPos + step <= targetPos)) {
      currentPos = targetPos;
    } else {
      currentPos += step;
    }

    servo.write(currentPos);
    delay(stepDelayMs);
  }

  Serial.print(name);
  Serial.print(" reached ");
  Serial.println(currentPos);
}

void smoothMoveSingleServo(Servo &servo, int &currentPos, int targetPos, const char* name) {
  smoothMoveSingleServoCustom(servo, currentPos, targetPos, name, STEP_SIZE_DEG, STEP_DELAY_MS);
}

void smoothMoveToAnglesSequential(int target1, int target2, int target3) {
  target1 = constrain(target1, 0, 180);
  target2 = constrain(target2, 0, 180);
  target3 = constrain(target3, 0, 180);

  smoothMoveSingleServo(servo2, pos2, target2, "servo2");
  delay(80);

  smoothMoveSingleServo(servo1, pos1, target1, "servo1");
  delay(80);

  smoothMoveSingleServo(servo3, pos3, target3, "servo3");
  delay(80);

  Serial.print("Final position -> s1:");
  Serial.print(pos1);
  Serial.print("  s2:");
  Serial.print(pos2);
  Serial.print("  s3:");
  Serial.println(pos3);
}

void setGripSmooth(int targetAngle) {
  targetAngle = constrain(targetAngle, 0, 180);
  if (pos4 == targetAngle) return;

  int step = (targetAngle > pos4) ? STEP_SIZE_DEG : -STEP_SIZE_DEG;

  Serial.print("Moving gripper from ");
  Serial.print(pos4);
  Serial.print(" to ");
  Serial.println(targetAngle);

  while (pos4 != targetAngle) {
    if ((step > 0 && pos4 + step >= targetAngle) ||
        (step < 0 && pos4 + step <= targetAngle)) {
      pos4 = targetAngle;
    } else {
      pos4 += step;
    }
    servo4.write(pos4);
    delay(STEP_DELAY_MS);
  }

  Serial.print("Gripper reached ");
  Serial.println(pos4);
}

// ================= PRESET ACTIONS =================
void goRed()   { smoothMoveToAnglesSequential(42, 7, 166); }
void goGreen() { smoothMoveToAnglesSequential(76, 32, 164); }
void goBlue()  { smoothMoveToAnglesSequential(105, 57, 160); }
void goScan()  { smoothMoveToAnglesSequential(80, 115, 70); }

void goMidpoint() {
  smoothMoveSingleServoCustom(servo1, pos1, 70, "servo1", MIDPOINT_STEP_SIZE_DEG, MIDPOINT_STEP_DELAY_MS);
  delay(80);

  smoothMoveSingleServoCustom(servo2, pos2, 61, "servo2", MIDPOINT_STEP_SIZE_DEG, MIDPOINT_STEP_DELAY_MS);
  delay(80);

  smoothMoveSingleServoCustom(servo3, pos3, 180, "servo3", MIDPOINT_STEP_SIZE_DEG, MIDPOINT_STEP_DELAY_MS);
  delay(80);

  Serial.print("Midpoint reached -> s1:");
  Serial.print(pos1);
  Serial.print("  s2:");
  Serial.print(pos2);
  Serial.print("  s3:");
  Serial.println(pos3);
}

void goInput()   { smoothMoveToAnglesSequential(69, 134, 40); }
void goOutput()  { smoothMoveToAnglesSequential(69, 134, 40); }
void goOutput2() { smoothMoveToAnglesSequential(15, 180, 0); }
void doGrip()    { setGripSmooth(5); }
void doUngrip()  { setGripSmooth(60); }

// ================= ESP-NOW SEND =================
void sendESPNowMessage(const char* msg) {
  memset(&espNowSendData, 0, sizeof(espNowSendData));
  strncpy(espNowSendData.text, msg, sizeof(espNowSendData.text) - 1);

  esp_err_t result = esp_now_send(carPeerAddress, (uint8_t *)&espNowSendData, sizeof(espNowSendData));

  Serial.print("ESP-NOW queue result: ");
  Serial.println(result == ESP_OK ? "Queued" : "Error");
}

// ================= PUBLISH FINISH =================
void publishAutoDone(const char* msg) {
  if (client.connected()) {
    if (client.publish("auto/base/out", msg)) {
      Serial.print("Published to MQTT auto/base/out: ");
      Serial.println(msg);
    } else {
      Serial.print("Failed to publish to MQTT auto/base/out: ");
      Serial.println(msg);
    }
  } else {
    Serial.println("MQTT not connected, skipped MQTT publish");
  }
}

// ================= AUTOMATION SEQUENCES =================
void executeAuto1() {
  Serial.println("Executing AUTO1");
  goBlue();
  doGrip();
  goMidpoint();
  goOutput();
  goOutput2();
  doUngrip();
  goMidpoint();
  publishAutoDone("finish1");
}

void executeAuto2() {
  Serial.println("Executing AUTO2");
  goGreen();
  doGrip();
  goOutput();
  goOutput2();
  doUngrip();
  goMidpoint();
  publishAutoDone("finish2");
}

void executeAuto3() {
  Serial.println("Executing AUTO3");
  goRed();
  doGrip();
  goOutput();
  goOutput2();
  doUngrip();
  goMidpoint();
  publishAutoDone("finish3");
}

// ================= COMMAND ROUTER =================
void handleCommand(String msg, const char* source) {
  msg.trim();
  msg.toLowerCase();

  if (msg.length() == 0) return;

  Serial.print("[");
  Serial.print(source);
  Serial.print("] Command: ");
  Serial.println(msg);

  if (!autoBusy) {
    if (msg == "ready1") {
      runAuto1 = true;
      Serial.println("Queued AUTO1");
      return;
    } else if (msg == "ready2") {
      runAuto2 = true;
      Serial.println("Queued AUTO2");
      return;
    } else if (msg == "ready3") {
      runAuto3 = true;
      Serial.println("Queued AUTO3");
      return;
    }
  } else {
    if (msg == "ready1" || msg == "ready2" || msg == "ready3") {
      Serial.println("Automation busy, command ignored");
      return;
    }
  }

  if (msg == "red") goRed();
  else if (msg == "green") goGreen();
  else if (msg == "blue") goBlue();
  else if (msg == "scan") goScan();
  else if (msg == "midpoint") goMidpoint();
  else if (msg == "input") goInput();
  else if (msg == "output") goOutput();
  else if (msg == "output2") goOutput2();
  else if (msg == "grip") doGrip();
  else if (msg == "ungrip") doUngrip();
  else if (msg == "c") {
    smoothMoveSingleServo(servo1, pos1, pos1 + STEP_SIZE_DEG, "servo1");
  }
  else if (msg == "d") {
    smoothMoveSingleServo(servo1, pos1, pos1 - STEP_SIZE_DEG, "servo1");
  }
  else if (msg == "j") {
    smoothMoveSingleServo(servo2, pos2, pos2 + STEP_SIZE_DEG, "servo2");
  }
  else if (msg == "k") {
    smoothMoveSingleServo(servo2, pos2, pos2 - STEP_SIZE_DEG, "servo2");
  }
  else if (msg == "n") {
    smoothMoveSingleServo(servo3, pos3, pos3 + STEP_SIZE_DEG, "servo3");
  }
  else if (msg == "o") {
    smoothMoveSingleServo(servo3, pos3, pos3 - STEP_SIZE_DEG, "servo3");
  }
  else if (msg == "p") {
    setGripSmooth(pos4 + STEP_SIZE_DEG);
  }
  else if (msg == "t") {
    setGripSmooth(pos4 - STEP_SIZE_DEG);
  }
  else if (msg == "pingcar") sendESPNowMessage("ping");
  else if (msg.startsWith("servo1 ")) {
    smoothMoveSingleServo(servo1, pos1, msg.substring(7).toInt(), "servo1");
  }
  else if (msg.startsWith("servo2 ")) {
    smoothMoveSingleServo(servo2, pos2, msg.substring(7).toInt(), "servo2");
  }
  else if (msg.startsWith("servo3 ")) {
    smoothMoveSingleServo(servo3, pos3, msg.substring(7).toInt(), "servo3");
  }
  else if (msg.startsWith("servo4 ")) {
    setGripSmooth(msg.substring(7).toInt());
  }
  else {
    Serial.println("Unknown command");
  }
}

// ================= SERIAL INPUT =================
void handleSerialInput() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      handleCommand(input, "SERIAL");
    }
  }
}

// ================= ESP-NOW CALLBACKS =================
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("ESP-NOW delivery: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  memset(&espNowData, 0, sizeof(espNowData));
  memcpy(&espNowData, incomingData, min(len, (int)sizeof(espNowData)));

  String msg = String(espNowData.text);
  msg.trim();

  Serial.print("ESP-NOW received: ");
  Serial.println(msg);

  handleCommand(msg, "ESP-NOW");
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  Serial.print("MQTT message - topic: ");
  Serial.print(t);
  Serial.print("  payload: ");
  Serial.println(msg);

  if (t == "arm/post" || t == "auto/base/in") {
    handleCommand(msg, "MQTT");
  }
  else if (t == "servo1") {
    smoothMoveSingleServo(servo1, pos1, msg.toInt(), "servo1");
  }
  else if (t == "servo2") {
    smoothMoveSingleServo(servo2, pos2, msg.toInt(), "servo2");
  }
  else if (t == "servo3") {
    smoothMoveSingleServo(servo3, pos3, msg.toInt(), "servo3");
  }
}

// ================= MQTT RECONNECT =================
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection to ");
    Serial.print(mqtt_server);
    Serial.print(":");
    Serial.println(mqtt_port);

    String clientId = "ESP32_ARM_";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("MQTT connected");
      client.subscribe("arm/post");
      client.subscribe("servo1");
      client.subscribe("servo2");
      client.subscribe("servo3");
      client.subscribe("auto/base/in");
      Serial.println("Subscribed to topics");
    } else {
      Serial.print("MQTT connect failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      delay(2000);
    }
  }
}

// ================= WIFI =================
void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("WiFi channel: ");
  Serial.println(WiFi.channel());
  Serial.print("This board MAC: ");
  Serial.println(WiFi.macAddress());
}

// ================= ESP-NOW SETUP =================
void setupEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  memset(&espNowPeerInfo, 0, sizeof(espNowPeerInfo));
  memcpy(espNowPeerInfo.peer_addr, carPeerAddress, 6);
  espNowPeerInfo.channel = WiFi.channel();
  espNowPeerInfo.encrypt = false;

  if (esp_now_add_peer(&espNowPeerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
    return;
  }

  Serial.println("ESP-NOW ready");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  delay(2000);
  Serial.println("Booting System 2 ARM...");

  servo1.setPeriodHertz(50);
  servo2.setPeriodHertz(50);
  servo3.setPeriodHertz(50);
  servo4.setPeriodHertz(50);

  servo1.attach(servo1Pin, 500, 2400);
  servo2.attach(servo2Pin, 500, 2400);
  servo3.attach(servo3Pin, 500, 2400);
  servo4.attach(servo4Pin, 500, 2400);

  servo1.write(pos1);
  servo2.write(pos2);
  servo3.write(pos3);
  servo4.write(pos4);

  setupWiFi();
  setupEspNow();

  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  Serial.println("Ready.");
  Serial.println("Commands:");
  Serial.println("ready1 | ready2 | ready3");
  Serial.println("red | green | blue | scan | midpoint | input | output | output2");
  Serial.println("grip | ungrip");
  Serial.println("servo1 90 | servo2 120 | servo3 45 | servo4 30");
  Serial.println("pingcar");
}

// ================= LOOP =================
void loop() {
  handleSerialInput();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (!autoBusy) {
    if (runAuto1) {
      autoBusy = true;
      runAuto1 = false;
      executeAuto1();
      autoBusy = false;
    }
    else if (runAuto2) {
      autoBusy = true;
      runAuto2 = false;
      executeAuto2();
      autoBusy = false;
    }
    else if (runAuto3) {
      autoBusy = true;
      runAuto3 = false;
      executeAuto3();
      autoBusy = false;
    }
  }
}