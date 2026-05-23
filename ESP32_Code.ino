#include <eribertoSalgadoESP32_inferencing.h>
#include "esp_camera.h"

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     45
#define SIOD_GPIO_NUM     1
#define SIOC_GPIO_NUM     2
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       46
#define Y7_GPIO_NUM       8
#define Y6_GPIO_NUM       7
#define Y5_GPIO_NUM       4
#define Y4_GPIO_NUM       41
#define Y3_GPIO_NUM       40
#define Y2_GPIO_NUM       39
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     42
#define PCLK_GPIO_NUM     5

#define EI_CAMERA_FRAME_BYTE_SIZE 1

static bool debug_nn = false;
static bool is_initialised = false;
uint8_t *snapshot_buf = nullptr;

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_GRAYSCALE,
    .frame_size = FRAMESIZE_96X96,

    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_DRAM,
    .grab_mode = CAMERA_GRAB_LATEST,
};

bool ei_camera_init(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

void setup() {
    Serial.begin(921600);
    delay(2000);

    Serial.println("Edge Impulse Inferencing Demo");

    if (!ei_camera_init()) {
        ei_printf("Failed to initialize Camera!\r\n");
    } else {
        ei_printf("Camera initialized\r\n");
    }

    ei_printf("\nStarting continuous inference in 2 seconds...\n");
    ei_sleep(2000);
}

void loop() {
    if (ei_sleep(500) != EI_IMPULSE_OK) {
        return;
    }

    snapshot_buf = (uint8_t *)malloc(EI_CLASSIFIER_INPUT_WIDTH *
                                     EI_CLASSIFIER_INPUT_HEIGHT *
                                     EI_CAMERA_FRAME_BYTE_SIZE);

    if (snapshot_buf == nullptr) {
        ei_printf("ERR: Failed to allocate snapshot buffer!\n");
        return;
    }

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    if (!ei_camera_capture(EI_CLASSIFIER_INPUT_WIDTH,
                           EI_CLASSIFIER_INPUT_HEIGHT,
                           snapshot_buf)) {
        ei_printf("Failed to capture image\r\n");
        free(snapshot_buf);
        snapshot_buf = nullptr;
        return;
    }

    ei_impulse_result_t result = {0};
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);

    if (err != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", err);
        free(snapshot_buf);
        snapshot_buf = nullptr;
        return;
    }

    ei_printf("Predictions:\n");

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf("    %s: %.5f\n",
                  result.classification[ix].label,
                  result.classification[ix].value);
    }

#if EI_CLASSIFIER_HAS_ANOMALY == 1
    ei_printf("    Anomaly score: %.3f\n", result.anomaly);
#endif

    free(snapshot_buf);
    snapshot_buf = nullptr;
}

bool ei_camera_init(void) {
    if (is_initialised) return true;

    esp_err_t err = esp_camera_init(&camera_config);

    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();

    if (s == NULL) {
        Serial.println("ERR: Camera sensor not found");
        return false;
    }

    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, 0);
    }

    is_initialised = true;
    return true;
}

bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    if (!is_initialised) {
        ei_printf("ERR: Camera is not initialized\r\n");
        return false;
    }

    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb) {
        ei_printf("Camera capture failed\n");
        return false;
    }

    if (fb->width != img_width || fb->height != img_height) {
        ei_printf("ERR: Framebuffer size mismatch. Expected: %lux%lu, Got: %lux%lu\n",
                  (unsigned long)img_width,
                  (unsigned long)img_height,
                  (unsigned long)fb->width,
                  (unsigned long)fb->height);

        esp_camera_fb_return(fb);
        return false;
    }

    memcpy(out_buf, fb->buf, img_width * img_height);

    uint32_t sum = 0;
    for (int i = 0; i < img_width * img_height; i += 200) {
        sum += out_buf[i];
    }
    ei_printf("frame checksum: %lu\n", sum);

    esp_camera_fb_return(fb);
    return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        uint8_t gray = snapshot_buf[offset + i];

        // Edge Impulse camera models expect packed RGB888 values.
        // This converts grayscale into R=G=B.
        out_ptr[i] = (gray << 16) | (gray << 8) | gray;
    }

    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif
