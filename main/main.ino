/*
* The main script which runs in the box
* This script is responsible for handling all the logic
* including the temperature model, all physical actuation and communication with thingspeak
* both sending and talkback.
*
* Each method will have a name signifying who was mainly resposible for it however we worked in cooporation on almost all of the code.'
* If a method does not have a name it is because it is either heavily inspired by something found only or straight up a copy.
*/


#include <Servo.h>  // servo library
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <ThingSpeak.h>
#include <stdio.h>
#include <stdlib.h>
#include <espnow.h>

#define transistorPin D4
#define servoPin D6
#define insideDHT11_Pin D7
#define outsideDHT11_Pin D5
#define DHTTYPE DHT11
#define photoresistorPin A0

//The different states in the statemachine
typedef enum {
  SLEEP,   //go back to sleep and increment important counters
  MOVE,    //activate motor and move air into apartment
  OPEN,    //open  the hatch
  CLOSE,   //close the hatch
  SEND,    //send data to thingspeak
  RECIEVE  //recieve data from thingspeak a.i. empty queue.
} States;



//The sturct
typedef struct struct_message {
  float humidity;
  float temp;
} struct_message;

struct_message myData;
struct_message outgoing;
struct_message incoming;



// We start in SLEEP state.
int currentState = SLEEP;
int currentTimeIncrement = 0;
bool overWriteActivated = false;

DHT outside_dht(outsideDHT11_Pin, DHTTYPE, 15);

// Replace these with what is currently being used.
const char* ssid = "AndroidAP";
const char* pass = "12345678";
WiFiClient client;

unsigned long myTalkBackID = 47856;
const char* myTalkBackKey = "DOS2ZPZ7O5HLWR20";

unsigned long channelID = 2005015;        //your TS channal
const char* APIKey = "ORCM6CR05BI287WU";  //your TS API
const char* server = "api.thingspeak.com";

float desiredTemp = 27.0;              //Can be set through thingspeak talkback
float delayBetweenUpdates = 1000 * 3;  //in ms
bool boxIsOpen = false;                //box is initially closed

uint8_t peerAddress[] = { 0x2C, 0x3A, 0xE8, 0x38, 0x16, 0x6C };  //MAC adress of the other ESP

Servo s1;

// Responsible Victor (s203951)
void setup() {
  s1.attach(servoPin);

  inside_dht.begin();
  outside_dht.begin();

  esp_now_init();

  pinMode(transistorPin, OUTPUT);

  // // 2C:3A:E8:38:16:6C

  // set peer adresse, self role, and what to do when sending and recieving
  esp_now_add_peer(peerAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  WiFi.begin(ssid, pass);
  Serial.begin(115200);

  // Make sure motor is turned off initially
  digitalWrite(transistorPin, HIGH);
  closeBox();
}

// Responsible Thor Dueholm (s194589)
void loop() {
  // State machine
  switch (currentState) {
    case SLEEP:
      delay(delayBetweenUpdates);

      // Check if ready to move air
      if (whenToUseTemperatureModel()) {
        currentState = OPEN;
        break;
      } else if (currentTimeIncrement % 20 == 0) {
        currentState = RECIEVE;
        break;
      } else if (currentTimeIncrement % 9 == 0) {
        currentState = SEND;
        break;
      }

      break;
    case SEND:  //Both writing to thingspeak and esp_now communication
      writeToThingSpeak();
      outgoing.temp = outside_dht.readTemperature();
      outgoing.humidity = outside_dht.readHumidity();
      esp_now_send(peerAddress, (uint8_t*)&outgoing, sizeof(outgoing));
      currentState = SLEEP;
      break;
    case RECIEVE:
      fetchUpdateFromTalkBack();
      currentState = SLEEP;
      break;
    case OPEN:
      openBox();
      currentState = MOVE;
      break;
    case CLOSE:
      closeBox();
      overWriteActivated = false;
      currentState = SLEEP;
      break;
    case MOVE:
      // Note LOW is on, HIGH is off.
      digitalWrite(transistorPin, LOW);
      delay(500);
      //Only check sometimes
      if (currentTimeIncrement % 10 == 0) {
        digitalWrite(transistorPin, HIGH);  // We have to turn motor of or we get NaN reading from DHT11 sensors (don't know why).
        delay(2000);
        if (!whenToUseTemperatureModel()) {
          currentState = CLOSE;
          break;
        }
        digitalWrite(transistorPin, LOW);
      }

      break;
    default:
      Serial.println("your not suppose to be here");
  }

  printDebugging();

  // Incrementing time and thresholding at 100 we do this just so we will not ever reach interger cap.
  currentTimeIncrement = (currentTimeIncrement + 1) % 100;
}

// Responsible Thor Dueholm (s194589)
// Prints only for debugging
void printDebugging() {
  Serial.print("The current state is: ");
  Serial.print(getStateName(currentState));
  Serial.println();

  Serial.print("time spent is: ");
  Serial.print(currentTimeIncrement);
  Serial.println();

  Serial.print("Temperature difference: ");
  Serial.print(differenceInTemperature());
  Serial.println(" C");
}


// Message on sending data
// Copied from https://randomnerdtutorials.com/esp-now-two-way-communication-esp8266-nodemcu/
void OnDataSent(uint8_t* mac_addr, uint8_t sendStatus) {
  Serial.print("Last Packet Send Status: ");
  if (sendStatus == 0) {
    Serial.println("Delivery success");
  } else {
    Serial.println("Delivery fail");
  }
}

// Responsible Mikkel (s194601)
// Triggerd when recieving data.
void OnDataRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("temp: ");
  Serial.println(myData.temp);
  Serial.print("Humidity: ");
  Serial.println(myData.humidity);
  Serial.println();
}

// Responsible Mikkel (s194601)
float differenceInTemperature() {
  return outside_dht.readTemperature() - inside_dht.readTemperature();
}

// Responsible Mikkel (s194601)
float differenceInHumidity() {
  return outside_dht.readHumidity() - inside_dht.readHumidity();
}

// Responsible Mikkel (s194601)
float differenceInHeatIndex() {
  float in_h = inside_dht.readHumidity();
  float in_tt = inside_dht.readTemperature();

  float out_h = outside_dht.readHumidity();
  float out_tt = outside_dht.readTemperature();

  float in_hic = outside_dht.computeHeatIndex(in_tt, in_h, false);
  float out_hic = outside_dht.computeHeatIndex(out_tt, out_h, false);

  return out_hic - in_hic;
}

// Responsible Mikkel (s194601)
// Write all relevant data to thingspeak.
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


// All talkback logic was heavily inspired by: https://se.mathworks.com/help/thingspeak/control-a-light-with-talkback-and-arduino.html
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

      // Cull beginning of string since it contains \n.
      char substring[1000];
      int len = newCommand.length();
      int pos = 4;

      for (int c = 0; c < len; c++) {

        substring[c] = tab2[pos + c - 1];
      }

      HandelStringInput(substring);
    }

  } else {
    Serial.println("Problem checking queue. HTTP error code " + String(x));
  }
}

// Responsible Thor Dueholm (s194589)
// What we recieve from the talkback
void HandelStringInput(char* input) {
  if (compareStartOfString("SET_TEMP_", input)) {
    Serial.println(getTempFromString(input));
    int res = getTempFromString(input);
    if (res != -1) {
      Serial.print("THE RECIEVED TEMP IS: ");
      Serial.println(res);
      desiredTemp = res;
    }
  } else if (compareStartOfString("OVERWRITE", input)) {
    overWriteActivated = true;
    Serial.println("OVERWRITE INITIATET");
  } else {  //unrecognized command
    Serial.println("unrecognized command");
  }
}



// Taken from: https://stackoverflow.com/questions/4770985/how-to-check-if-a-string-starts-with-another-string-in-c
bool compareStartOfString(const char* pre, const char* str) {
  return strncmp(pre, str, strlen(pre)) == 0;
}

// General function to POST to ThingSpeak From https://se.mathworks.com/help/thingspeak/control-a-light-with-talkback-and-arduino.html
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

// Responsible Victor (s203951)
// Gets the integer X from the string SET_TEMP_X
int getTempFromString(char* str) {
  char tempString[9];  // "SET_TEMP_" is 9 characters long
  int temp;
  if (strstr(str, "SET_TEMP_") != NULL) {
    strcpy(tempString, strstr(str, "SET_TEMP_") + 9);
    sscanf(tempString, "%d", &temp);
    return temp;
  } else {
    return -1;
  }
}

// Responsible Thor (s194589)
// Only used for debugging through serial
const char* getStateName(int state) {
  switch (state) {
    case SLEEP:
      return "SLEEP";
    case SEND:
      return "SEND";
    case RECIEVE:
      return "RECIEVE";
    case OPEN:
      return "OPEN";
    case CLOSE:
      return "CLOSE";
    case MOVE:
      return "MOVE";
    default:
      return "your not suppose to be here";
  }
}

// Responsible Victor (s203951)
void openBox() {
  s1.write(0);
  boxIsOpen = true;
  delay(3000);
}

// Responsible Victor (s203951)
void closeBox() {
  s1.write(180);
  boxIsOpen = false;
  delay(3000);
}


// Responsible Victor (s203951)
// The model responsible for deciding when to turn on the motor.
bool whenToUseTemperatureModel() {
  // overwrite logic
  if (overWriteActivated) {
    overWriteActivated = false;
    return true;
  }

  int photoresistorValue = analogRead(photoresistorPin);
  // If light is low (night) we accept lower threshold for hot air.
  if (photoresistorValue < 30) {
    return (differenceInTemperature() > 2 && inside_dht.readTemperature() < desiredTemp && ((inside_dht.readHumidity() > 45 && outside_dht.readHumidity() < 45) || (inside_dht.readHumidity() < 30 && outside_dht.readHumidity() > 30)));
  }
  return (differenceInTemperature() > 5 && inside_dht.readTemperature() < desiredTemp && ((inside_dht.readHumidity() > 45 && outside_dht.readHumidity() < 45) || (inside_dht.readHumidity() < 30 && outside_dht.readHumidity() > 30)));
}
