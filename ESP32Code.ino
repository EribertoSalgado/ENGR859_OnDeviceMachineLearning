/*
Project: ESP32 Edge Capture
Author: Eriberto Salgado

Description:
This program initializes an ESP32 camera module and captures images
when a serial command is received. The image is then transferred
through Serial communication for use in Edge AI projects such as
Edge Impulse.

Main Features:
- Initializes OV2640 camera module
- Uses AXP313A power management IC
- Captures JPEG images at VGA resolution
- Sends image data over Serial communication
- Optimized for low memory usage
*/

#include "esp_camera.h"
#include "DFRobot_AXP313A.h"

// ==========================
// Camera Pin Configuration
// ==========================

// Power-down pin (not connected)
#define PWDN_GPIO_NUM     -1

// Reset pin (not connected)
#define RESET_GPIO_NUM    -1

// External clock pin
#define XCLK_GPIO_NUM     45

// I2C communication pins for camera control
#define SIOD_GPIO_NUM     1
#define SIOC_GPIO_NUM     2

// Camera data pins
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       46
#define Y7_GPIO_NUM       8
#define Y6_GPIO_NUM       7
#define Y5_GPIO_NUM       4
#define Y4_GPIO_NUM       41
#define Y3_GPIO_NUM       40
#define Y2_GPIO_NUM       39

// Synchronization and clock pins
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     42
#define PCLK_GPIO_NUM     5

// Create AXP313A power management object
DFRobot_AXP313A axp;

// ======================================================
// SETUP FUNCTION
// Runs once when the ESP32 starts
// ======================================================
void setup() {

  // Start Serial communication
  // High baud rate improves image transfer speed
  Serial.begin(921600);

  // Initialize the AXP313A power management chip
  while (axp.begin() != 0) {
    Serial.println("AXP313A initialization failed");
    delay(1000);
  }

  // Turn on power for the OV2640 camera
  axp.enableCameraPower(axp.eOV2640);

  // ==========================================
  // Camera Configuration Structure
  // ==========================================
  camera_config_t config;

  // LED controller settings
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  // Camera data pin assignments
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  // Camera clock and sync pins
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  // Camera control pins (I2C)
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  // Optional pins
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  // Camera settings
  config.xclk_freq_hz = 20000000;          // 20 MHz clock
  config.frame_size = FRAMESIZE_VGA;       // 640x480 resolution
  config.pixel_format = PIXFORMAT_JPEG;    // JPEG image format

  // Frame buffer settings
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_DRAM;

  // JPEG quality:
  // Lower value = higher quality image
  config.jpeg_quality = 12;

  // Use only one frame buffer to reduce memory usage
  config.fb_count = 1;

  // ==========================================
  // Initialize Camera
  // ==========================================
  esp_err_t err = esp_camera_init(&config);

  // Check if camera initialized correctly
  if (err != ESP_OK) {
    Serial.printf("Camera initialization failed with error 0x%x\n", err);
    return;
  }

  // ==========================================
  // Camera Sensor Adjustments
  // ==========================================
  sensor_t *s = esp_camera_sensor_get();

  if (s) {

    // Slightly brighten the image
    s->set_brightness(s, 1);

    // Increase contrast slightly
    s->set_contrast(s, 1);

    // Reduce color saturation
    s->set_saturation(s, -2);

    // Disable image effects
    s->set_special_effect(s, 0);

    // Enable automatic white balance
    s->set_whitebal(s, 1);

    // Enable automatic white balance gain
    s->set_awb_gain(s, 1);

    // Auto white balance mode
    s->set_wb_mode(s, 0);

    // Set camera gain ceiling
    s->set_gainceiling(s, (gainceiling_t)GAINCEILING_2X);
  }

  // Notify user that the camera is ready
  Serial.println("READY");
}

// ======================================================
// LOOP FUNCTION
// Continuously checks for serial commands
// ======================================================
void loop() {

  // Check if data is available on Serial
  if (Serial.available()) {

    // Read incoming command
    String command = Serial.readStringUntil('\n');

    // ==========================================
    // Capture Image Command
    // ==========================================
    if (command == "CAPTURE") {

      // Capture a frame from the camera
      camera_fb_t *fb = esp_camera_fb_get();

      // Check if capture failed
      if (!fb) {
        Serial.println("ERROR");
        return;
      }

      // Small delay before processing
      delay(100);

      // Return frame buffer memory
      esp_camera_fb_return(fb);

      delay(100);

      // Send image size first
      Serial.println(fb->len);

      // Delay to ensure complete transmission
      delay(100);

      // Send image bytes through Serial
      Serial.write(fb->buf, fb->len);

      delay(100);
    }
  }

  // Small delay to reduce CPU usage
  delay(10);
}
