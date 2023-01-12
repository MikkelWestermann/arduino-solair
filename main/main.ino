#include <Servo.h>  // servo library
#include <Stepper.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <ThingSpeak.h>
#include <stdio.h>
#include <stdlib.h>
#include <regex.h>

#define STEPS 32

#define servoPin D6
#define insideDHT11_Pin D7
#define outsideDHT11_Pin D5
#define DHTTYPE DHT11

Stepper stepper(STEPS, D1, D2, D3, D4);
DHT inside_dht(insideDHT11_Pin, DHTTYPE, 15);
DHT outside_dht(outsideDHT11_Pin, DHTTYPE, 15);

const char* ssid = "OnePlus 5T";
const char* pass = "12345678";
WiFiClient client;

unsigned long myTalkBackID = 47865;
const char* myTalkBackKey = "L9LRWG9JR106RF4P";

unsigned long channelID = 2005015;        //your TS channal
const char* APIKey = "ORCM6CR05BI287WU";  //your TS API
const char* server = "api.thingspeak.com";
float desiredTemp = 27.0;
float delayBetweenUpdates = 1000 * 20;  //in ms
bool boxIsOpen = false;

Servo s1;
void setup() {
  s1.attach(servoPin);
  stepper.setSpeed(200);

  inside_dht.begin();
  outside_dht.begin();

  WiFi.begin(ssid, pass);
  Serial.begin(115200);
}
void loop() {
  //read photoresistor
  //TODO: use this for something mabey in model.
  //int photoresistorValue = analogRead(A0);
  //Serial.println(photoresistorValue);

  //Check for updates:
  fetchUpdateFromTalkBack();

  //Temporary logic will have to be corrected when physical enviroment is done
  //Check if we need to move hot air in
  if (whenToUseTemperatureModel()) {
    if (!boxIsOpen) {
      openBox();
      boxIsOpen = true;
      delay(1000);
    }

    // If using stepper.step(200) then a soft WDT will occur hence we split up and use yield(). since yield resets internal timer
    while (whenToUseTemperatureModel()) {
      stepper.step(50);
      yield();
    }

    if (boxIsOpen) {
      closeBox();
      boxIsOpen = false;
      delay(1000);
    }
  }

  //Do measurements every 2s
  Serial.print("Temperature difference: ");
  Serial.print(differenceInTemperature());
  Serial.println(" C");
  writeToThingSpeak();
  delay(delayBetweenUpdates);
}


float differenceInTemperature() {
  return outside_dht.readTemperature() - inside_dht.readTemperature();
}

float differenceInHumidity() {
  return outside_dht.readHumidity() - inside_dht.readHumidity();
}

float differenceInHeatIndex() {
  float in_h = inside_dht.readHumidity();
  float in_tt = inside_dht.readTemperature();

  float out_h = outside_dht.readHumidity();
  float out_tt = outside_dht.readTemperature();

  float in_hic = outside_dht.computeHeatIndex(in_tt, in_h, false);
  float out_hic = outside_dht.computeHeatIndex(out_tt, out_h, false);

  return out_hic - in_hic;
}

//Might wanna split this up into multiple writes so we can do them when relevant.
void writeToThingSpeak() {
  ThingSpeak.begin(client);
  client.connect(server, 80);

  float in_h = inside_dht.readHumidity();
  float in_tt = inside_dht.readTemperature();

  float out_h = outside_dht.readHumidity();
  float out_tt = outside_dht.readTemperature();

  float in_hic = outside_dht.computeHeatIndex(in_tt, in_h, false);
  float out_hic = outside_dht.computeHeatIndex(out_tt, out_h, false);

  ThingSpeak.setField(1, analogRead(A0));
  ThingSpeak.setField(2, in_h);
  ThingSpeak.setField(3, in_hic);
  ThingSpeak.setField(4, in_tt);
  ThingSpeak.setField(5, out_h);
  ThingSpeak.setField(6, out_hic);
  ThingSpeak.setField(7, out_tt);

  ThingSpeak.writeFields(channelID, APIKey);

  client.stop();
}

void fetchUpdateFromTalkBack() {
  ThingSpeak.begin(client);

  int timeOutCounter = 0;
  // Connect or reconnect to Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(String(ssid));
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, pass);
      Serial.print(".");
      timeOutCounter++;
      delay(5000);
      if (timeOutCounter > 4) {
        return;
      }
    }
    Serial.println("\nConnected.");
  }

  // Create the TalkBack URI
  String tbURI = String("/talkbacks/") + String(myTalkBackID) + String("/commands/execute");

  // Create the message body for the POST out of the values
  String postMessage = String("api_key=") + String(myTalkBackKey);

  // Make a string for any commands that might be in the queue
  String newCommand = String();

  // Make the POST to ThingSpeak
  int x = httpPOST(tbURI, postMessage, newCommand);
  client.stop();

  // Check the result
  if (x == 200) {
    Serial.println("checking queue...");
    // Check for a command returned from TalkBack
    if (newCommand.length() != 0) {

      Serial.print("  Latest command from queue: ");
      Serial.println(newCommand);

      //Proccess command:
      char tab2[1024];
      strcpy(tab2, newCommand.c_str());
      int res = getTempFromString(tab2);

      Serial.println(tab2);

      if (res != -1) {
        Serial.print("THE RECIEVED TEMP IS: ");
        Serial.println(res);
        desiredTemp = res;
      }
    }

  } else {
    Serial.println("Problem checking queue. HTTP error code " + String(x));
  }
}

// General function to POST to ThingSpeak
int httpPOST(String uri, String postMessage, String& response) {

  bool connectSuccess = false;
  connectSuccess = client.connect("api.thingspeak.com", 80);

  if (!connectSuccess) {
    return -301;
  }

  postMessage += "&headers=false";

  String Headers = String("POST ") + uri + String(" HTTP/1.1\r\n") + String("Host: api.thingspeak.com\r\n") + String("Content-Type: application/x-www-form-urlencoded\r\n") + String("Connection: close\r\n") + String("Content-Length: ") + String(postMessage.length()) + String("\r\n\r\n");

  client.print(Headers);
  client.print(postMessage);

  long startWaitForResponseAt = millis();
  while (client.available() == 0 && millis() - startWaitForResponseAt < 5000) {
    delay(100);
  }

  if (client.available() == 0) {
    return -304;  // Didn't get server response in time
  }

  if (!client.find(const_cast<char*>("HTTP/1.1"))) {
    return -303;  // Couldn't parse response (didn't find HTTP/1.1)
  }

  int status = client.parseInt();
  if (status != 200) {
    return status;
  }

  if (!client.find(const_cast<char*>("\n\r\n"))) {
    return -303;
  }

  String tempString = String(client.readString());
  response = tempString;

  return status;
}

int getTempFromString(char* str) {
  char tempString[9];  // "SET_TEMP_" is 8 characters long
  int temp;
  if (strstr(str, "SET_TEMP_") != NULL) {
    strcpy(tempString, strstr(str, "SET_TEMP_") + 9);
    sscanf(tempString, "%d", &temp);
    return temp;
  } else {
    return -1;
  }
}

//TODO finish following function physical setup is complete:
void openBox() {
  s1.write(90);
}

void closeBox() {
  s1.write(0);
}

//TODO at the very least this should have some overwrite switch that can be enabled in thingspeak.
bool whenToUseTemperatureModel() {
  return differenceInTemperature() > 1.001 && inside_dht.readTemperature() < desiredTemp;
}
