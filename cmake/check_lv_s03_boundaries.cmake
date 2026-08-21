file(GLOB S03_PUBLIC_FILES
  "${SOURCE_ROOT}/include/quickapp/lvgl/surface/*.h")
set(S03_HOST_FILES
  "${SOURCE_ROOT}/src/surface/surface_host.cpp")
set(S03_BACKEND_FILES
  "${SOURCE_ROOT}/src/surface/lvgl_page_root_backend.cpp")

set(S03_FORBIDDEN_PATTERNS
  "#include[ \\t]*[<\\\"]SDL"
  "#include[ \\t]*[<\\\"]uv\\.h"
  "SDL_[A-Za-z0-9_]*"
  "uv_(loop|async|timer|run|stop)[A-Za-z0-9_]*"
  "MountTransaction"
  "PlatformInputMessage"
  "MeasureRequest"
  "Yoga"
  "Navigation"
  "Revision"
  "while[ \\t]*\\(true\\)"
  "while[ \\t\\r\\n]*\\([^)]*test_and_set")

foreach(FILE_PATH IN LISTS S03_PUBLIC_FILES S03_HOST_FILES S03_BACKEND_FILES)
  file(READ "${FILE_PATH}" CONTENT)
  foreach(PATTERN IN LISTS S03_FORBIDDEN_PATTERNS)
    if(CONTENT MATCHES "${PATTERN}")
      message(FATAL_ERROR "LV-S03 scope violation in ${FILE_PATH}: ${PATTERN}")
    endif()
  endforeach()
endforeach()

foreach(FILE_PATH IN LISTS S03_PUBLIC_FILES S03_HOST_FILES)
  file(READ "${FILE_PATH}" CONTENT)
  if(CONTENT MATCHES "#include[ \\t]*[<\\\"]lvgl(\\.h|/)")
    message(FATAL_ERROR "LV-S03 shared surface layer leaks LVGL in ${FILE_PATH}")
  endif()
  if(CONTENT MATCHES "lv_obj_t")
    message(FATAL_ERROR "LV-S03 shared surface layer leaks lv_obj_t in ${FILE_PATH}")
  endif()
endforeach()

message(STATUS "LV-S03 boundary scan passed")
