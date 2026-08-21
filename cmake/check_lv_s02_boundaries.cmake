file(GLOB_RECURSE HOST_AND_EMBEDDED_FILES
  "${SOURCE_ROOT}/include/quickapp/lvgl/runtime/*.h"
  "${SOURCE_ROOT}/include/quickapp/lvgl/backends/embedded_backends.h"
  "${SOURCE_ROOT}/src/composition.cpp"
  "${SOURCE_ROOT}/src/package_source.cpp"
  "${SOURCE_ROOT}/src/runtime_host.cpp"
  "${SOURCE_ROOT}/src/runtime_types.cpp"
  "${SOURCE_ROOT}/src/trace_adapter.cpp"
  "${SOURCE_ROOT}/src/backends/embedded_backends.cpp")

set(BACKEND_LEAK_PATTERNS
  "#include[ \t]*[<\"]SDL"
  "#include[ \t]*[<\"]uv\\.h"
  "SDL_[A-Za-z0-9_]*"
  "uv_(loop|async|timer|fs|run|stop)[A-Za-z0-9_]*"
  "lv_obj_t")

foreach(FILE_PATH IN LISTS HOST_AND_EMBEDDED_FILES)
  file(READ "${FILE_PATH}" CONTENT)
  foreach(PATTERN IN LISTS BACKEND_LEAK_PATTERNS)
    if(CONTENT MATCHES "${PATTERN}")
      message(FATAL_ERROR "LV-S02 backend leak in ${FILE_PATH}: ${PATTERN}")
    endif()
  endforeach()
endforeach()

file(GLOB_RECURSE S02_PRODUCT_FILES
  "${SOURCE_ROOT}/include/quickapp/lvgl/runtime/*.h"
  "${SOURCE_ROOT}/include/quickapp/lvgl/backends/*.h"
  "${SOURCE_ROOT}/src/composition.cpp"
  "${SOURCE_ROOT}/src/package_source.cpp"
  "${SOURCE_ROOT}/src/runtime_host.cpp"
  "${SOURCE_ROOT}/src/runtime_types.cpp"
  "${SOURCE_ROOT}/src/trace_adapter.cpp"
  "${SOURCE_ROOT}/src/backends/*.cpp")

set(FUTURE_SEMANTIC_PATTERNS
  "MountTransaction"
  "PlatformInputMessage"
  "MeasureRequest"
  "ShowToast"
  "DeviceGetInfo"
  "lv_obj_t"
  "while[ \t]*\\(true\\)"
  "while[ \t\r\n]*\\([^)]*test_and_set")

foreach(FILE_PATH IN LISTS S02_PRODUCT_FILES)
  file(READ "${FILE_PATH}" CONTENT)
  foreach(PATTERN IN LISTS FUTURE_SEMANTIC_PATTERNS)
    if(CONTENT MATCHES "${PATTERN}")
      message(FATAL_ERROR "LV-S02 scope violation in ${FILE_PATH}: ${PATTERN}")
    endif()
  endforeach()
endforeach()

execute_process(COMMAND /usr/bin/otool -L "${EMBEDDED_PROBE}"
  RESULT_VARIABLE EMBEDDED_RESULT OUTPUT_VARIABLE EMBEDDED_LINKS)
if(NOT EMBEDDED_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not inspect embedded isolated probe")
endif()
if(EMBEDDED_LINKS MATCHES "libuv|libSDL")
  message(FATAL_ERROR "Embedded profile links simulator dependency:\n${EMBEDDED_LINKS}")
endif()

execute_process(COMMAND /usr/bin/otool -L "${SIMULATOR_PROBE}"
  RESULT_VARIABLE SIMULATOR_RESULT OUTPUT_VARIABLE SIMULATOR_LINKS)
if(NOT SIMULATOR_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not inspect simulator isolated probe")
endif()
if(NOT SIMULATOR_LINKS MATCHES "libuv")
  message(FATAL_ERROR "Simulator probe does not link libuv:\n${SIMULATOR_LINKS}")
endif()
if(NOT SIMULATOR_LINKS MATCHES "libSDL3")
  message(FATAL_ERROR "Simulator probe does not link SDL3:\n${SIMULATOR_LINKS}")
endif()

message(STATUS "LV-S02 boundary and trimming scan passed")
