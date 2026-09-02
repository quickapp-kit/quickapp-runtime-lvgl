#include <stdint.h>

#include "quickapp/lvgl/sdk/runtime.h"

static qak_result_t accepted(void* context, const qak_rpk_source_t* source) {
  (void)context;
  (void)source;
  return (qak_result_t){QAK_STATUS_OK, 0};
}

static qak_result_t surface(void* context, const qak_surface_t* value) {
  (void)context;
  (void)value;
  return (qak_result_t){QAK_STATUS_OK, 0};
}

static qak_result_t input(void* context, const qak_input_t* value) {
  (void)context;
  (void)value;
  return (qak_result_t){QAK_STATUS_OK, 0};
}

static qak_result_t lifecycle(void* context, qak_lifecycle_signal_t signal) {
  (void)context;
  (void)signal;
  return (qak_result_t){QAK_STATUS_OK, 0};
}

int main(void) {
  const qak_runtime_adapter_t adapter = {
      sizeof(qak_runtime_adapter_t), NULL, accepted, surface, input, lifecycle,
      NULL, NULL};
  const qak_runtime_config_t config = {
      sizeof(qak_runtime_config_t), QAK_RUNTIME_ABI_VERSION, 0, 1, 0,
      &adapter};
  qak_runtime_t* runtime = NULL;
  if (qak_runtime_create(&config, &runtime).status != QAK_STATUS_OK) return 1;
  qak_runtime_destroy(runtime);
  return 0;
}
