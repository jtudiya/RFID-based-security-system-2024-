#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal.h>
#include <ESP32Servo.h>

// Wi-Fi credentials
const char* ssid = "MyWiFi";
const char* password = "MyPassword";

// MQTT server credentials
const char* mqttServer = "mqtt-dashboard.com";
const int mqttPort = 1883;
const char* mqttTopic = "read_data/rfid";
const char* mqttCommandTopic = "write_data/rfid";

// MQTT client
WiFiClient espClient;
PubSubClient client(espClient);

// Pins for LCD
LiquidCrystal lcd(14, 27, 26, 25, 33, 32);

// Pins for RFID-RC522
#define SS_PIN 5
#define RST_PIN 4

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

// Servo motor
Servo myservo;
int servoPin = 15;

// IR Sensors
const int irSensor1Pin = 16;  // IR Sensor 1
const int irSensor2Pin = 17;  // IR Sensor 2

// Debounce variables for IR sensors
bool irSensor1State = LOW;
bool lastIrSensor1State = LOW;
bool irSensor2State = LOW;
bool lastIrSensor2State = LOW;

// Known UIDs (replace with the UIDs of your valid cards)
byte knownUIDs[][4] = {
  { 0x99, 0xE3, 0x4E, 0x68 },
  // Add more known UIDs here
};

void setup() {
  // Initialize serial communications with the PC
  Serial.begin(9600);
  while (!Serial);

  // Initialize the LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Scan your card");

  // Initialize SPI bus
  SPI.begin();
  // Initialize RFID reader
  mfrc522.PCD_Init();

  // Initialize servo
  myservo.attach(servoPin);
  myservo.write(0);  // Start with the servo at 0 degrees

  // Initialize IR sensors
  pinMode(irSensor1Pin, INPUT);
  pinMode(irSensor2Pin, INPUT);

  // Connect to Wi-Fi
  setupWifi();

  // Setup MQTT
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  // Allow time for setup
  delay(1000);
}

void loop() {
  // Ensure MQTT connection
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Read IR sensors
  irSensor1State = digitalRead(irSensor1Pin);
  irSensor2State = digitalRead(irSensor2Pin);

  // Check IR sensor 1
  if (irSensor1State != lastIrSensor1State) {
    lastIrSensor1State = irSensor1State;
    if (irSensor1State == HIGH) {
      Serial.println("Car arrived");
      lcd.clear();
      lcd.print("Car arrived");
    }
  }

  // Check IR sensor 2
  if (irSensor2State != lastIrSensor2State) {
    lastIrSensor2State = irSensor2State;
    if (irSensor2State == HIGH) {
      Serial.println("Car gone");
      lcd.clear();
      lcd.print("Car gone");
      myservo.write(0);  // Move servo back to 0 degrees
    }
  }

  // Check for new cards
  if (irSensor1State == HIGH && !mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Display card UID
  Serial.print("UID tag: ");
  String content = "";
  byte letter;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
    content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
    content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  Serial.println();

  // Check if the scanned UID matches any known UID
  if (isValidCard()) {
    lcd.clear();
    lcd.print("Card verified");
    lcd.setCursor(0, 1);
    lcd.print("Welcome");
    myservo.write(90);  // Move servo to 90 degrees
    delay(6000);       // Wait for 10 seconds
    myservo.write(0);   // Move servo back to 0 degrees
    delay(1000);

    // Publish verification message to MQTT
    client.publish(mqttTopic, "Card verified: Welcome");
  } else {
    lcd.clear();
    lcd.print("Access Denied");
    lcd.setCursor(0, 1);
    lcd.print("Try Again");
    delay(2000);

    // Publish denial message to MQTT
    client.publish(mqttTopic, "Access Denied: Try Again");
  }

  lcd.clear();
  lcd.print("Scan your card");

  // Halt PICC
  mfrc522.PICC_HaltA();
  // Stop encryption on PCD
  mfrc522.PCD_StopCrypto1();
}

bool isValidCard() {
  // Compare the UID of the scanned card with the known UIDs
  for (int i = 0; i < sizeof(knownUIDs) / sizeof(knownUIDs[0]); i++) {
    if (memcmp(mfrc522.uid.uidByte, knownUIDs[i], mfrc522.uid.size) == 0) {
      return true;
    }
  }
  return false;
}

void setupWifi() {
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
    if (client.connect("ESP32Client__data__")) {
      Serial.println("connected");
      client.subscribe(mqttCommandTopic);  // Subscribe to command topic

      // Publish a connected message to MQTT
      client.publish(mqttTopic, "ESP32 Connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Handle incoming MQTT messages
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(message);

  // If the message is "OPEN", move the servo to 90 degrees for 6 seconds
  if (message.equalsIgnoreCase("OPEN")) {
    myservo.write(90);
    lcd.clear();
    lcd.print("Command: OPEN");
    delay(6000);
    myservo.write(0);
  }
  // If the message is "CLOSE", move the servo back to 0 degrees
  else if (message.equalsIgnoreCase("CLOSE")) {
    myservo.write(0);
    lcd.clear();
    lcd.print("Command: CLOSE");
  } else {
    // Display the message on the LCD
    lcd.clear();
    lcd.print("MQTT Message:");
    lcd.setCursor(0, 1);
    lcd.print(message);
  }

  // Publish the received message to confirm
  client.publish(mqttTopic, message.c_str());
}
