/**
 * ESP32-CAM Motion Detection and Streaming System
 * Created by: Mateusz Jachimowski
 * Last Updated: 2025-01-28
 *
 * This program implements a WiFi-connected camera system with motion detection
 * and power-saving features using deep sleep mode.
 */

// Essential libraries for camera functionality and WebSocket communication
#include "esp_camera.h"
#include <ArduinoWebsockets.h>

// Specify the camera model being used
#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"

// WiFi Configuration
const char* ssid = "ESP32-AP"; // Access Point SSID
const char* password = "monitoring"; // Access Point Password

// WebSocket Server Configuration
const char* websockets_server_host = "192.168.4.1"; // Server IP address
const uint16_t websockets_server_port = 8888; // Server port

using namespace websockets;
WebsocketsClient client;

// Motion Detection Configuration
const int PIR_PIN = GPIO_NUM_13; // PIR sensor pin
unsigned long startTime = 0; // Timer for sleep management
unsigned long sleepTriggerTimeout = 30000; // Sleep timeout in milliseconds
bool lastPirState = false; // Previous PIR sensor state

/**
 * Monitors motion sensor and manages the activity timer
 * Sends motion status updates via WebSocket when motion state changes
 */
void checkAndResetTimer() {
    bool currentPirState = digitalRead(PIR_PIN);
    
    // Check for motion state changes
    if (currentPirState != lastPirState) {
        if (client.available()) {
          // Send motion status update via WebSocket
            String motionMessage = "{\"type\":\"motion\",\"detected\":" + String(currentPirState ? "true" : "false") + "}";
            client.send(motionMessage);
        }

        // Reset timer when motion is detected
        if (currentPirState == HIGH) {
            startTime = millis();
        }
    }
    
    lastPirState = currentPirState;
}

/**
 * Prepares the device for deep sleep mode
 * - Closes WebSocket connection
 * - Disconnects WiFi
 * - Configures wake-up source (PIR sensor)
 */
void prepareSleep() {

    // Clean up WebSocket connection
    if (client.available()) {
        client.send("{\"type\":\"motion\",\"detected\":false}");
        client.close();
    }

    // Shutdown WiFi
    delay(100);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Configure wake-up source
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);
    
    delay(100);
    
    esp_deep_sleep_start();
}

/**
 * Initial setup function
 * Configures hardware components and establishes network connections
 */
void setup() {
    Serial.begin(115200);

    // Initialize motion sensor and timer
    pinMode(PIR_PIN, INPUT);
    startTime = millis();

    Serial.setDebugOutput(true);
    Serial.println();
    
    // Initialize camera with configuration
    camera_config_t config = setupCameraConfig();

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        return;
    }

    // Connect to WiFi network
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    // Establish WebSocket connection
    while(!client.connect(websockets_server_host, websockets_server_port, "/")){
        delay(500);
    }

    if(client.available()){
        client.send("CAMERA_RECONNECTED");
    }
}

/**
 * Main program loop
 * Handles motion detection, camera capture, and WebSocket streaming
 */
void loop() {
    // Check motion sensor and manage sleep timer
    checkAndResetTimer();

    // Enter deep sleep if timeout reached
    if ((unsigned long)(millis() - startTime) >= sleepTriggerTimeout){
        prepareSleep();
    }

    // Capture and stream camera frame
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    fb = esp_camera_fb_get();
    if(!fb){
        esp_camera_fb_return(fb);
        return;
    }

    // Verify image format
    if(fb->format != PIXFORMAT_JPEG){
        return;
    }

    // Set camera identifier in image buffer
    fb->buf[12] = 0x01; //First cam
    //fb->buf[12] = 0x02; //Second cam

    // Stream image via WebSocket
    if (client.available()) {
        client.sendBinary((const char*) fb->buf, fb->len);
    }
    esp_camera_fb_return(fb);

    delay(50); // Small delay to prevent overwhelming the system
}

/**
 * Configures camera hardware settings
 * Returns a camera_config_t structure with all necessary parameters
 */
camera_config_t setupCameraConfig(){
    camera_config_t config;
    
    // Configure camera pins
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    
    // Configure camera parameters
    config.xclk_freq_hz = 10000000; // 10MHz XCLK frequency
    config.pixel_format = PIXFORMAT_JPEG; // output format
    config.frame_size = FRAMESIZE_QVGA; // 320x240 resolution
    config.jpeg_quality = 12; // Lower value = higher quality
    config.fb_count = 2; // Number of frame buffers

    return config;
}