#include <Servo.h>  // servo library
#include <Stepper.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <ThingSpeak.h>

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
unsigned long channelID = 2005015;        //your TS channal
const char* APIKey = "ORCM6CR05BI287WU";  //your TS API
const char* server = "api.thingspeak.com";
float desiredTemp = 27.0;
float delayBetweenUpdates = 1000 * 20; //in ms
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

  //Temporary logic will have to be corrected when physical enviroment is done
  //Check if we need to move hot air in
  if (whenToUseTemperatureModel()) {
    if (!boxIsOpen) {
      openBox();
      boxIsOpen = true;
      delay(1000);
    }

    // If using stepper.step(200) then a soft WDT will occur hence we split up and use yield(). since yield resets internal timer
    for (int i = 0; i < 4; i++) {
      stepper.step(50);
      yield();
    }

  } else if (boxIsOpen) {
    closeBox();
    boxIsOpen = false;
    delay(1000);
  }

  //Do measurements every 2s
  Serial.print("Temperature difference: ");
  Serial.print(differenceInTemperature());
  Serial.println(" C");
  delay(delayBetweenUpdates);

  writeToThingSpeak();
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


//TODO finish following function physical setup is complete:
void openBox() {
  s1.write(90);
}

void closeBox() {
  s1.write(0);
}

//TODO at the very least this should have some overwrite switch that can be enabled in thingspeak.
bool whenToUseTemperatureModel() {
  return differenceInTemperature() > 2.2 && inside_dht.readTemperature() < desiredTemp;
}
