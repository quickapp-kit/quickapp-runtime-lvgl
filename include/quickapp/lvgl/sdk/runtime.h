#ifndef QUICKAPP_LVGL_SDK_RUNTIME_H_
#define QUICKAPP_LVGL_SDK_RUNTIME_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QAK_RUNTIME_ABI_VERSION UINT32_C(1)

typedef struct qak_runtime qak_runtime_t;

typedef struct qak_bytes {
  const uint8_t* data;
  uint64_t size;
} qak_bytes_t;

typedef enum qak_status {
  QAK_STATUS_OK = 0,
  QAK_STATUS_ACCEPTED = 1,
  QAK_STATUS_INVALID_ARGUMENT = 2,
  QAK_STATUS_INVALID_STATE = 3,
  QAK_STATUS_WRONG_THREAD = 4,
  QAK_STATUS_CAPACITY_EXCEEDED = 5,
  QAK_STATUS_PACKAGE_INVALID = 6,
  QAK_STATUS_PACKAGE_TOO_LARGE = 7,
  QAK_STATUS_UNSUPPORTED = 8,
  QAK_STATUS_DESTROYED = 9,
  QAK_STATUS_HOST_REJECTED = 10,
  QAK_STATUS_BUSY = 11,
  QAK_STATUS_INTERNAL = 12,
} qak_status_t;

typedef struct qak_result {
  qak_status_t status;
  uint32_t retryable;
} qak_result_t;

typedef enum qak_rpk_source_kind {
  QAK_RPK_SOURCE_MEMORY = 1,
  QAK_RPK_SOURCE_PATH = 2,
} qak_rpk_source_kind_t;

typedef struct qak_rpk_source {
  uint32_t struct_size;
  qak_rpk_source_kind_t kind;
  qak_bytes_t value;
  uint32_t has_expected_sha256;
  uint8_t expected_sha256[32];
} qak_rpk_source_t;

typedef struct qak_surface {
  uint32_t struct_size;
  uint64_t opaque_surface;
  uint32_t width_px;
  uint32_t height_px;
  uint32_t flags;
} qak_surface_t;

typedef enum qak_input_kind {
  QAK_INPUT_POINTER = 1,
  QAK_INPUT_KEY = 2,
  QAK_INPUT_TEXT = 3,
} qak_input_kind_t;

typedef enum qak_input_action {
  QAK_INPUT_DOWN = 1,
  QAK_INPUT_MOVE = 2,
  QAK_INPUT_UP = 3,
  QAK_INPUT_CANCEL = 4,
} qak_input_action_t;

typedef struct qak_input {
  uint32_t struct_size;
  qak_input_kind_t kind;
  qak_input_action_t action;
  int32_t x_px;
  int32_t y_px;
  uint32_t key_code;
  uint32_t modifiers;
  uint64_t timestamp_ns;
  qak_bytes_t text;
} qak_input_t;

typedef enum qak_lifecycle_signal {
  QAK_LIFECYCLE_SURFACE_SHOW = 1,
  QAK_LIFECYCLE_SURFACE_HIDE = 2,
  QAK_LIFECYCLE_APP_FOREGROUND = 3,
  QAK_LIFECYCLE_APP_BACKGROUND = 4,
} qak_lifecycle_signal_t;

typedef qak_result_t (*qak_load_rpk_fn)(void*, const qak_rpk_source_t*);
typedef qak_result_t (*qak_attach_surface_fn)(void*, const qak_surface_t*);
typedef qak_result_t (*qak_dispatch_input_fn)(void*, const qak_input_t*);
typedef qak_result_t (*qak_lifecycle_fn)(void*, qak_lifecycle_signal_t);
typedef qak_result_t (*qak_pump_fn)(void*);
typedef qak_result_t (*qak_destroy_fn)(void*);

typedef struct qak_runtime_adapter {
  uint32_t struct_size;
  void* context;
  qak_load_rpk_fn load_rpk;
  qak_attach_surface_fn attach_surface;
  qak_dispatch_input_fn dispatch_input;
  qak_lifecycle_fn update_lifecycle;
  qak_pump_fn pump;
  qak_destroy_fn destroy;
} qak_runtime_adapter_t;

typedef struct qak_runtime_config {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t max_rpk_bytes;
  uint32_t max_surfaces;
  uint32_t reserved;
  const qak_runtime_adapter_t* adapter;
} qak_runtime_config_t;

qak_result_t qak_runtime_create(const qak_runtime_config_t* config,
                                qak_runtime_t** out_runtime);
qak_result_t qak_runtime_load_rpk(qak_runtime_t* runtime,
                                  const qak_rpk_source_t* source);
qak_result_t qak_runtime_attach_surface(qak_runtime_t* runtime,
                                         const qak_surface_t* surface);
qak_result_t qak_runtime_dispatch_input(qak_runtime_t* runtime,
                                        const qak_input_t* input);
qak_result_t qak_runtime_update_lifecycle(qak_runtime_t* runtime,
                                          qak_lifecycle_signal_t signal);
qak_result_t qak_runtime_pump(qak_runtime_t* runtime);
qak_result_t qak_runtime_destroy(qak_runtime_t* runtime);

#ifdef __cplusplus
}
#endif

#endif
