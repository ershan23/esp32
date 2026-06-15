#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <list>
#include <string>
#include <unistd.h>
#include <vector>

#include "dl_recognition_database.hpp"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"

extern "C" {
#include "camera.h"
#include "lcd.h"
}

static const char *TAG = "face_guard";

static constexpr const char *DB_MOUNT_POINT = "/spiffs";
static constexpr const char *DB_PATH = "/spiffs/face.db";
static constexpr float FACE_MATCH_THRESHOLD = 0.50f;
static constexpr int FACE_QUERY_TOP_K = 1;
static constexpr int64_t LED_EVENT_HOLD_US = 2000000;
static constexpr int FACE_STATE_CONFIRM_FRAMES = 3;
static constexpr int64_t FACE_STATE_MIN_HOLD_US = 1200000;

static constexpr bool ENABLE_GPIO_KEYS = true;
static constexpr gpio_num_t KEY_ENROLL_GPIO = GPIO_NUM_1;
static constexpr gpio_num_t KEY_CLEAR_GPIO = GPIO_NUM_2;

static constexpr gpio_num_t LED_RED_GPIO = GPIO_NUM_8;
static constexpr gpio_num_t LED_GREEN_GPIO = GPIO_NUM_41;
static constexpr gpio_num_t LED_BLUE_GPIO = GPIO_NUM_42;
static constexpr gpio_num_t LED_YELLOW_GPIO = GPIO_NUM_20;
static constexpr int LED_ON_LEVEL = 0;
static constexpr int LED_OFF_LEVEL = 1;

enum class face_state_t {
    FACE_NONE,
    FACE_USER,
    FACE_UNKNOWN,
    FACE_USER_AND_UNKNOWN,
};

static const char *face_state_name(face_state_t state)
{
    switch (state) {
    case face_state_t::FACE_NONE:
        return "FACE_NONE";
    case face_state_t::FACE_USER:
        return "FACE_USER";
    case face_state_t::FACE_UNKNOWN:
        return "FACE_UNKNOWN";
    case face_state_t::FACE_USER_AND_UNKNOWN:
        return "FACE_USER_AND_UNKNOWN";
    default:
        return "FACE_INVALID";
    }
}

typedef struct {
    face_state_t stable;
    face_state_t candidate;
    int candidate_count;
    int64_t last_change_us;
} face_state_filter_t;

static face_state_t update_face_state_filter(face_state_filter_t &filter, face_state_t raw_state)
{
    int64_t now_us = esp_timer_get_time();
    if (raw_state == filter.stable) {
        filter.candidate = raw_state;
        filter.candidate_count = 0;
        return filter.stable;
    }

    if (raw_state != filter.candidate) {
        filter.candidate = raw_state;
        filter.candidate_count = 1;
        return filter.stable;
    }

    filter.candidate_count++;
    if (filter.candidate_count >= FACE_STATE_CONFIRM_FRAMES &&
        now_us - filter.last_change_us >= FACE_STATE_MIN_HOLD_US) {
        filter.stable = raw_state;
        filter.candidate_count = 0;
        filter.last_change_us = now_us;
    }
    return filter.stable;
}

static esp_err_t mount_face_db_storage(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = DB_MOUNT_POINT,
        .partition_label = "storage",
        .max_files = 3,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    err = esp_spiffs_info(conf.partition_label, &total, &used);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted: total=%u used=%u", static_cast<unsigned>(total), static_cast<unsigned>(used));
    }
    return err;
}

static void init_optional_keys(void)
{
    if (!ENABLE_GPIO_KEYS) {
        ESP_LOGW(TAG, "GPIO keys disabled");
        return;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY_ENROLL_GPIO) | (1ULL << KEY_CLEAR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

static void init_status_leds(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_RED_GPIO) | (1ULL << LED_GREEN_GPIO) | (1ULL << LED_BLUE_GPIO) |
                        (1ULL << LED_YELLOW_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(LED_RED_GPIO, LED_OFF_LEVEL);
    gpio_set_level(LED_GREEN_GPIO, LED_ON_LEVEL);
    gpio_set_level(LED_BLUE_GPIO, LED_OFF_LEVEL);
    gpio_set_level(LED_YELLOW_GPIO, LED_OFF_LEVEL);
}

static void set_status_leds(bool red, bool green, bool blue, bool yellow)
{
    gpio_set_level(LED_RED_GPIO, red ? LED_ON_LEVEL : LED_OFF_LEVEL);
    gpio_set_level(LED_GREEN_GPIO, green ? LED_ON_LEVEL : LED_OFF_LEVEL);
    gpio_set_level(LED_BLUE_GPIO, blue ? LED_ON_LEVEL : LED_OFF_LEVEL);
    gpio_set_level(LED_YELLOW_GPIO, yellow ? LED_ON_LEVEL : LED_OFF_LEVEL);
}

static void update_status_leds(face_state_t state, int64_t blue_until_us, int64_t yellow_until_us)
{
    int64_t now_us = esp_timer_get_time();
    bool unknown = (state == face_state_t::FACE_UNKNOWN || state == face_state_t::FACE_USER_AND_UNKNOWN);

    if (now_us < yellow_until_us) {
        set_status_leds(false, false, false, true);
    } else if (now_us < blue_until_us) {
        set_status_leds(false, false, true, false);
    } else if (unknown) {
        set_status_leds(true, false, false, false);
    } else {
        set_status_leds(false, true, false, false);
    }
}

static void init_stdin_nonblocking(void)
{
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
}

typedef struct {
    bool enroll;
    bool clear;
    bool delete_last;
} serial_commands_t;

static serial_commands_t read_serial_commands(void)
{
    serial_commands_t commands = {};
    int ch = 0;
    while ((ch = getchar()) != EOF) {
        if (ch == 'e' || ch == 'E') {
            commands.enroll = true;
        } else if (ch == 'c' || ch == 'C') {
            commands.clear = true;
        } else if (ch == 'd' || ch == 'D') {
            commands.delete_last = true;
        }
    }
    return commands;
}

static bool key_falling_edge(gpio_num_t gpio)
{
    static int last_enroll = 1;
    static int last_clear = 1;
    int *last = (gpio == KEY_ENROLL_GPIO) ? &last_enroll : &last_clear;
    int now = gpio_get_level(gpio);
    bool pressed = (*last == 1 && now == 0);
    *last = now;
    return pressed;
}

static dl::image::img_t frame_to_dl_image(camera_fb_t *fb)
{
    return {
        .data = fb->buf,
        .width = static_cast<uint16_t>(fb->width),
        .height = static_cast<uint16_t>(fb->height),
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE,
    };
}

static std::list<dl::detect::result_t>::const_iterator largest_face(const std::list<dl::detect::result_t> &faces)
{
    return std::max_element(faces.begin(), faces.end(), [](const auto &a, const auto &b) {
        return a.box_area() < b.box_area();
    });
}

static esp_err_t enroll_largest_face(dl::image::img_t &img,
                                     const std::list<dl::detect::result_t> &faces,
                                     HumanFaceFeat &feat_model,
                                     dl::recognition::DataBase &db)
{
    if (faces.empty()) {
        ESP_LOGW(TAG, "Enroll skipped: no face detected");
        return ESP_FAIL;
    }

    auto face = largest_face(faces);
    dl::TensorBase *feat = feat_model.run(img, face->keypoint);
    esp_err_t err = db.enroll_feat(feat);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "USER_ENROLLED id_count=%d", db.get_num_feats());
    } else {
        ESP_LOGE(TAG, "Enroll failed: %s", esp_err_to_name(err));
    }
    return err;
}

static face_state_t classify_faces(dl::image::img_t &img,
                                   const std::list<dl::detect::result_t> &faces,
                                   HumanFaceFeat &feat_model,
                                   dl::recognition::DataBase &db)
{
    if (faces.empty()) {
        return face_state_t::FACE_NONE;
    }

    bool has_user = false;
    bool has_unknown = false;

    if (db.get_num_feats() == 0) {
        return face_state_t::FACE_UNKNOWN;
    }

    int face_idx = 0;
    for (const auto &face : faces) {
        dl::TensorBase *feat = feat_model.run(img, face.keypoint);
        std::vector<dl::recognition::result_t> matches = db.query_feat(feat, FACE_MATCH_THRESHOLD, FACE_QUERY_TOP_K);
        if (matches.empty()) {
            has_unknown = true;
            ESP_LOGI(TAG, "face[%d] unknown", face_idx);
        } else {
            has_user = true;
            ESP_LOGI(TAG,
                     "face[%d] user id=%u sim=%.3f",
                     face_idx,
                     static_cast<unsigned>(matches[0].id),
                     matches[0].similarity);
        }
        face_idx++;
    }

    if (has_user && has_unknown) {
        return face_state_t::FACE_USER_AND_UNKNOWN;
    }
    return has_user ? face_state_t::FACE_USER : face_state_t::FACE_UNKNOWN;
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(mount_face_db_storage());
    init_stdin_nonblocking();
    init_optional_keys();
    init_status_leds();

    ESP_LOGI(TAG, "Commands: 'e'=enroll largest visible face, 'd'=delete last user, 'c'=clear all users");
    ESP_LOGI(TAG, "Recognition threshold=%.2f db=%s", FACE_MATCH_THRESHOLD, DB_PATH);
    ESP_LOGI(TAG,
             "GPIO: KEY1(enroll)=%d KEY2(clear)=%d LED_R=%d LED_G=%d LED_B=%d LED_Y=%d",
             KEY_ENROLL_GPIO,
             KEY_CLEAR_GPIO,
             LED_RED_GPIO,
             LED_GREEN_GPIO,
             LED_BLUE_GPIO,
             LED_YELLOW_GPIO);

    lcd_init();
    camera_init();

    HumanFaceDetect detect_model(HumanFaceDetect::MSRMNP_S8_V1, false);
    HumanFaceFeat feat_model(HumanFaceFeat::MFN_S8_V1, false);
    dl::recognition::DataBase face_db(DB_PATH, feat_model.get_feat_len());

    ESP_LOGI(TAG, "Face DB loaded: users=%d", face_db.get_num_feats());

    face_state_t last_state = face_state_t::FACE_NONE;
    face_state_filter_t state_filter = {
        .stable = face_state_t::FACE_NONE,
        .candidate = face_state_t::FACE_NONE,
        .candidate_count = 0,
        .last_change_us = esp_timer_get_time(),
    };
    int64_t last_report_us = 0;
    int64_t blue_until_us = 0;
    int64_t yellow_until_us = 0;

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Camera frame buffer is NULL");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        dl::image::img_t img = frame_to_dl_image(fb);
        std::list<dl::detect::result_t> &faces = detect_model.run(img);

        serial_commands_t commands = read_serial_commands();

        if (commands.clear ||
            (ENABLE_GPIO_KEYS && key_falling_edge(KEY_CLEAR_GPIO))) {
            if (face_db.clear_all_feats() == ESP_OK) {
                ESP_LOGI(TAG, "FACE_DB_CLEARED");
                yellow_until_us = esp_timer_get_time() + LED_EVENT_HOLD_US;
            }
        }

        if (commands.delete_last) {
            if (face_db.delete_last_feat() == ESP_OK) {
                ESP_LOGI(TAG, "FACE_DB_DELETE_LAST users=%d", face_db.get_num_feats());
                yellow_until_us = esp_timer_get_time() + LED_EVENT_HOLD_US;
            }
        }

        if (commands.enroll ||
            (ENABLE_GPIO_KEYS && key_falling_edge(KEY_ENROLL_GPIO))) {
            if (enroll_largest_face(img, faces, feat_model, face_db) == ESP_OK) {
                blue_until_us = esp_timer_get_time() + LED_EVENT_HOLD_US;
            }
        }

        face_state_t raw_state = classify_faces(img, faces, feat_model, face_db);
        face_state_t state = update_face_state_filter(state_filter, raw_state);
        update_status_leds(state, blue_until_us, yellow_until_us);
        int64_t now_us = esp_timer_get_time();
        if (state != last_state || now_us - last_report_us > 1000000) {
            ESP_LOGI(TAG,
                     "%s raw=%s faces=%u users=%d",
                     face_state_name(state),
                     face_state_name(raw_state),
                     static_cast<unsigned>(faces.size()),
                     face_db.get_num_feats());
            last_state = state;
            last_report_us = now_us;
        }

        lcd_show_picture(fb->buf);
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
