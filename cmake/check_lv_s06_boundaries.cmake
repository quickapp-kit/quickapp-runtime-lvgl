file(GLOB S06_FILES
  "${SOURCE_ROOT}/include/quickapp/lvgl/measure/*.h"
  "${SOURCE_ROOT}/src/measure/*.cpp")

set(S06_FORBIDDEN_PATTERNS
  "#include[ \\t]*[<\\\"]lvgl"
  "#include[ \\t]*[<\\\"]SDL"
  "#include[ \\t]*[<\\\"]uv\\.h"
  "lv_obj_t"
  "SDL_[A-Za-z0-9_]*"
  "uv_(loop|async|timer|run|stop)[A-Za-z0-9_]*"
  "Yoga"
  "Host Tree"
  "MountTransaction"
  "PlatformInputMessage"
  "CoreMeasureCache"
  "while[ \\t]*\\(true\\)"
  "while[ \\t\\r\\n]*\\([^)]*test_and_set")

foreach(FILE_PATH IN LISTS S06_FILES)
  file(READ "${FILE_PATH}" CONTENT)
  foreach(PATTERN IN LISTS S06_FORBIDDEN_PATTERNS)
    if(CONTENT MATCHES "${PATTERN}")
      message(FATAL_ERROR "LV-S06 scope violation in ${FILE_PATH}: ${PATTERN}")
    endif()
  endforeach()
endforeach()

message(STATUS "LV-S06 boundary scan passed")
