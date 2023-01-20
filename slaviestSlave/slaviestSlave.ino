/*
* Script resposible for the indoor temperature and humidity measurements aswell as displaying on the LCD screen.
* Emil Slente Liljegren (s174036) was mainly reposible for creating this script.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <espnow.h>


#define insideDHT11_Pin D4
#define DHTTYPE DHT11
LiquidCrystal_I2C lcd(0x27, 16, 2);

uint8_t broadcastAddress[] = {0xC8, 0x2B, 0x96, 0x09, 0x0E, 0x47};
typedef struct struct_message {
  float humidity;
  float temp;
} struct_message;

struct_message dataToSend;
struct_message dataToRecieve;

int dataSendTries = 0;

DHT inside_dht(insideDHT11_Pin, DHTTYPE, 15);

int readDelay = 1500;
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 2, NULL, 0);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  lcd.init();  //display initialization
  lcd.clear();
  inside_dht.begin();
}

void loop() {
  // put your main code here, to run repeatedly:

  // read voltage from temperature sensor
  //  int sensorVal = analogRead(A0);
  dataToSend.humidity = inside_dht.readHumidity();
  dataToSend.temp = inside_dht.readTemperature();

  esp_now_send(broadcastAddress, (uint8_t *)&dataToSend, sizeof(dataToSend));


  //convert analog reading (0-1023) to voltage (0-5.0 V)
  //Serial.print(in_tt);
  //Serial.println("°");
  // Serial.write(176)
  lcd.clear();



  printToLCD();
  delay(readDelay);
}

void printToLCD() {
  lcd.backlight();      // activate the backlight
  lcd.setCursor(0, 0);  // place cursor at first line
  lcd.print("In temp: ");
  lcd.print(dataToSend.temp);
  lcd.print("C");
  lcd.setCursor(0, 1);  // place cursor at second line
  lcd.print("Box temp: ");
  lcd.print(dataToRecieve.temp);
  lcd.print("C");
}


void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Last Packet Send Status: ");
  if (sendStatus == 0) {
    Serial.println("Delivery success");
    dataSendTries = 0;
  } else {
    Serial.println("Delivery fail");
    dataSendTries = 0;
  }
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&dataToRecieve, incomingData, sizeof(dataToRecieve));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("temp: ");
  Serial.println(dataToRecieve.temp);
  Serial.print("Humidity: ");
  Serial.println(dataToRecieve.humidity);
  Serial.println();
}
