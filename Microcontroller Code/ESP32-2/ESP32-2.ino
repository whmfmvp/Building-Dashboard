#include <WiFi.h>
#include <PubSubClient.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 2

#define CLK_PIN   21  
#define DATA_PIN  23  
#define CS_PIN1    22  
#define CS_PIN2    4   


MD_MAX72XX mx1(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN1, 1);
MD_MAX72XX mx2(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN2, 1);

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";
const char* mqtt_server = "YOUR_MQTT_SERVER";
const int mqtt_port = 1884;
const char* mqtt_username = "-";
const char* mqtt_password = "-";
const char* mqtt_topic_base = "student/CASA0022/zczqhw8/Dot_matrix";

WiFiClient espClient;
PubSubClient client(espClient);
int integerToShow = 0;   
int decimalToShow = 0;   

void setup() {
  Serial.begin(115200);
  setupWiFi();
  setupMQTT();
  mx1.begin();
  mx1.control(MD_MAX72XX::INTENSITY, 8);
  mx1.clear();
  mx2.begin();
  mx2.control(MD_MAX72XX::INTENSITY, 8);
  mx2.clear();
}


void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  displayTwoDigits(integerToShow, 0);  
  displayTwoDigits(decimalToShow, 1);  
  delay(2000);  
}

void setupWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");
}

void setupMQTT() {
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  reconnectMQTT();
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect("ESP32Client", mqtt_username, mqtt_password)) {
      Serial.println("Connected");
      client.subscribe(mqtt_topic_base);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received [");
  Serial.print(topic);
  Serial.print("] ");
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  float num = message.toFloat();
  integerToShow = int(num);           
  decimalToShow = int(num * 100) % 100;  
  Serial.print("Parsed decimal part: ");
  Serial.println(decimalToShow);
}

void displayTwoDigits(int number, int screen) {
  MD_MAX72XX* mx = (screen == 0) ? &mx1 : &mx2;  
  if (number < 0 || number > 99) return;  

  int tens = number / 10;  
  int units = number % 10;  

  mx->clear();
  displayDigit(*mx, tens, 0);  
  displayDigit(*mx, units, 4);  
}

void displayDigit(MD_MAX72XX& mx, int digit, int offset) {
  const uint8_t digits[10][4] = {
    {0x3E, 0x45, 0x49, 0x3E}, 
    {0x00, 0x42, 0x7F, 0x40}, 
    {0x72, 0x49, 0x49, 0x46}, 
    {0x21, 0x49, 0x49, 0x36}, 
    {0x0F, 0x08, 0x08, 0x7F}, 
    {0x4F, 0x49, 0x49, 0x31}, 
    {0x3E, 0x49, 0x49, 0x32}, 
    {0x01, 0x01, 0x79, 0x07}, 
    {0x36, 0x49, 0x49, 0x36}, 
    {0x26, 0x49, 0x49, 0x3E}  
  };

  for (int i = 0; i < 4; i++) {
    mx.setColumn(7 - (offset + i), digits[digit][i]);  
  }
}
