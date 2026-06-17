#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <inttypes.h>
#include <list>
#include <string>
#include <unistd.h>
#include <vector>

#include "dl_recognition_database.hpp"
#include "dl_image_jpeg.hpp"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "nvs_flash.h"

#include "stream_config.h"

#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID ""
#endif

#ifndef APP_WIFI_PASSWORD
#define APP_WIFI_PASSWORD ""
#endif

#ifndef APP_FRAME_UPLOAD_URL
#define APP_FRAME_UPLOAD_URL "http://62.234.54.71:8080/api/frame"
#endif

#ifndef APP_UPLOAD_TOKEN
#define APP_UPLOAD_TOKEN ""
#endif

extern "C" {
#include "camera.h"
}

static const char *TAG = "face_guard";

static constexpr const char *DB_MOUNT_POINT = "/spiffs";
static constexpr const char *DB_PATH = "/spiffs/face.db";
static constexpr float FACE_MATCH_THRESHOLD = 0.50f;
static constexpr int FACE_QUERY_TOP_K = 1;
static constexpr int FACE_STATE_CONFIRM_FRAMES = 3;
static constexpr int64_t FACE_STATE_MIN_HOLD_US = 1200000;
static constexpr int64_t FRAME_UPLOAD_INTERVAL_US = 250000;
static constexpr uint8_t FRAME_JPEG_QUALITY = 70;

static constexpr bool ENABLE_GPIO_KEYS = true;
static constexpr gpio_num_t KEY_ENROLL_GPIO = GPIO_NUM_1;
static constexpr gpio_num_t KEY_CLEAR_GPIO = GPIO_NUM_2;

static constexpr int WIFI_CONNECTED_BIT = BIT0;
static constexpr int WIFI_MAX_RETRY = 10;
static EventGroupHandle_t s_wifi_event_group = nullptr;
static int s_wifi_retry_num = 0;

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

static bool configured_string(const char *value)
{
    return value != nullptr && value[0] != '\0';
}

static esp_err_t init_nvs_storage(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
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

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_wifi_retry_num < WIFI_MAX_RETRY) {
            s_wifi_retry_num++;
            esp_wifi_connect();
            ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d", s_wifi_retry_num, WIFI_MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "WiFi connection failed");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi connected");
    }
}

static esp_err_t init_wifi_sta(void)
{
    if (!configured_string(APP_WIFI_SSID)) {
        ESP_LOGW(TAG, "WiFi disabled: set APP_WIFI_SSID in main/stream_config.h");
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group != nullptr, ESP_ERR_NO_MEM, TAG, "Failed to create WiFi event group");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr));

    wifi_config_t wifi_config = {};
    std::snprintf(reinterpret_cast<char *>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid), "%s", APP_WIFI_SSID);
    std::snprintf(reinterpret_cast<char *>(wifi_config.sta.password),
                  sizeof(wifi_config.sta.password),
                  "%s",
                  APP_WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(15000));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static bool wifi_is_connected(void)
{
    return s_wifi_event_group != nullptr && (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT);
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

static dl::image::jpeg_img_t encode_frame_jpeg(dl::image::img_t &img)
{
#if CONFIG_SOC_JPEG_CODEC_SUPPORTED
    dl::image::jpeg_img_t jpeg = dl::image::hw_encode_jpeg(img, FRAME_JPEG_QUALITY, 120);
    if (jpeg.data != nullptr && jpeg.data_len > 0) {
        return jpeg;
    }
    ESP_LOGW(TAG, "Hardware JPEG encode failed, falling back to software encoder");
#endif
    return dl::image::sw_encode_jpeg(img, FRAME_JPEG_QUALITY);
}

static void free_jpeg(dl::image::jpeg_img_t &jpeg)
{
    if (jpeg.data != nullptr) {
        heap_caps_free(jpeg.data);
        jpeg.data = nullptr;
        jpeg.data_len = 0;
    }
}

static esp_err_t upload_frame(const dl::image::jpeg_img_t &jpeg,
                              face_state_t state,
                              face_state_t raw_state,
                              size_t face_count,
                              int user_count)
{
    static uint32_t seq = 0;

    if (!configured_string(APP_FRAME_UPLOAD_URL) || !wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[320];
    std::snprintf(url,
                  sizeof(url),
                  "%s?state=%s&raw=%s&faces=%u&users=%d&seq=%" PRIu32,
                  APP_FRAME_UPLOAD_URL,
                  face_state_name(state),
                  face_state_name(raw_state),
                  static_cast<unsigned>(face_count),
                  user_count,
                  ++seq);

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 3000;
    config.disable_auto_redirect = true;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    if (configured_string(APP_UPLOAD_TOKEN)) {
        esp_http_client_set_header(client, "X-Device-Token", APP_UPLOAD_TOKEN);
    }
    esp_http_client_set_post_field(client, static_cast<const char *>(jpeg.data), jpeg.data_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "Frame upload failed: err=%s status=%d", esp_err_to_name(err), status);
        return err == ESP_OK ? ESP_FAIL : err;
    }
    return ESP_OK;
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs_storage());
    ESP_ERROR_CHECK(mount_face_db_storage());
    init_stdin_nonblocking();
    init_optional_keys();

    ESP_LOGI(TAG, "Commands: 'e'=enroll largest visible face, 'd'=delete last user, 'c'=clear all users");
    ESP_LOGI(TAG, "Recognition threshold=%.2f db=%s", FACE_MATCH_THRESHOLD, DB_PATH);
    ESP_LOGI(TAG, "GPIO: KEY1(enroll)=%d KEY2(clear)=%d", KEY_ENROLL_GPIO, KEY_CLEAR_GPIO);
    ESP_LOGI(TAG, "Upload URL: %s", APP_FRAME_UPLOAD_URL);

    esp_err_t wifi_err = init_wifi_sta();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Continuing without upload until WiFi is connected: %s", esp_err_to_name(wifi_err));
    }
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
    int64_t last_upload_us = 0;

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
            }
        }

        if (commands.delete_last) {
            if (face_db.delete_last_feat() == ESP_OK) {
                ESP_LOGI(TAG, "FACE_DB_DELETE_LAST users=%d", face_db.get_num_feats());
            }
        }

        if (commands.enroll ||
            (ENABLE_GPIO_KEYS && key_falling_edge(KEY_ENROLL_GPIO))) {
            enroll_largest_face(img, faces, feat_model, face_db);
        }

        face_state_t raw_state = classify_faces(img, faces, feat_model, face_db);
        face_state_t state = update_face_state_filter(state_filter, raw_state);
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

        if (now_us - last_upload_us >= FRAME_UPLOAD_INTERVAL_US) {
            dl::image::jpeg_img_t jpeg = encode_frame_jpeg(img);
            if (jpeg.data != nullptr && jpeg.data_len > 0) {
                upload_frame(jpeg, state, raw_state, faces.size(), face_db.get_num_feats());
                free_jpeg(jpeg);
            } else {
                ESP_LOGW(TAG, "JPEG encode failed");
            }
            last_upload_us = now_us;
        }

        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
