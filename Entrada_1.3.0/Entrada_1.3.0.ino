#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <WiegandMulti.h>
#include <EEPROM.h>
#include "GyverButton.h"

#define LED 3          //LED indicator of the door
#define RELAY 19        //Relay for electronic door
#define GERCON 34        //Relay for electronic door
#define TRIGGER_PIN 2  // Pin for reconnecting to wifi
#define RFID_BUFFER_SIZE 100 //number of card(unsigned long)

String stext, curr;
int doorflag;
int numtry;
int TIME_TO_OPEN = 1000;  // time to door to be opened(ms)
unsigned long myInts[60];
unsigned long CoolDownForce;
char cardRFID[15];

unsigned long lastMsg = millis() - 600000;
unsigned long data[RFID_BUFFER_SIZE] PROGMEM = { 94669688, 3099698, 3099699 };
unsigned long doortimer, biptimer, lastcard, num1, lasttry;

const char* mqtt_server = "caoaptip.ddns.net";
const char* aliveTopic = "/entrance/front_door/alive";
const char* ipTopic = "/entrance/front_door/ip";
const char* commTopic = "/entrance/front_door/command";
const char* RFID = "/entrance/front_door/string";
const char* audit_log = "/entrance/front_door/audit_log";
const char* MQTTuser = "MQTT";
const char* MQTTpass = "q26cywizhBEH";
const char* sismsgTopic = "/entrance/front_door/sismsg";
const char* TakeTopic = "/entrance/front_door/Read_RFID";
const char* RSSITopic = "/entrance/front_door/rssi";

WiFiManager wm;
WiFiClient espClient;
PubSubClient client(espClient);
WIEGANDMULTI wg;
GButton Gbutt(GERCON);

void setup() {
  Serial.begin(115200);
  attachInterrupt(1, isr, CHANGE);
  Serial.print("Setup Start!");
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, HIGH);
	wg.begin(15,4,ReaderD0Interrupt,ReaderD1Interrupt);

  if (!EEPROM.begin(RFID_BUFFER_SIZE * 4)) {
    Serial.println("EEPROM failed to initialise");
  } 
  else {
    Serial.println("EEPROM initialised");
  }
  wm.setEnableConfigPortal(false);
  reconnect();
  wm.setEnableConfigPortal(true);
  rereading();
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

  if (millis() - lasttry > 600000) {
    if(!client.connected()){reconnect();}
    lasttry = millis();
    client.publish(aliveTopic, "Bip!");
  }

  if (millis() - doortimer > TIME_TO_OPEN and doorflag == 1) {
    doorflag = 0;
    digitalWrite(RELAY, HIGH);
  }

  client.loop();
  Gbutt.tick();
  if (Gbutt.isPress()) Serial.println("Press");
  if (Gbutt.isRelease()) Serial.println("Release");
  if (wg.available()) {
    Serial.print("Wiegand Recieved = ");
    Serial.println(wg.getCode());
    lastcard = wg.getCode();
    dtostrf(lastcard, 4, 0, cardRFID);
    byte flag1 = 0;
    int nummes = 0;
    if(CoolDownForce + 6000 < millis() and numtry >= 5){
      numtry = 0;
    }
    while (flag1 == 0 and numtry < 5) {
      if (myInts[nummes] == lastcard) {
        flag1 = 1;
        client.publish(audit_log, "yes");
        client.publish(TakeTopic, cardRFID);
        Serial.print("card is right!");
        digitalWrite(RELAY, LOW);
        numtry = 0;
        doorflag = 1; 
        doortimer = millis();
      } else if (myInts[nummes] == 0) {
        flag1 = 1;
        client.publish(audit_log, "unknown card");
        client.publish(TakeTopic, cardRFID);
        Serial.print("unknown card!");
        numtry++;
      }
      Serial.println(nummes);
      Serial.println(numtry);
      nummes++;
    }
    if(numtry == 5){CoolDownForce = millis();client.publish(sismsgTopic, "Too many attempts to open the door!");}
    nummes = 0;
  }

  if (millis() - doortimer > TIME_TO_OPEN and doorflag == 1) {
    doorflag = 0;
    doortimer = millis();
    digitalWrite(RELAY, HIGH);
  }
}

void isr() {
  Gbutt.tick();  // опрашиваем в прерывании, чтобы поймать нажатие в любом случае
}

void rereading() {
  int nummes1 = 0;
  unsigned long RFID_message;
  Serial.println("EEPROM data: ");
  while (EEPROM.get(nummes1*4, RFID_message) != 4294967295) {
    EEPROM.get(nummes1*4, RFID_message);
    myInts[nummes1] = RFID_message;
    Serial.println(myInts[nummes1]);
    nummes1++;
  }
}

void reconnect() {
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
      // Once connected, publish an announcement...
      String LocalIP = String() + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3];
      char charBuf[LocalIP.length() + 1];
      LocalIP.toCharArray(charBuf, LocalIP.length() + 1);
      Serial.println(charBuf);

      client.subscribe(commTopic);
      client.subscribe(RFID);
      client.publish(ipTopic, charBuf);
      lastMsg = millis() - 600000;
    } else {
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String stext = "";
  String rtext = "";
  for (int i = 0; i < length; i++) {stext += String((char)payload[i]); }

  if (String(topic) == commTopic) {
    num1 = stext.toInt();
    doortimer = millis();
    digitalWrite(RELAY, LOW);
    digitalWrite(LED, LOW);
    doorflag = 1;
    Serial.println("Open the door!");
  }

  else if (String(topic) == RFID) {
    int nummes = 0;
    int i;
    unsigned long RFID_message;
    byte flagend = 0;
    num1 = 0;
    curr = "";
    unsigned long zeros;
    Serial.println("RFID info:");
    while (flagend != 2) {
      stext += String((char)payload[i]);
      curr = String((char)payload[i]);
      if (curr == ",") {
        EEPROM.get(nummes*4, RFID_message);
        num1 = strtoul(stext.c_str(), NULL, 10);
        myInts[nummes] = num1;
        if (nummes > RFID_BUFFER_SIZE - 5) {
          client.publish(sismsgTopic, "Message is too big!");
        }
        if (RFID_message != num1) {
          Serial.print("Old message: ");
          Serial.println(RFID_message);
          EEPROM.put(nummes*4, num1);

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
        nummes++;
        num1 = 0;
        stext = "";
      } else if (curr == ";") {
        EEPROM.get(nummes*4, RFID_message);
        num1 = strtoul(stext.c_str(), NULL, 10);
        myInts[nummes] = num1;
        if (RFID_message != num1) {
          Serial.print("Old message: ");
          Serial.println(RFID_message);
          EEPROM.put(nummes*4, num1);

          Serial.print("New message: ");
          Serial.print(num1);
        } else {
          Serial.print("Actual RFID: ");
          Serial.print(num1);
        }
        Serial.print("(");
        Serial.print(nummes);
        Serial.println(")");
        nummes++;
        num1 = 0;
        stext = "";
        flagend = 1;
      }
      if (flagend == 1) {
        EEPROM.get(nummes*4, zeros);
        if (zeros != 4294967295) {
          EEPROM.put(nummes*4, 4294967295);
          Serial.print(nummes*4);
          Serial.println(" is clean!");
        }
        myInts[nummes] = 0;
        nummes++;
        flagend = 2;
      }
      i++;
    }
    EEPROM.commit();
    rereading();
  }
}

void ReaderD0Interrupt(void) wg.ReadD0();

void ReaderD1Interrupt(void) wg.ReadD1();