
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <espnow.h>


#define insideDHT11_Pin D4
#define DHTTYPE DHT11
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 2C:3A:E8:38:16:6C

uint8_t broadcastAddress[] = { 0xC8, 0x2B, 0x96, 0x09, 0x0E, 0x47 };
typedef struct struct_message {
  float humidity;
  float temp;
} struct_message;

struct_message myData;


DHT inside_dht(insideDHT11_Pin, DHTTYPE, 15);

int readDelay = 1000;
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
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(OnDataSent);

  // Register peer
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
  lcd.init();  //display initialization
  lcd.clear();
  inside_dht.begin();
}

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Last Packet Send Status: ");
  if (sendStatus == 0) {
    Serial.println("Delivery success");
  } else {
    Serial.println("Delivery fail");
  }
}

void loop() {
  // put your main code here, to run repeatedly:

  // read voltage from temperature sensor
  //  int sensorVal = analogRead(A0);
  float in_h = inside_dht.readHumidity();
  float in_tt = inside_dht.readTemperature();

  myData.humidity = inside_dht.readHumidity();
  myData.temp = inside_dht.readTemperature();

  esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

  //convert analog reading (0-1023) to voltage (0-5.0 V)
  Serial.print(in_tt);
  Serial.println("°");
  // Serial.write(176)
  lcd.clear();



  lcd.backlight();      // activate the backlight
  lcd.setCursor(0, 0);  // place cursor at first line

  lcd.print(in_tt);
  lcd.print("C");
  delay(readDelay);
}
