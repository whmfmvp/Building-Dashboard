#include <WiFi.h>
#include <PubSubClient.h>
#include "DEV_Config.h"
#include "EPD.h"
#include "GUI_Paint.h"
#include "imagedata.h"
#include <stdlib.h>
#include <ESP32Servo.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Wi-Fi credentials
const char* ssid = "UCL_IoT";
const char* password = "Fyb6i5cPKn";

// MQTT broker parameters
const char* mqtt_server = "mqtt.cetools.org";
const int mqtt_port = 1884;
const char* mqtt_username = "student";
const char* mqtt_password = "ce2021-mqtt-forget-whale";
const char* mqtt_topic_base_electricity = "student/CASA0022/zczqhw8/Electricity/";
const char* mqtt_topic_base_carbon = "student/CASA0022/zczqhw8/Carbon_Emissions/";
const char* mqtt_topic_servo = "student/CASA0022/zczqhw8/Net_zero_ratio"; 
const char* mqtt_topic_pir = "student/CASA0022/zczqhw8/PIR"; 
const char* mqtt_topic_button = "student/CASA0022/zczqhw8/Button";


WiFiClient espClient;
PubSubClient client(espClient);
Servo servoMotor;

int yCoordsElectricity[8] = {400, 400, 400, 400, 400, 400, 400, 400};
int yCoordsCarbon[8] = {400, 400, 400, 400, 400, 400, 400, 400};
bool dataReceivedElectricity[8] = {false, false, false, false, false, false, false, false};
bool dataReceivedCarbon[8] = {false, false, false, false, false, false, false, false};

UBYTE *BlackImage, *RYImage;
bool updateDisplay = false;
int receivedCountElectricity = 0;
int receivedCountCarbon = 0;
bool displayBarChart = true; 

const int buttonPin = 4; 
const int servoPin = 18;
const int pirPin = 22;    
int targetServoAngle = 0; 
const int servoRestAngle = 0; 

unsigned long lastPIRCheck = 0;  
const long PIRInterval = 2000;   
bool servoActive = false;        
unsigned long servoActivationTime = 0;  

int servoTurnCount = 0;          
int buttonPressCount = 0;        
unsigned long lastMidnightCheck = 0; 

bool lastButtonState = HIGH;      
unsigned long lastDebounceTime = 0; 
const long debounceDelay = 50;    

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); 

void setup() {
  Serial.begin(115200);
  Serial.println("EPD_7IN5B_V2_test Demo");

  // Initialize Wi-Fi
  setup_wifi();

  // Initialize MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);

  // Initialize button
  pinMode(buttonPin, INPUT_PULLUP); 

  pinMode(pirPin, INPUT); 
  servoMotor.attach(servoPin); 

  // Initialize e-Paper display
  DEV_Module_Init();

  // Attach servo
  ESP32PWM::allocateTimer(0); 
  servoMotor.setPeriodHertz(50); 
  servoMotor.attach(servoPin, 500, 2400); 

    // Initialize NTP
  timeClient.begin();

  Serial.println("e-Paper Init and Clear...");
  EPD_7IN5B_V2_Init();
  EPD_7IN5B_V2_Clear();
  DEV_Delay_ms(500);

  // Create image cache
  UWORD Imagesize = ((EPD_7IN5B_V2_WIDTH % 8 == 0) ? (EPD_7IN5B_V2_WIDTH / 8) : (EPD_7IN5B_V2_WIDTH / 8 + 1)) * EPD_7IN5B_V2_HEIGHT;

  if ((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    Serial.println("Failed to apply for black memory...");
    while(1);
  }
  if ((RYImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    Serial.println("Failed to apply for red memory...");
    while(1);
  }
  Serial.println("NewImage: BlackImage and RYImage");
  Paint_NewImage(BlackImage, EPD_7IN5B_V2_WIDTH, EPD_7IN5B_V2_HEIGHT, 0, WHITE);
  Paint_NewImage(RYImage, EPD_7IN5B_V2_WIDTH, EPD_7IN5B_V2_HEIGHT, 0, WHITE);

  // Clear image cache
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_SelectImage(RYImage);
  Paint_Clear(WHITE);

  // Draw initial image
  drawImage(BlackImage, RYImage, displayBarChart);
}
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis = millis();
  timeClient.update(); 

  if (currentMillis - lastMidnightCheck >= 60000) {
    lastMidnightCheck = currentMillis;
    if (timeClient.getHours() == 0 && timeClient.getMinutes() == 0) {
      servoTurnCount = 0; 
      buttonPressCount = 0; 
      Serial.println("Servo turn count and button press count reset to 0 at midnight.");
    }
  }

  if (currentMillis - lastPIRCheck >= PIRInterval) {
    lastPIRCheck = currentMillis; 
    int pirState = digitalRead(pirPin);
    if (pirState == HIGH && !servoActive) {
      servoMotor.write(targetServoAngle);
      servoActivationTime = currentMillis;
      servoActive = true;
      servoTurnCount++; 
      client.publish(mqtt_topic_pir, String(servoTurnCount).c_str()); 
      Serial.print("Motion detected - Servo moving to target angle. Turn count: ");
      Serial.println(servoTurnCount);
    }
  }

  if (servoActive && (millis() - servoActivationTime >= 10000)) {
    servoMotor.write(servoRestAngle);
    servoActive = false;
    Serial.println("Servo moving back to rest position after delay.");
  }

  int reading = digitalRead(buttonPin);
  if (reading != lastButtonState) {
    lastDebounceTime = currentMillis; 
  }

  lastButtonState = reading; 

  if (updateDisplay) {
    drawImage(BlackImage, RYImage, displayBarChart);
    updateDisplay = false;
  }

  if (digitalRead(buttonPin) == HIGH) {
    delay(50); 
    while (digitalRead(buttonPin) == HIGH); 
    buttonPressCount++; 
    client.publish(mqtt_topic_button, String(buttonPressCount).c_str()); 
    Serial.print("Button pressed. Press count: ");
    Serial.println(buttonPressCount);
    displayBarChart = !displayBarChart; 
    drawImage(BlackImage, RYImage, displayBarChart);
    updateDisplay = true; 
  }
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client", mqtt_username, mqtt_password)) {
      Serial.println("connected");

      for (int i = 1; i <= 8; i++) {
        String topic = String(mqtt_topic_base_electricity) + "2024-" + (i < 10 ? "0" : "") + String(i);
        client.subscribe(topic.c_str());
        String topicCarbon = String(mqtt_topic_base_carbon) + "2024-" + (i < 10 ? "0" : "") + String(i);
        client.subscribe(topicCarbon.c_str());
      }
      client.subscribe(mqtt_topic_servo);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String message = String((char*)payload);
  float value = atof(message.c_str()); 
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(value);

  if (String(topic) == mqtt_topic_servo) {
      targetServoAngle = int(value * 100); 
      targetServoAngle = constrain(targetServoAngle, 0, 180); 
    }

  int index = atof(&topic[strlen(topic) - 2]) - 1;  
  
  if (strstr(topic, "Electricity")) {
    yCoordsElectricity[index] = map(value, 0, 48000, 400, 100);
    dataReceivedElectricity[index] = true;
    receivedCountElectricity++;
  } else if (strstr(topic, "Carbon_Emissions")) {
    yCoordsCarbon[index] = map(value, 0, 9000, 400, 100);
    dataReceivedCarbon[index] = true;
    receivedCountCarbon++;
  }

  if (receivedCountElectricity == 8 && receivedCountCarbon == 8) {
    updateDisplay = true;
  }
}


void drawImage(UBYTE *BlackImage, UBYTE *RYImage, bool displayBarChart) {
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawPoint(100, 400, BLACK, DOT_PIXEL_1X1, DOT_STYLE_DFT);
  Paint_DrawLine(100, 400, 100, 70, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(100, 400, 730, 400, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(110, 80, 100, 70, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(90, 80, 100, 70, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(720, 390, 730, 400, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(720, 410, 730, 400, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

  Paint_DrawString_EN(150, 410, "Jan", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(210, 410, "Feb", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(270, 410, "Mar", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(330, 410, "Apr", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(390, 410, "May", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(450, 410, "Jun", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(510, 410, "Jul", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(570, 410, "Aug", &Font16, WHITE, BLACK);


  if (displayBarChart) {
    Paint_DrawLine(100, 350, 700, 350, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(100, 300, 700, 300, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(100, 250, 700, 250, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(100, 200, 700, 200, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(100, 150, 700, 150, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(100, 100, 700, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawString_EN(100, 10, "Electricity consumption of UCL Student Centre in 2024", &Font16, WHITE, BLACK);
    Paint_DrawNum(50, 345, 8000, &Font16, BLACK, WHITE);
    Paint_DrawNum(40, 295, 16000, &Font16, BLACK, WHITE);
    Paint_DrawNum(40, 245, 24000, &Font16, BLACK, WHITE);
    Paint_DrawNum(40, 195, 32000, &Font16, BLACK, WHITE);
    Paint_DrawNum(40, 145, 40000, &Font16, BLACK, WHITE);
    Paint_DrawNum(40, 95, 48000, &Font16, BLACK, WHITE);

    Paint_DrawString_EN(90, 50, "kWh", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(735, 395, "Month", &Font16, WHITE, BLACK);
    // Draw bar chart
      Paint_SelectImage(RYImage);
      Paint_Clear(WHITE);
    for (int i = 0; i < 8; i++) {
      Paint_DrawRectangle(150 + i * 60, yCoordsElectricity[i], 180 + i * 60, 400, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    }
  } else {
    Paint_DrawString_EN(50, 10, "Carbon emissions of UCL Student Centre electricity usage in 2024", &Font16, WHITE, BLACK);

    Paint_DrawLine(100, 100, 700, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(100, 200, 700, 200, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(100, 300, 700, 300, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(165, 400, 165, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(225, 400, 225, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(285, 400, 285, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(345, 400, 345, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(405, 400, 405, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(465, 400, 465, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(525, 400, 525, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(585, 400, 585, 100, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawNum(50, 300, 3000, &Font16, BLACK, WHITE);
    Paint_DrawNum(50, 200, 6000, &Font16, BLACK, WHITE);
    Paint_DrawNum(50, 100, 9000, &Font16, BLACK, WHITE);

    Paint_DrawString_EN(90, 50, "kgCO2", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(735, 395, "Month", &Font16, WHITE, BLACK);
    // Draw line chart
      Paint_SelectImage(RYImage);
      Paint_Clear(WHITE);
    for (int i = 0; i < 8; i++) {
      Paint_DrawPoint(165 + i * 60, yCoordsCarbon[i], BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
      if (i < 7) {
        Paint_DrawLine(165 + i * 60, yCoordsCarbon[i], 165 + (i + 1) * 60, yCoordsCarbon[i + 1], BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      }
    }
  }

  Serial.println("EPD_Display");
  EPD_7IN5B_V2_Display(BlackImage, RYImage);
  DEV_Delay_ms(2000);
}
