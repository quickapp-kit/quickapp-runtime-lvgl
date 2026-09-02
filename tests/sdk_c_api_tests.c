#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "quickapp/lvgl/sdk/runtime.h"

struct test_context {
  uint32_t loads;
  uint32_t attaches;
  uint32_t inputs;
  uint32_t lifecycle;
  uint32_t pumps;
  uint32_t destroys;
};

static qak_result_t ok(void) { return (qak_result_t){QAK_STATUS_OK, 0}; }

static qak_result_t load(void* opaque, const qak_rpk_source_t* source) {
  struct test_context* context = (struct test_context*)opaque;
  assert(source->kind == QAK_RPK_SOURCE_MEMORY);
  assert(source->value.size == 4);
  context->loads += 1;
  return ok();
}

static qak_result_t attach(void* opaque, const qak_surface_t* surface) {
  struct test_context* context = (struct test_context*)opaque;
  assert(surface->opaque_surface == UINT64_C(7));
  context->attaches += 1;
  return ok();
}

static qak_result_t input(void* opaque, const qak_input_t* value) {
  struct test_context* context = (struct test_context*)opaque;
  assert(value->kind == QAK_INPUT_POINTER);
  context->inputs += 1;
  return ok();
}

static qak_result_t lifecycle(void* opaque, qak_lifecycle_signal_t signal) {
  struct test_context* context = (struct test_context*)opaque;
  assert(signal == QAK_LIFECYCLE_SURFACE_SHOW);
  context->lifecycle += 1;
  return ok();
}

static qak_result_t pump(void* opaque) {
  struct test_context* context = (struct test_context*)opaque;
  context->pumps += 1;
  return ok();
}

static qak_result_t destroy(void* opaque) {
  struct test_context* context = (struct test_context*)opaque;
  context->destroys += 1;
  return ok();
}

int main(void) {
  static const uint8_t rpk_header[] = {'P', 'K', 3, 4};
  static const uint8_t invalid_digest[32] = {1};
  static const uint8_t traversal_path[] = {'.', '.', '/', 'r', 'p', 'k'};
  struct test_context context = {0};
  const qak_runtime_adapter_t adapter = {
      sizeof(qak_runtime_adapter_t), &context, load, attach, input,
      lifecycle, pump, destroy};
  const qak_runtime_config_t config = {
      sizeof(qak_runtime_config_t), QAK_RUNTIME_ABI_VERSION, 4096, 1, 0,
      &adapter};
  qak_runtime_t* runtime = NULL;
  assert(qak_runtime_create(&config, &runtime).status == QAK_STATUS_OK);

  qak_rpk_source_t bad_digest = {
      sizeof(qak_rpk_source_t), QAK_RPK_SOURCE_MEMORY,
      {rpk_header, sizeof(rpk_header)}, 1, {0}};
  for (uint32_t index = 0; index < 32; ++index)
    bad_digest.expected_sha256[index] = invalid_digest[index];
  assert(qak_runtime_load_rpk(runtime, &bad_digest).status ==
         QAK_STATUS_PACKAGE_INVALID);

  const qak_rpk_source_t traversal = {
      sizeof(qak_rpk_source_t), QAK_RPK_SOURCE_PATH,
      {traversal_path, sizeof(traversal_path)}, 0, {0}};
  assert(qak_runtime_load_rpk(runtime, &traversal).status ==
         QAK_STATUS_PACKAGE_INVALID);

  const qak_rpk_source_t source = {
      sizeof(qak_rpk_source_t), QAK_RPK_SOURCE_MEMORY,
      {rpk_header, sizeof(rpk_header)}, 0, {0}};
  assert(qak_runtime_load_rpk(runtime, &source).status == QAK_STATUS_OK);
  const qak_surface_t surface = {sizeof(qak_surface_t), 7, 240, 240, 0};
  assert(qak_runtime_attach_surface(runtime, &surface).status == QAK_STATUS_OK);
  const qak_input_t pointer = {
      sizeof(qak_input_t), QAK_INPUT_POINTER, QAK_INPUT_DOWN, 10, 20, 0, 0,
      1, {NULL, 0}};
  assert(qak_runtime_dispatch_input(runtime, &pointer).status == QAK_STATUS_OK);
  assert(qak_runtime_update_lifecycle(runtime, QAK_LIFECYCLE_SURFACE_SHOW)
             .status == QAK_STATUS_OK);
  assert(qak_runtime_pump(runtime).status == QAK_STATUS_OK);
  assert(qak_runtime_destroy(runtime).status == QAK_STATUS_OK);
  assert(context.loads == 1 && context.attaches == 1 && context.inputs == 1 &&
         context.lifecycle == 1 && context.pumps == 1 && context.destroys == 1);
  puts("QuickApp Kit C ABI tests passed");
  return 0;
}
