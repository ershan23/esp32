#ifndef __ML_CLASSIFIER_H_
#define __ML_CLASSIFIER_H_

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ML_CLASSIFIER_OK = 0,
    ML_CLASSIFIER_NO_MODEL,
    ML_CLASSIFIER_CAMERA_ERROR,
    ML_CLASSIFIER_INFERENCE_ERROR,
} ml_classifier_status_t;

typedef struct {
    ml_classifier_status_t status;
    const char *label;
    float confidence;
    uint32_t inference_ms;
} ml_classifier_result_t;

void ml_classifier_init(void);
ml_classifier_result_t ml_classifier_run(void);

#ifdef __cplusplus
}
#endif

#endif
