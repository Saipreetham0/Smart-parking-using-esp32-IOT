#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

unsigned long timerDelay = 1000;
// #define IR_SENSOR_PIN_1 36 //
// #define IR_SENSOR_PIN_2 39 //
// #define IR_SENSOR_PIN_3 34 //
// #define IR_SENSOR_PIN_4 35 //
// IR sensor pins
#define IR_SENSOR_PIN_1 25 //
#define IR_SENSOR_PIN_2 36 //
#define IR_SENSOR_PIN_3 39 //
#define IR_SENSOR_PIN_4 34 //
#define IR_SENSOR_PIN_5 18
#define IR_SENSOR_PIN_6 5
#define IR_SENSOR_PIN_7 4
#define IR_SENSOR_PIN_8 13

// Define LED pins
// #define LED_PIN_1 12
// #define LED_PIN_2 14
// #define LED_PIN_3 19
// #define LED_PIN_4 21
// #define LED_PIN_5 22
// #define LED_PIN_6 23
// #define LED_PIN_7 26
// #define LED_PIN_8 27

// Define LED pins
const int LED_PIN_1 = 12;
const int LED_PIN_2 = 14;
const int LED_PIN_3 = 19;
const int LED_PIN_4 = 21;
const int LED_PIN_5 = 22;
const int LED_PIN_6 = 23;
const int LED_PIN_7 = 26;
const int LED_PIN_8 = 27;

// Insert your network credentials
#define WIFI_SSID "KSP"
#define WIFI_PASSWORD "9550421866"

// Insert Firebase project API Key
#define API_KEY "AIzaSyCQI6SfVHGxwLGwAvxojy4JSsdTfFE9zYg"

#define USER_EMAIL "admin@saipreetham.com"
#define USER_PASSWORD "123456789"
// Insert RTDB URLefine the RTDB URL */
#define DATABASE_URL "smart-parking-a7052-default-rtdb.firebaseio.com"

// Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseData stream;
FirebaseConfig config;

// Variables to save database paths
String listenerPath = "board1/outputs/digital/";

// Variable to save USER UID
String uid;

unsigned long sendDataPrevMillis = 0;
int count = 0;
FirebaseJson json;
// Database main path (to be updated in setup with the user UID)
String databasePath;

// Callback function that runs on database changes
void streamCallback(FirebaseStream data)
{
  Serial.printf("stream path, %s\nevent path, %s\ndata type, %s\nevent type, %s\n\n",
                data.streamPath().c_str(),
                data.dataPath().c_str(),
                data.dataType().c_str(),
                data.eventType().c_str());
  printResult(data); // see addons/RTDBHelper.h
  Serial.println();

  // Get the path that triggered the function
  String streamPath = String(data.dataPath());

  // if the data returned is an integer, there was a change on the GPIO state on the following path /{gpio_number}
  if (data.dataTypeEnum() == fb_esp_rtdb_data_type_integer)
  {
    String gpio = streamPath.substring(1);
    int state = data.intData();
    Serial.print("GPIO: ");
    Serial.println(gpio);
    Serial.print("STATE: ");
    Serial.println(state);
    digitalWrite(gpio.toInt(), state);
  }

  /* When it first runs, it is triggered on the root (/) path and returns a JSON with all keys
  and values of that path. So, we can get all values from the database and updated the GPIO states*/
  if (data.dataTypeEnum() == fb_esp_rtdb_data_type_json)
  {
    FirebaseJson json = data.to<FirebaseJson>();

    // To iterate all values in Json object
    size_t count = json.iteratorBegin();
    Serial.println("\n---------");
    for (size_t i = 0; i < count; i++)
    {
      FirebaseJson::IteratorValue value = json.valueAt(i);
      int gpio = value.key.toInt();
      int state = value.value.toInt();
      Serial.print("STATE: ");
      Serial.println(state);
      Serial.print("GPIO:");
      Serial.println(gpio);
      digitalWrite(gpio, state);
      Serial.printf("Name: %s, Value: %s, Type: %s\n", value.key.c_str(), value.value.c_str(), value.type == FirebaseJson::JSON_OBJECT ? "object" : "array");
    }
    Serial.println();
    json.iteratorEnd(); // required for free the used memory in iteration (node data collection)
  }

  // This is the size of stream payload received (current and max value)
  // Max payload size is the payload size under the stream path since the stream connected
  // and read once and will not update until stream reconnection takes place.
  // This max value will be zero as no payload received in case of ESP8266 which
  // BearSSL reserved Rx buffer size is less than the actual stream payload.
  Serial.printf("Received stream payload size: %d (Max. %d)\n\n", data.payloadLength(), data.maxPayloadLength());
}

void streamTimeoutCallback(bool timeout)
{
  if (timeout)
    Serial.println("stream timeout, resuming...\n");
  if (!stream.httpConnected())
    Serial.printf("error code: %d, reason: %s\n\n", stream.httpCode(), stream.errorReason().c_str());
}

void setup()
{
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  Firebase.reconnectWiFi(true);

  fbdo.setResponseSize(4096);

  // Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  // Assign the maximum retry of token generation
  config.max_token_generation_retry = 5;

  // Initialize the library with the Firebase authen and config
  Firebase.begin(&config, &auth);

  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "")
  {
    Serial.print('.');
    delay(1000);
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);

  // Update database path
  databasePath = "/parkingstatus";

  // Streaming (whenever data changes on a path)
  // Begin stream on a database path --> board1/outputs/digital
  if (!Firebase.RTDB.beginStream(&stream, listenerPath.c_str()))
    Serial.printf("stream begin error, %s\n\n", stream.errorReason().c_str());

  // Assign a calback function to run when it detects changes on the database
  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);

  delay(2000);

  pinMode(IR_SENSOR_PIN_1, INPUT);
  pinMode(IR_SENSOR_PIN_2, INPUT);
  pinMode(IR_SENSOR_PIN_3, INPUT);
  pinMode(IR_SENSOR_PIN_4, INPUT);
  pinMode(IR_SENSOR_PIN_5, INPUT);
  pinMode(IR_SENSOR_PIN_6, INPUT);
  pinMode(IR_SENSOR_PIN_7, INPUT);
  pinMode(IR_SENSOR_PIN_8, INPUT);

  pinMode(LED_PIN_1, OUTPUT);
  pinMode(LED_PIN_2, OUTPUT);
  pinMode(LED_PIN_3, OUTPUT);
  pinMode(LED_PIN_4, OUTPUT);
  pinMode(LED_PIN_5, OUTPUT);
  pinMode(LED_PIN_6, OUTPUT);
  pinMode(LED_PIN_7, OUTPUT);
  pinMode(LED_PIN_8, OUTPUT);
}
void loop()
{

  if (Firebase.isTokenExpired())
  {
    Firebase.refreshToken(&config);
    Serial.println("Refresh token");
  }
  // Send new readings to database
  if (Firebase.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0))
  {
    sendDataPrevMillis = millis();

    // Construct the database path
    String databasePath = "/parkingstatus";

    // Read IR sensor values
    int irSensorValues[8];
    irSensorValues[0] = digitalRead(IR_SENSOR_PIN_1);
    irSensorValues[1] = digitalRead(IR_SENSOR_PIN_2);
    irSensorValues[2] = digitalRead(IR_SENSOR_PIN_3);
    irSensorValues[3] = digitalRead(IR_SENSOR_PIN_4);
    irSensorValues[4] = digitalRead(IR_SENSOR_PIN_5);
    irSensorValues[5] = digitalRead(IR_SENSOR_PIN_6);
    irSensorValues[6] = digitalRead(IR_SENSOR_PIN_7);
    irSensorValues[7] = digitalRead(IR_SENSOR_PIN_8);

    // Construct JSON payload

    for (int i = 0; i < 8; i++)
    {
      // String key = "IR_Sensor_" + String(i + 1);
      // json.set(key.c_str(), irSensorValues[i]);

      String key = "IR_Sensor_" + String(i + 1);
      json.set(key.c_str(), irSensorValues[i]);

      // Print sensor value to serial monitor
      Serial.print(key);
      Serial.print(": ");
      Serial.println(irSensorValues[i]);
    }

    // Update Firebase database
    Serial.printf("Set JSON... %s\n", Firebase.RTDB.setJSON(&fbdo, databasePath.c_str(), &json) ? "OK" : fbdo.errorReason().c_str());
  }
}