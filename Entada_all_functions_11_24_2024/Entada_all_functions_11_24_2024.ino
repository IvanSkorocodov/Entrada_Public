#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <Wiegand.h>
#include <EEPROM.h>

#define LED 3          //LED indicator of the door
#define RELAY 3        //Relay for electronic door
#define TRIGGER_PIN 5  // Pin for reconnecting to wifi
#define MSG_BUFFER_SIZE (50)
#define RFID_BUFFER_SIZE 100

String stext, curr;
int attempt, doorflag, numint, nummes, enumer;
int TIME_TO_OPEN = 3000;  // time to door to be opened(ms)
byte offline, flag, flagRFID = 0;
char msgtime[MSG_BUFFER_SIZE];
unsigned long myInts[60];
char cardRFID[15];

unsigned long RFID_message = 0;
unsigned long lastMsg = millis() - 600000;
unsigned long data[RFID_BUFFER_SIZE] PROGMEM = { 94669688, 3099698, 3099699 };
unsigned long doortimer, biptimer, lastcard, num1, lasttry;

const char* mqtt_server = "caoaptip.ddns.net";
const char* aliveTopic = "/entrance_test/front_door/alive";
const char* ipTopic = "/entrance_test/front_door/ip";
const char* commTopic = "/entrance_test/front_door/command";
const char* RFID = "/entrance_test/front_door/string";
const char* audit_log = "/entrance_test/front_door/audit_log";
const char* MQTTuser = "MQTT";
const char* MQTTpass = "q26cywizhBEH";

WiFiManager wm;
WiFiClient espClient;
PubSubClient client(espClient);
WIEGAND wg;

void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(RELAY, OUTPUT);
  wm.setConfigPortalTimeout(2);
  Serial.println("start!");

  if (!wm.autoConnect()) {
    Serial.println("Failed to connect!");
  } else {
    Serial.println("Connected!");
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
  }
  wg.begin(4, 15);

  if (!EEPROM.begin(RFID_BUFFER_SIZE * 4)) {
    Serial.println("EEPROM failed to initialise");
  } else {
    Serial.println("EEPROM initialised");
  }
  reconnect();
  rereading();
}

void rereading() {
  int numint1 = 0;
  int nummes1 = 0;
  Serial.println("EEPROM data: ");
  while (EEPROM.get(numint1, RFID_message) != 0) {
    EEPROM.get(numint1, RFID_message);
    myInts[nummes1] = RFID_message;
    Serial.println(myInts[nummes1]);
    nummes1++;
    numint1 = numint1 + 4;
  }
}

void loop() {
  if (digitalRead(TRIGGER_PIN) == LOW) {

    Serial.println("Click!");
    wm.setConfigPortalTimeout(90);
    if (!wm.startConfigPortal("OnDemandAP")) {
      Serial.println("failed to connect and hit timeout");
      delay(1000);
    }
  }

  if (!client.connected() and millis() - lasttry > 180000) {
    lasttry = millis();
    reconnect();
  }

  if (millis() - doortimer > TIME_TO_OPEN and doorflag == 1) {
    doorflag = 0;
    digitalWrite(RELAY, HIGH);
  }

  client.loop();

  if (wg.available()) {
    Serial.print("Wiegand Recieved = ");
    Serial.println(wg.getCode());
    lastcard = wg.getCode();
    dtostrf(lastcard, 4, 0, cardRFID);
    byte flag1 = 0;
    nummes = 0;
    while (flag1 == 0) {
      if (myInts[nummes] == lastcard) {
        flag1 = 1;
        client.publish(audit_log, "yes");
        Serial.print("card is right!");
        digitalWrite(RELAY, LOW);
        doorflag = 1;
      } else if (myInts[nummes] == 0) {
        flag1 = 1;
        client.publish(audit_log, "unknown card");
        Serial.print("unknown card!");
      }
      Serial.println(nummes);
      nummes++;
    }
    nummes = 0;
  }

  if (millis() - doortimer > TIME_TO_OPEN and doorflag == 1) {
    doorflag = 0;
    doortimer = millis();
    digitalWrite(RELAY, HIGH);
  }
}

void reconnect() {
  byte attempt;
  if (!client.connected()) {

    Serial.println("Attempting MQTT connection...");
    if (!wm.autoConnect()) {
      Serial.println("Failed to connect!");
    } else {
      Serial.println("Connected!");
      client.setServer(mqtt_server, 1883);
      client.setCallback(callback);
    }
    String clientId = "ESP32C3_Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), MQTTuser, MQTTpass)) {
      Serial.println("MQTT connected");
      attempt = 0;
      // Once connected, publish an announcement...
      String LocalIP = String() + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3];
      char charBuf[LocalIP.length() + 1];
      LocalIP.toCharArray(charBuf, LocalIP.length() + 1);
      Serial.println(charBuf);

      client.subscribe(commTopic);
      client.subscribe(RFID);

      lastMsg = millis() - 600000;
    } else {
      attempt++;
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String stext = "";
  String rtext = "";
  for (int i = 0; i < length; i++) { stext += String((char)payload[i]); }
  //Serial.print("Got command from MQTT: ");Serial.println(stext);

  if (String(topic) == commTopic) {
    num1 = stext.toInt();
    doortimer = millis();
    digitalWrite(RELAY, LOW);
    digitalWrite(LED, LOW);
    doorflag = 1;
    Serial.print("Item: ");
    Serial.println(num1);
    TIME_TO_OPEN = num1;
  }

  if (String(topic) == RFID) {
    numint = 0;
    flag = 0;
    num1 = 0;
    nummes = 0;
    curr = "";
    unsigned long zeros;
    Serial.println("RFID info:");
    int i = 0;
    while (nummes < 40) {
      stext += String((char)payload[i]);
      curr = String((char)payload[i]);
      if (curr == ",") {
        EEPROM.get(numint, RFID_message);
        num1 = stext.toInt();
        myInts[nummes] = num1;
        if (RFID_message != num1) {
          Serial.print("Old message: ");
          Serial.println(RFID_message);
          EEPROM.put(numint, num1);

          Serial.print("New message: ");
          Serial.print(num1);
          Serial.print("(");
          Serial.print(nummes);
          Serial.println(")");
        } else {
          Serial.print("Actual RFID: ");
          Serial.print(RFID_message);
          Serial.print("(");
          Serial.print(nummes);
          Serial.println(")");
        }
        numint = numint + 4;
        nummes++;
        num1 = 0;
        stext = "";
      } else if (curr == ";") {
        EEPROM.get(numint, RFID_message);
        num1 = stext.toInt();
        myInts[nummes] = num1;
        if (RFID_message != num1) {
          Serial.print("Old message: ");
          Serial.println(RFID_message);
          EEPROM.put(numint, num1);

          Serial.print("New message: ");
          Serial.print(num1);
        } else {
          Serial.print("Actual RFID: ");
          Serial.print(num1);
        }
        Serial.print(" (");
        Serial.print(nummes);
        Serial.println(")");
        numint = numint + 4;
        nummes++;
        num1 = 0;
        stext = "";
        flag = 1;
      }
      if (flag == 1) {
        EEPROM.get(numint, zeros);
        if (zeros != 0) {
          EEPROM.put(numint, 0);
          Serial.print(numint);
          Serial.println(" is clean!");
        }
        myInts[nummes] = 0;
        numint = numint + 4;
        nummes++;
      }
      i++;
    }
    EEPROM.commit();
    rereading();
  }
}