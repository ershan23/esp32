#include "ml_classifier.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "generated/plant_binary_model.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

constexpr int kModelWidth = 96;
constexpr int kModelHeight = 96;
constexpr int kModelChannels = 3;
constexpr int kTensorArenaSize = 240 * 1024;
constexpr float kNoLeafPixelRatio = 0.02f;
constexpr float kMinUsableLeafPixelRatio = 0.06f;
constexpr float kDiseaseMinConfidence = 0.95f;
constexpr float kDiseaseMinMargin = 0.20f;

const char *TAG = "ml_classifier";
const char *kLabels[] = {"healthy", "disease"};

const tflite::Model *model = nullptr;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr;
TfLiteTensor *output = nullptr;
uint8_t *tensor_arena = nullptr;
bool initialized = false;

struct LeafStats {
    int count;
    int min_x;
    int min_y;
    int max_x;
    int max_y;
};

struct FrameAnalysis {
    bool byte_swapped;
    float leaf_pixel_ratio;
    int roi_x0;
    int roi_y0;
    int roi_size;
};

static inline int clamp_int(int value, int low, int high)
{
    return std::min(high, std::max(low, value));
}

static inline void reset_leaf_stats(LeafStats *stats)
{
    stats->count = 0;
    stats->min_x = 0;
    stats->min_y = 0;
    stats->max_x = 0;
    stats->max_y = 0;
}

static inline void add_leaf_pixel(LeafStats *stats, int x, int y)
{
    if (stats->count == 0) {
        stats->min_x = x;
        stats->max_x = x;
        stats->min_y = y;
        stats->max_y = y;
    } else {
        stats->min_x = std::min(stats->min_x, x);
        stats->max_x = std::max(stats->max_x, x);
        stats->min_y = std::min(stats->min_y, y);
        stats->max_y = std::max(stats->max_y, y);
    }
    ++stats->count;
}

static inline int8_t quantize_channel(uint8_t value, const TfLiteTensor *tensor)
{
    const float normalized = static_cast<float>(value) / 255.0f;
    const int32_t quantized =
        static_cast<int32_t>(std::lround(normalized / tensor->params.scale)) +
        tensor->params.zero_point;
    return static_cast<int8_t>(std::min<int32_t>(127, std::max<int32_t>(-128, quantized)));
}

static inline bool is_leaf_like_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    const uint8_t maxc = std::max(r, std::max(g, b));
    const uint8_t minc = std::min(r, std::min(g, b));
    if (maxc < 30 || maxc > 252 || (maxc - minc) < 15) {
        return false;
    }

    const int excess_green = 2 * static_cast<int>(g) - static_cast<int>(r) - static_cast<int>(b);
    const bool greenish = (g > 38 && excess_green > 10 && g >= r * 7 / 10 && g >= b * 9 / 10);
    const bool yellow_green = (r > 45 && g > 45 && b < 150 && g >= r * 55 / 100 && r >= g * 55 / 100);
    const bool dark_leaf = (g > 35 && g > r * 95 / 100 && g > b * 105 / 100);
    const bool dry_leaf = (r > 50 && g > 28 && b < 115 && r >= g * 9 / 10 && r > b * 12 / 10);
    return greenish || yellow_green || dark_leaf || dry_leaf;
}

static inline void decode_rgb565_pixel(const camera_fb_t *fb,
                                       int src_x,
                                       int src_y,
                                       bool byte_swapped,
                                       uint8_t *r,
                                       uint8_t *g,
                                       uint8_t *b)
{
    const size_t src_index = (static_cast<size_t>(src_y) * fb->width + src_x) * 2;
    const uint8_t first = fb->buf[src_index];
    const uint8_t second = fb->buf[src_index + 1];
    const uint16_t rgb565 = byte_swapped
                                 ? ((static_cast<uint16_t>(second) << 8) | first)
                                 : ((static_cast<uint16_t>(first) << 8) | second);

    *r = static_cast<uint8_t>(((rgb565 >> 11) & 0x1F) * 255 / 31);
    *g = static_cast<uint8_t>(((rgb565 >> 5) & 0x3F) * 255 / 63);
    *b = static_cast<uint8_t>((rgb565 & 0x1F) * 255 / 31);
}

FrameAnalysis rgb565_frame_to_input(const camera_fb_t *fb, TfLiteTensor *tensor)
{
    const int src_w = fb->width;
    const int src_h = fb->height;
    const int crop = std::min(src_w, src_h);
    const int x0 = (src_w - crop) / 2;
    const int y0 = (src_h - crop) / 2;
    LeafStats normal_stats;
    LeafStats swapped_stats;
    reset_leaf_stats(&normal_stats);
    reset_leaf_stats(&swapped_stats);

    for (int y = 0; y < kModelHeight; ++y) {
        const int src_y = y0 + (y * crop) / kModelHeight;
        for (int x = 0; x < kModelWidth; ++x) {
            const int src_x = x0 + (x * crop) / kModelWidth;
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            decode_rgb565_pixel(fb, src_x, src_y, false, &r, &g, &b);
            if (is_leaf_like_pixel(r, g, b)) {
                add_leaf_pixel(&normal_stats, src_x, src_y);
            }
            decode_rgb565_pixel(fb, src_x, src_y, true, &r, &g, &b);
            if (is_leaf_like_pixel(r, g, b)) {
                add_leaf_pixel(&swapped_stats, src_x, src_y);
            }
        }
    }

    const bool byte_swapped = swapped_stats.count > normal_stats.count;
    const LeafStats *stats = byte_swapped ? &swapped_stats : &normal_stats;
    FrameAnalysis analysis = {
        .byte_swapped = byte_swapped,
        .leaf_pixel_ratio = static_cast<float>(stats->count) /
                            static_cast<float>(kModelWidth * kModelHeight),
        .roi_x0 = x0,
        .roi_y0 = y0,
        .roi_size = crop,
    };

    if (stats->count > 0) {
        const int box_w = stats->max_x - stats->min_x + 1;
        const int box_h = stats->max_y - stats->min_y + 1;
        int roi_size = std::max(box_w, box_h);
        roi_size += std::max(16, roi_size / 3);
        roi_size = clamp_int(roi_size, crop / 4, crop);

        const int center_x = (stats->min_x + stats->max_x) / 2;
        const int center_y = (stats->min_y + stats->max_y) / 2;
        analysis.roi_size = roi_size;
        analysis.roi_x0 = clamp_int(center_x - roi_size / 2, x0, x0 + crop - roi_size);
        analysis.roi_y0 = clamp_int(center_y - roi_size / 2, y0, y0 + crop - roi_size);
    }

    int8_t *dst = tensor->data.int8;
    for (int y = 0; y < kModelHeight; ++y) {
        const int src_y = analysis.roi_y0 + (y * analysis.roi_size) / kModelHeight;
        for (int x = 0; x < kModelWidth; ++x) {
            const int src_x = analysis.roi_x0 + (x * analysis.roi_size) / kModelWidth;
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            decode_rgb565_pixel(fb, src_x, src_y, analysis.byte_swapped, &r, &g, &b);

            *dst++ = quantize_channel(r, tensor);
            *dst++ = quantize_channel(g, tensor);
            *dst++ = quantize_channel(b, tensor);
        }
    }

    return analysis;
}

float dequantize_score(int8_t value)
{
    return (static_cast<int32_t>(value) - output->params.zero_point) * output->params.scale;
}

}  // namespace

void ml_classifier_init(void)
{
    model = tflite::GetModel(plant_binary_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "model schema %d != supported %d", model->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    tensor_arena = static_cast<uint8_t *>(
        heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (tensor_arena == nullptr) {
        ESP_LOGW(TAG, "PSRAM arena allocation failed, trying default heap");
        tensor_arena = static_cast<uint8_t *>(heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_8BIT));
    }
    if (tensor_arena == nullptr) {
        ESP_LOGE(TAG, "tensor arena allocation failed: %d bytes", kTensorArenaSize);
        return;
    }

    static tflite::MicroMutableOpResolver<8> resolver;
    if (resolver.AddConv2D() != kTfLiteOk ||
        resolver.AddFullyConnected() != kTfLiteOk ||
        resolver.AddMaxPool2D() != kTfLiteOk ||
        resolver.AddPack() != kTfLiteOk ||
        resolver.AddReshape() != kTfLiteOk ||
        resolver.AddShape() != kTfLiteOk ||
        resolver.AddSoftmax() != kTfLiteOk ||
        resolver.AddStridedSlice() != kTfLiteOk) {
        ESP_LOGE(TAG, "op resolver setup failed");
        return;
    }

    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed");
        return;
    }

    input = interpreter->input(0);
    output = interpreter->output(0);
    if (input == nullptr || output == nullptr ||
        input->type != kTfLiteInt8 ||
        output->type != kTfLiteInt8 ||
        input->dims->size != 4 ||
        input->dims->data[1] != kModelWidth ||
        input->dims->data[2] != kModelHeight ||
        input->dims->data[3] != kModelChannels ||
        output->dims->size != 2 ||
        output->dims->data[1] != 2) {
        ESP_LOGE(TAG, "unexpected tensor shape or dtype");
        return;
    }

    initialized = true;
    ESP_LOGI(TAG, "model linked: %u bytes, arena used: %u bytes",
             static_cast<unsigned int>(plant_binary_model_len),
             static_cast<unsigned int>(interpreter->arena_used_bytes()));
}

ml_classifier_result_t ml_classifier_run(void)
{
    ml_classifier_result_t result = {
        .status = ML_CLASSIFIER_NO_MODEL,
        .label = "NO MODEL",
        .confidence = 0.0f,
        .inference_ms = 0,
    };

    if (!initialized) {
        return result;
    }

    int64_t start_us = esp_timer_get_time();
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == nullptr) {
        result.status = ML_CLASSIFIER_CAMERA_ERROR;
        result.label = "CAM ERR";
        return result;
    }

    if (fb->format != PIXFORMAT_RGB565 || fb->width <= 0 || fb->height <= 0) {
        esp_camera_fb_return(fb);
        result.status = ML_CLASSIFIER_CAMERA_ERROR;
        result.label = "FMT ERR";
        return result;
    }

    const FrameAnalysis analysis = rgb565_frame_to_input(fb, input);
    esp_camera_fb_return(fb);

    if (analysis.leaf_pixel_ratio < kNoLeafPixelRatio) {
        result.status = ML_CLASSIFIER_OK;
        result.label = "NO LEAF";
        result.confidence = -1.0f;
        result.inference_ms = static_cast<uint32_t>((esp_timer_get_time() - start_us) / 1000);
        ESP_LOGI(TAG, "result=%s leaf_ratio=%.1f%% rgb565=%s time=%lu ms",
                 result.label,
                 static_cast<double>(analysis.leaf_pixel_ratio * 100.0f),
                 analysis.byte_swapped ? "swapped" : "normal",
                 static_cast<unsigned long>(result.inference_ms));
        return result;
    }

    if (analysis.leaf_pixel_ratio < kMinUsableLeafPixelRatio) {
        result.status = ML_CLASSIFIER_OK;
        result.label = "AIM LEAF";
        result.confidence = -1.0f;
        result.inference_ms = static_cast<uint32_t>((esp_timer_get_time() - start_us) / 1000);
        ESP_LOGI(TAG, "result=%s leaf_ratio=%.1f%% rgb565=%s time=%lu ms",
                 result.label,
                 static_cast<double>(analysis.leaf_pixel_ratio * 100.0f),
                 analysis.byte_swapped ? "swapped" : "normal",
                 static_cast<unsigned long>(result.inference_ms));
        return result;
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        result.status = ML_CLASSIFIER_INFERENCE_ERROR;
        result.label = "AI ERR";
        result.inference_ms = static_cast<uint32_t>((esp_timer_get_time() - start_us) / 1000);
        return result;
    }

    const float healthy_score = dequantize_score(output->data.int8[0]);
    const float disease_score = dequantize_score(output->data.int8[1]);
    const bool disease_confirmed =
        disease_score >= kDiseaseMinConfidence &&
        (disease_score - healthy_score) >= kDiseaseMinMargin;

    result.status = ML_CLASSIFIER_OK;
    result.label = disease_confirmed ? kLabels[1] : kLabels[0];
    result.confidence = disease_confirmed ? disease_score : healthy_score;
    result.confidence = std::min(1.0f, std::max(0.0f, result.confidence));
    result.inference_ms = static_cast<uint32_t>((esp_timer_get_time() - start_us) / 1000);
    ESP_LOGI(TAG, "result=%s confidence=%.1f%% healthy=%.1f%% disease=%.1f%% leaf_ratio=%.1f%% roi=%d,%d,%d rgb565=%s time=%lu ms",
             result.label,
             static_cast<double>(result.confidence * 100.0f),
             static_cast<double>(healthy_score * 100.0f),
             static_cast<double>(disease_score * 100.0f),
             static_cast<double>(analysis.leaf_pixel_ratio * 100.0f),
             analysis.roi_x0,
             analysis.roi_y0,
             analysis.roi_size,
             analysis.byte_swapped ? "swapped" : "normal",
             static_cast<unsigned long>(result.inference_ms));
    return result;
}
