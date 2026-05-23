/*
Project: ESP32 Edge Impulse Image Classification
Author: Eriberto Salgado

Description:
This program uses an ESP32 camera module together with an
Edge Impulse machine learning model to perform real-time
image classification directly on the device.

Main Features:
- Initializes ESP32 camera
- Captures grayscale images at 96x96 resolution
- Runs Edge Impulse inference on captured frames
- Displays prediction confidence scores in Serial Monitor
- Optimized for embedded AI applications
*/

#include <eribertoSalgadoESP32_inferencing.h>
#include "esp_camera.h"

// ======================================================
// Camera Pin Configuration
// ======================================================

// Power-down pin (not used)
#define PWDN_GPIO_NUM     -1

// Reset pin (not used)
#define RESET_GPIO_NUM    -1

// External clock signal pin
#define XCLK_GPIO_NUM     45

// Camera communication pins (I2C)
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

// Camera synchronization pins
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     42
#define PCLK_GPIO_NUM     5

// Number of bytes per grayscale pixel
#define EI_CAMERA_FRAME_BYTE_SIZE 1

// ======================================================
// Global Variables
// ======================================================

// Enable/disable neural network debug messages
static bool debug_nn = false;

// Tracks whether the camera has been initialized
static bool is_initialised = false;

// Buffer used to store image data
uint8_t *snapshot_buf = nullptr;

// ======================================================
// Camera Configuration
// ======================================================
static camera_config_t camera_config = {

    // Camera control pins
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    // Camera data pins
    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    // Synchronization pins
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    // Clock and timer settings
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    // Image settings
    .pixel_format = PIXFORMAT_GRAYSCALE,
    .frame_size = FRAMESIZE_96X96,

    // JPEG quality setting
    .jpeg_quality = 12,

    // Frame buffer settings
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_DRAM,
    .grab_mode = CAMERA_GRAB_LATEST,
};

// ======================================================
// Function Prototypes
// ======================================================
bool ei_camera_init(void);

bool ei_camera_capture(uint32_t img_width,
                       uint32_t img_height,
                       uint8_t *out_buf);

static int ei_camera_get_data(size_t offset,
                              size_t length,
                              float *out_ptr);

// ======================================================
// SETUP FUNCTION
// Runs once at startup
// ======================================================
void setup() {

    // Start Serial communication
    Serial.begin(921600);

    // Delay for stability during startup
    delay(2000);

    Serial.println("Edge Impulse Inferencing Demo");

    // Initialize camera
    if (!ei_camera_init()) {
        ei_printf("Failed to initialize Camera!\r\n");
    }
    else {
        ei_printf("Camera initialized\r\n");
    }

    // Wait before starting inference loop
    ei_printf("\nStarting continuous inference in 2 seconds...\n");

    ei_sleep(2000);
}

// ======================================================
// MAIN LOOP
// Continuously captures images and runs inference
// ======================================================
void loop() {

    // Small delay between inferences
    if (ei_sleep(500) != EI_IMPULSE_OK) {
        return;
    }

    // Allocate memory for image buffer
    snapshot_buf = (uint8_t *)malloc(
        EI_CLASSIFIER_INPUT_WIDTH *
        EI_CLASSIFIER_INPUT_HEIGHT *
        EI_CAMERA_FRAME_BYTE_SIZE
    );

    // Check memory allocation
    if (snapshot_buf == nullptr) {
        ei_printf("ERR: Failed to allocate snapshot buffer!\n");
        return;
    }

    // Create Edge Impulse signal structure
    ei::signal_t signal;

    signal.total_length =
        EI_CLASSIFIER_INPUT_WIDTH *
        EI_CLASSIFIER_INPUT_HEIGHT;

    signal.get_data = &ei_camera_get_data;

    // Capture image from camera
    if (!ei_camera_capture(
            EI_CLASSIFIER_INPUT_WIDTH,
            EI_CLASSIFIER_INPUT_HEIGHT,
            snapshot_buf)) {

        ei_printf("Failed to capture image\r\n");

        free(snapshot_buf);
        snapshot_buf = nullptr;

        return;
    }

    // Store inference result
    ei_impulse_result_t result = {0};

    // Run Edge Impulse classifier
    EI_IMPULSE_ERROR err =
        run_classifier(&signal, &result, debug_nn);

    // Check if inference failed
    if (err != EI_IMPULSE_OK) {

        ei_printf("ERR: Failed to run classifier (%d)\n", err);

        free(snapshot_buf);
        snapshot_buf = nullptr;

        return;
    }

    // ==========================================
    // Print Prediction Results
    // ==========================================
    ei_printf("Predictions:\n");

    for (size_t ix = 0;
         ix < EI_CLASSIFIER_LABEL_COUNT;
         ix++) {

        ei_printf("    %s: %.5f\n",
                  result.classification[ix].label,
                  result.classification[ix].value);
    }

    // Print anomaly score if enabled
#if EI_CLASSIFIER_HAS_ANOMALY == 1
    ei_printf("    Anomaly score: %.3f\n",
              result.anomaly);
#endif

    // Free image buffer memory
    free(snapshot_buf);

    snapshot_buf = nullptr;
}

// ======================================================
// CAMERA INITIALIZATION FUNCTION
// ======================================================
bool ei_camera_init(void) {

    // Prevent double initialization
    if (is_initialised) {
        return true;
    }

    // Initialize camera hardware
    esp_err_t err = esp_camera_init(&camera_config);

    // Check for initialization errors
    if (err != ESP_OK) {

        Serial.printf(
            "Camera init failed with error 0x%x\n",
            err
        );

        return false;
    }

    // Get camera sensor object
    sensor_t *s = esp_camera_sensor_get();

    // Verify sensor exists
    if (s == NULL) {

        Serial.println("ERR: Camera sensor not found");

        return false;
    }

    // Apply settings for OV3660 sensor
    if (s->id.PID == OV3660_PID) {

        // Flip image vertically
        s->set_vflip(s, 1);

        // Increase brightness slightly
        s->set_brightness(s, 1);

        // Keep normal saturation
        s->set_saturation(s, 0);
    }

    // Mark camera as initialized
    is_initialised = true;

    return true;
}

// ======================================================
// IMAGE CAPTURE FUNCTION
// Captures a frame from the camera
// ======================================================
bool ei_camera_capture(uint32_t img_width,
                       uint32_t img_height,
                       uint8_t *out_buf) {

    // Check camera initialization
    if (!is_initialised) {

        ei_printf("ERR: Camera is not initialized\r\n");

        return false;
    }

    // Capture frame buffer
    camera_fb_t *fb = esp_camera_fb_get();

    // Verify capture success
    if (!fb) {

        ei_printf("Camera capture failed\n");

        return false;
    }

    // Check image dimensions
    if (fb->width != img_width ||
        fb->height != img_height) {

        ei_printf(
            "ERR: Framebuffer size mismatch. "
            "Expected: %lux%lu, Got: %lux%lu\n",

            (unsigned long)img_width,
            (unsigned long)img_height,
            (unsigned long)fb->width,
            (unsigned long)fb->height
        );

        // Return framebuffer memory
        esp_camera_fb_return(fb);

        return false;
    }

    // Copy image data into output buffer
    memcpy(out_buf,
           fb->buf,
           img_width * img_height);

    // Simple checksum for debugging
    uint32_t sum = 0;

    for (int i = 0;
         i < img_width * img_height;
         i += 200) {

        sum += out_buf[i];
    }

    ei_printf("frame checksum: %lu\n", sum);

    // Release framebuffer memory
    esp_camera_fb_return(fb);

    return true;
}

// ======================================================
// EDGE IMPULSE DATA ACCESS FUNCTION
// Converts grayscale pixels into RGB888 format
// ======================================================
static int ei_camera_get_data(size_t offset,
                              size_t length,
                              float *out_ptr) {

    for (size_t i = 0; i < length; i++) {

        // Get grayscale pixel value
        uint8_t gray = snapshot_buf[offset + i];

        /*
         Edge Impulse expects RGB888 image format.
         Since the image is grayscale, the same value
         is copied into R, G, and B channels.
        */
        out_ptr[i] =
            (gray << 16) |
            (gray << 8)  |
             gray;
    }

    return 0;
}

// ======================================================
// Verify correct sensor type
// ======================================================
#if !defined(EI_CLASSIFIER_SENSOR) || \
    EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA

#error "Invalid model for current sensor"

#endif
