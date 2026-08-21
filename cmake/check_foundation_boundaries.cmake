file(GLOB_RECURSE FOUNDATION_FILES
  "${SOURCE_ROOT}/include/quickapp/lvgl/foundation/*.h"
  "${SOURCE_ROOT}/src/backend_lifecycle.cpp"
  "${SOURCE_ROOT}/src/owner_task_queue.cpp"
  "${SOURCE_ROOT}/fakes/include/*.h"
  "${SOURCE_ROOT}/fakes/src/*.cpp"
)

set(FORBIDDEN_PATTERNS
  "#include[ \t]*[<\\\"]lvgl\\.h"
  "#include[ \t]*[<\\\"]SDL"
  "#include[ \t]*[<\\\"]uv\\.h"
  "#include[ \t]*[<\\\"]quickapp/(core|runtime)"
  "lv_obj_t"
  "SDL_[A-Za-z0-9_]*"
  "uv_(loop|async|timer|run|stop)[A-Za-z0-9_]*"
  "SurfaceId"
  "MountTransaction"
  "PlatformInputMessage"
  "while[ \t]*\\(true\\)"
  "while[ \t\r\n]*\\([^)]*test_and_set"
)

foreach(FILE_PATH IN LISTS FOUNDATION_FILES)
  file(READ "${FILE_PATH}" CONTENT)
  foreach(PATTERN IN LISTS FORBIDDEN_PATTERNS)
    if(CONTENT MATCHES "${PATTERN}")
      message(FATAL_ERROR
        "LV-S01 boundary violation in ${FILE_PATH}: ${PATTERN}")
    endif()
  endforeach()
endforeach()

message(STATUS "LV-S01 boundary scan passed")
