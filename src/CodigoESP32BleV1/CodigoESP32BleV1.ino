/**
 * @file main.cpp
 * @brief BLE-based NeoPixel controller for an 8x8 (64-pixel) matrix.
 *        Automatically restarts the ESP32 upon BLE client disconnection.
 *        Developer: Alexis Mora
 *
 * This code implements a BLE peripheral that allows a central device (e.g., smartphone) to:
 *  - Send commands to control a strip of 64 NeoPixels (WS2812/WS2812B).
 *  - Receive periodic notifications with sensor data (digital input and analog reading).
 *  - Restart the ESP32 automatically if the BLE client disconnects after having been connected.
 *
 * Command format examples:
 *   1) "*brightness,red,green,blue." 
 *       Sets ALL pixels to the same color & brightness (global).
 *       e.g., "*100,255,0,0." -> all red at brightness=100
 *
 *   2) "_index,brightness,red,green,blue."
 *       Sets ONE pixel at 'index' to the given color.
 *       brightness can be used or ignored for global brightness changes.
 *       e.g., "_3,120,0,255,128." -> LED #3 with brightness=120, color=(0,255,128)
 *       e.g., "_10,0,255,255,0."  -> LED #10 with brightness=0 (ignored?), color=(255,255,0)
 *
 *   3) "!."
 *       Clears (turns off) all pixels.
 *
 * The code also reads a digital sensor at pin 23 and an analog sensor at pin 34,
 * sending their values every 500ms via BLE notifications in the format "D:x,A:y".
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino.h> // Typically included by default, but explicitly stated.

// ---------------------- Pin and NeoPixel Configuration ----------------------
#define NEOPIXEL_PIN       32   // GPIO pin connected to the NeoPixel data line
#define NUM_PIXELS         64   // 8x8 matrix -> 64 NeoPixels

// Sensor pins
#define DIGITAL_SENSOR_PIN 23   // Example digital sensor (e.g., push button)
#define ANALOG_SENSOR_PIN  34   // Example analog sensor (e.g., potentiometer)

// -------------------------- BLE UUID Definitions ----------------------------
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ---------------------- Global Variables and Objects ------------------------
BLEServer*          pServer         = nullptr;
BLECharacteristic*  pCharacteristic = nullptr;
bool                deviceConnected = false;

// Create a NeoPixel strip object (8x8 matrix or any shape, total 64 LEDs)
Adafruit_NeoPixel strip(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Optional variables for debugging sensor changes
int previousDigitalValue = -1;
int previousAnalogValue  = -1;

/**
 * @class MyServerCallbacks
 * @brief Handles BLE server events, including connection and disconnection.
 */
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    BLEDevice::stopAdvertising();  // Stop advertising once a device connects
    Serial.println("BLE client connected.");
  }

  void onDisconnect(BLEServer* pServer) override {
    // If we were previously connected, restart the ESP32 on disconnection
    if (deviceConnected) {
      Serial.println("BLE client disconnected. Restarting ESP32...");
      ESP.restart(); // This function reboots the ESP32
    }
    deviceConnected = false;
  }
};

/**
 * @class MyCharacteristicCallbacks
 * @brief Handles BLE characteristic write events (commands from the client).
 */
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    // Retrieve the value written to this characteristic
    std::string rxValue = pCharacteristic->getValue();
    if (!rxValue.empty()) {
      Serial.print("Received BLE command: ");
      Serial.println(rxValue.c_str());

      // Convert to Arduino String for easier parsing
      String command = String(rxValue.c_str());

      // If the command ends with '.', remove it to simplify parsing
      if (command.endsWith(".")) {
        command.remove(command.length() - 1); // remove the trailing '.'
      }

      // Decide the action based on the first character
      if (command.startsWith("*")) {
        /**
         * Format: "*brightness,red,green,blue"
         * Example: "*100,255,0,0"
         * Sets ALL LEDs to the same color and brightness (global).
         */
        command.remove(0, 1); // Remove '*'
        
        int firstComma  = command.indexOf(',');
        int secondComma = command.indexOf(',', firstComma + 1);
        int thirdComma  = command.indexOf(',', secondComma + 1);

        if (firstComma != -1 && secondComma != -1 && thirdComma != -1) {
          // Parse the values
          int brightness = command.substring(0, firstComma).toInt();
          int red        = command.substring(firstComma + 1, secondComma).toInt();
          int green      = command.substring(secondComma + 1, thirdComma).toInt();
          int blue       = command.substring(thirdComma + 1).toInt();

          // Set global brightness
          strip.setBrightness(brightness);

          // Apply color to all pixels
          for (int i = 0; i < NUM_PIXELS; i++) {
            strip.setPixelColor(i, strip.Color(red, green, blue));
          }
          strip.show();

          Serial.printf("All LEDs -> Brightness=%d, Color=(%d,%d,%d)\n",
                        brightness, red, green, blue);
        } else {
          Serial.println("Invalid format for '*' command. Expected 3 commas.");
        }
      }
      else if (command.startsWith("_")) {
        /**
         * Format: "_index,brightness,red,green,blue"
         * Example: "_5,100,255,0,128"
         * Sets a single LED at `index` with the given color, possibly
         * updating global brightness if you choose to handle it that way.
         *
         * If your Android app always sends '0' for brightness, you could ignore
         * that parameter or keep a default brightness. Currently, this code
         * applies it as global brightness. Adjust as needed.
         */
        command.remove(0, 1); // Remove '_'

        int firstComma  = command.indexOf(',');
        int secondComma = command.indexOf(',', firstComma + 1);
        int thirdComma  = command.indexOf(',', secondComma + 1);
        int fourthComma = command.indexOf(',', thirdComma + 1);

        if (firstComma != -1 && secondComma != -1 &&
            thirdComma != -1 && fourthComma != -1) {

          // Parse the values
          int ledIndex   = command.substring(0, firstComma).toInt();
          int brightness = command.substring(firstComma + 1, secondComma).toInt();
          int red        = command.substring(secondComma + 1, thirdComma).toInt();
          int green      = command.substring(thirdComma + 1, fourthComma).toInt();
          int blue       = command.substring(fourthComma + 1).toInt();

          // If your app always sends 0 for brightness, you can either ignore this:
          // e.g., // strip.setBrightness(...);  // or do nothing
          //
          // Or you can keep it if you want to set global brightness dynamically:
          strip.setBrightness(130);

          // Validate the LED index
          if (ledIndex >= 0 && ledIndex < NUM_PIXELS) {
            strip.setPixelColor(ledIndex, strip.Color(red, green, blue));
            strip.show();
            Serial.printf("LED %d -> Brightness=%d, Color=(%d,%d,%d)\n",
                          ledIndex, brightness, red, green, blue);
          } else {
            Serial.println("LED index out of range.");
          }
        } else {
          Serial.println("Invalid format for '_' command. Expected 4 commas.");
        }
      }
      else if (command.startsWith("!")) {
        /**
         * Format: "!"
         * Clear (turn off) all LEDs.
         */
        strip.clear();
        strip.show();
        Serial.println("All LEDs turned OFF.");
      }
      else {
        Serial.println("Unknown command (must start with '*', '_' or '!').");
      }
    }
  }
};

void setup() {
  Serial.begin(115200);

  // Initialize sensor pins
  pinMode(DIGITAL_SENSOR_PIN, INPUT);
  // pinMode(ANALOG_SENSOR_PIN, INPUT); // Not strictly needed on ESP32 for analogRead

  // Initialize the NeoPixel strip
  strip.begin();
  strip.setBrightness(100);  // Initial brightness
  strip.show();              // Turn off all pixels initially

  // Initialize BLE
  BLEDevice::init("SmartBleDevice"); // Set your BLE device name
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE service
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Create a characteristic with WRITE and NOTIFY properties
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE | 
    BLECharacteristic::PROPERTY_NOTIFY
  );

  // Add a descriptor to enable notifications on the client side
  pCharacteristic->addDescriptor(new BLE2902());

  // Assign our callbacks to handle write requests
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06); // Optional tuning of BLE parameters
  pAdvertising->setMinPreferred(0x12); // Optional tuning of BLE parameters
  BLEDevice::startAdvertising();

  Serial.println("BLE device is ready and advertising...");
}

void loop() {
  // Only send sensor data if a client is connected
  if (deviceConnected) {
    static unsigned long lastNotifyTime = 0;
    unsigned long currentMillis = millis();

    // Send updates every 500 ms
    if (currentMillis - lastNotifyTime >= 500) {
      lastNotifyTime = currentMillis;

      // Read the sensors
      int currentDigital = digitalRead(DIGITAL_SENSOR_PIN);
      int currentAnalog  = analogRead(ANALOG_SENSOR_PIN);

      // Construct sensor data string, e.g.: "D:0,A:1234"
      String sensorData = "D:" + String(currentDigital) + 
                          ",A:" + String(currentAnalog);

      // Notify the client
      pCharacteristic->setValue(sensorData.c_str());
      pCharacteristic->notify();

      Serial.print("Notifying sensor data: ");
      Serial.println(sensorData);

      // Optional: track previous values to compare changes, etc.
      previousDigitalValue = currentDigital;
      previousAnalogValue  = currentAnalog;
    }
  }

  // A small delay to allow background BLE processing
  delay(100);
}
