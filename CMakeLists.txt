cmake_minimum_required(VERSION 3.24)
project(quickapp_runtime_lvgl LANGUAGES C CXX)

option(QUICKAPP_LVGL_BUILD_TESTS "Build LVGL contract tests" ON)
option(QUICKAPP_LVGL_BUILD_S02 "Build LV-S02 Runtime Host" ON)
option(QUICKAPP_LVGL_BUILD_S03 "Build LV-S03 Surface Host" ON)
option(QUICKAPP_LVGL_BUILD_S04 "Build LV-S04 Mount Host Components" ON)
option(QUICKAPP_LVGL_BUILD_S06 "Build LV-S06 Font Measure" ON)
option(QUICKAPP_LVGL_BUILD_SIMULATOR "Build SDL/libuv simulator backends" ON)
option(QUICKAPP_LVGL_EMBEDDED_MIN "Build the LVGL runtime without SDL, filesystem, or diagnostics" OFF)
option(QUICKAPP_LVGL_ENABLE_ASAN "Enable AddressSanitizer and UBSan" OFF)
option(QUICKAPP_LVGL_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(QUICKAPP_LVGL_ENABLE_ASAN AND QUICKAPP_LVGL_ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan must use separate builds")
endif()

if(QUICKAPP_LVGL_EMBEDDED_MIN)
  set(QUICKAPP_LVGL_BUILD_SIMULATOR OFF CACHE BOOL
      "Build SDL/libuv simulator backends" FORCE)
endif()

set(QUICKAPP_LVGL_WARNINGS -Wall -Wextra -Wpedantic -Werror)
set(QUICKAPP_LVGL_SANITIZERS "")
if(QUICKAPP_LVGL_ENABLE_ASAN)
  set(QUICKAPP_LVGL_SANITIZERS
      -fsanitize=address,undefined -fno-omit-frame-pointer)
elseif(QUICKAPP_LVGL_ENABLE_TSAN)
  set(QUICKAPP_LVGL_SANITIZERS -fsanitize=thread -fno-omit-frame-pointer)
endif()

function(quickapp_lvgl_configure target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_compile_options(${target} PRIVATE
    ${QUICKAPP_LVGL_WARNINGS} ${QUICKAPP_LVGL_SANITIZERS})
  target_link_options(${target} PRIVATE ${QUICKAPP_LVGL_SANITIZERS})
endfunction()

add_library(quickapp_lvgl_foundation STATIC
  src/backend_lifecycle.cpp
  src/owner_task_queue.cpp
)
target_include_directories(quickapp_lvgl_foundation PUBLIC include)
quickapp_lvgl_configure(quickapp_lvgl_foundation)
target_compile_options(quickapp_lvgl_foundation PRIVATE
  -fno-exceptions -fno-rtti)

add_library(quickapp_lvgl_foundation_fakes STATIC fakes/src/fakes.cpp)
target_include_directories(quickapp_lvgl_foundation_fakes
  PUBLIC include fakes/include)
target_link_libraries(quickapp_lvgl_foundation_fakes
  PUBLIC quickapp_lvgl_foundation)
quickapp_lvgl_configure(quickapp_lvgl_foundation_fakes)
target_compile_options(quickapp_lvgl_foundation_fakes PRIVATE
  -fno-exceptions -fno-rtti)

if(QUICKAPP_LVGL_BUILD_S02)
  set(QUICKAPP_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(QUICKAPP_CORE_ENABLE_ASAN ${QUICKAPP_LVGL_ENABLE_ASAN} CACHE BOOL "" FORCE)
  set(QUICKAPP_CORE_ENABLE_TSAN ${QUICKAPP_LVGL_ENABLE_TSAN} CACHE BOOL "" FORCE)
  add_subdirectory(../quickapp-runtime-core
                   ${CMAKE_CURRENT_BINARY_DIR}/quickapp-runtime-core)

  set(QUICKAPP_JS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(QUICKAPP_JS_BUILD_QUICKJS_PROVIDER ON CACHE BOOL "" FORCE)
  set(QUICKAPP_JS_ENABLE_ASAN ${QUICKAPP_LVGL_ENABLE_ASAN} CACHE BOOL "" FORCE)
  set(QUICKAPP_JS_ENABLE_UBSAN ${QUICKAPP_LVGL_ENABLE_ASAN} CACHE BOOL "" FORCE)
  set(QUICKAPP_JS_ENABLE_TSAN ${QUICKAPP_LVGL_ENABLE_TSAN} CACHE BOOL "" FORCE)
  add_subdirectory(../quickapp-runtime-js
                   ${CMAKE_CURRENT_BINARY_DIR}/quickapp-runtime-js)

  add_library(quickapp_lvgl_runtime_host STATIC
    src/composition.cpp
    src/package_source.cpp
    src/runtime_host.cpp
    src/runtime_types.cpp
    src/trace_adapter.cpp
  )
  target_include_directories(quickapp_lvgl_runtime_host PUBLIC include)
  target_link_libraries(quickapp_lvgl_runtime_host PUBLIC
    quickapp_lvgl_foundation
    quickapp_core_foundation
    quickapp_js_engine_api)
  quickapp_lvgl_configure(quickapp_lvgl_runtime_host)

  add_library(quickapp_lvgl_embedded_backends STATIC
    src/backends/embedded_backends.cpp)
  target_include_directories(quickapp_lvgl_embedded_backends PUBLIC include)
  target_link_libraries(quickapp_lvgl_embedded_backends
    PUBLIC quickapp_lvgl_runtime_host)
  quickapp_lvgl_configure(quickapp_lvgl_embedded_backends)

  if(QUICKAPP_LVGL_BUILD_S04 OR QUICKAPP_LVGL_BUILD_S06)
    add_library(quickapp_lvgl_font_asset STATIC
      src/font/system_default_font_asset.cpp)
    target_include_directories(quickapp_lvgl_font_asset PUBLIC include)
    quickapp_lvgl_configure(quickapp_lvgl_font_asset)
  endif()

  if(QUICKAPP_LVGL_BUILD_S03)
    set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH
        "LVGL configuration for QuickApp Kit" FORCE)
    set(LV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
    if(QUICKAPP_LVGL_BUILD_SIMULATOR)
      set(CONFIG_LV_USE_SDL 1 CACHE STRING "Enable LVGL SDL simulator driver" FORCE)
    else()
      set(CONFIG_LV_USE_SDL 0 CACHE STRING "Disable LVGL SDL simulator driver" FORCE)
    endif()
    add_subdirectory(../source/third_party/lvgl
                     ${CMAKE_CURRENT_BINARY_DIR}/lvgl EXCLUDE_FROM_ALL)
    if(QUICKAPP_LVGL_BUILD_SIMULATOR)
      target_compile_definitions(lvgl PUBLIC CONFIG_LV_USE_SDL=1)
    endif()

    add_library(quickapp_lvgl_surface_host STATIC
      src/surface/surface_host.cpp
      src/surface/lvgl_page_root_backend.cpp)
    target_include_directories(quickapp_lvgl_surface_host PUBLIC include)
    target_link_libraries(quickapp_lvgl_surface_host PUBLIC
      quickapp_lvgl_foundation quickapp_core_foundation lvgl::lvgl)
    quickapp_lvgl_configure(quickapp_lvgl_surface_host)

    add_library(quickapp_lvgl_feature_provider STATIC
      src/feature/lvgl_feature_provider.cpp)
    target_include_directories(quickapp_lvgl_feature_provider PUBLIC include)
    target_link_libraries(quickapp_lvgl_feature_provider PUBLIC
      quickapp_core_features quickapp_lvgl_surface_host lvgl::lvgl)
    quickapp_lvgl_configure(quickapp_lvgl_feature_provider)

    if(QUICKAPP_LVGL_BUILD_TESTS)
      add_executable(lv_b4_feature_provider_tests
        tests/lv_b4_feature_provider_tests.cpp)
      target_link_libraries(lv_b4_feature_provider_tests PRIVATE
        quickapp_lvgl_feature_provider lvgl)
      quickapp_lvgl_configure(lv_b4_feature_provider_tests)
      add_test(NAME lv_b4_feature_provider_tests
               COMMAND lv_b4_feature_provider_tests)
      set_tests_properties(lv_b4_feature_provider_tests PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")

      add_executable(lv_b6_url_provider_tests
        tests/lv_b6_url_provider_tests.cpp)
      target_link_libraries(lv_b6_url_provider_tests PRIVATE
        quickapp_lvgl_feature_provider lvgl)
      quickapp_lvgl_configure(lv_b6_url_provider_tests)
      add_test(NAME lv_b6_url_provider_tests
               COMMAND lv_b6_url_provider_tests)
      set_tests_properties(lv_b6_url_provider_tests PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")

    endif()

    if(QUICKAPP_LVGL_BUILD_S04)
      add_library(quickapp_lvgl_mount_host STATIC
        src/integration/core_mount_bridge.cpp
        src/mount/mount_types.cpp
        src/mount/mount_host.cpp
        src/mount/lvgl_mount_backend.cpp)
      target_include_directories(quickapp_lvgl_mount_host PUBLIC include)
      target_link_libraries(quickapp_lvgl_mount_host PUBLIC
        quickapp_lvgl_surface_host quickapp_lvgl_foundation
        quickapp_core_foundation quickapp_core_render
        quickapp_lvgl_font_asset lvgl::lvgl)
      quickapp_lvgl_configure(quickapp_lvgl_mount_host)

      if(QUICKAPP_LVGL_BUILD_TESTS)
        add_executable(lv_b35_tabs_mount_tests
          tests/lv_b35_tabs_mount_tests.cpp)
        target_link_libraries(lv_b35_tabs_mount_tests PRIVATE
          quickapp_lvgl_mount_host quickapp_lvgl_foundation_fakes lvgl)
        quickapp_lvgl_configure(lv_b35_tabs_mount_tests)
        add_test(NAME lv_b35_tabs_mount_tests
                 COMMAND lv_b35_tabs_mount_tests)
        set_tests_properties(lv_b35_tabs_mount_tests PROPERTIES
          ENVIRONMENT "SDL_VIDEODRIVER=dummy")
      endif()
    endif()
  endif()

  if(QUICKAPP_LVGL_BUILD_S06)
    add_library(quickapp_lvgl_font_measure STATIC
      src/measure/font_metrics.cpp
      src/measure/font_measure.cpp)
    target_include_directories(quickapp_lvgl_font_measure PUBLIC include)
    target_link_libraries(quickapp_lvgl_font_measure PUBLIC
      quickapp_lvgl_foundation quickapp_core_foundation
      quickapp_lvgl_font_asset)
    quickapp_lvgl_configure(quickapp_lvgl_font_measure)
  endif()

  if(QUICKAPP_LVGL_BUILD_SIMULATOR)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUV REQUIRED IMPORTED_TARGET libuv)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)

    add_library(quickapp_lvgl_libuv_backends STATIC
      src/backends/libuv_file_package_source.cpp
      src/backends/libuv_loop_backend.cpp)
    target_include_directories(quickapp_lvgl_libuv_backends PUBLIC include)
    target_link_libraries(quickapp_lvgl_libuv_backends
      PUBLIC quickapp_lvgl_runtime_host PkgConfig::LIBUV)
    quickapp_lvgl_configure(quickapp_lvgl_libuv_backends)

    add_library(quickapp_lvgl_sdl_backends STATIC
      src/backends/sdl_backends.cpp
      src/event/input_adapter.cpp)
    target_include_directories(quickapp_lvgl_sdl_backends PUBLIC include)
    target_link_libraries(quickapp_lvgl_sdl_backends
      PUBLIC quickapp_lvgl_runtime_host quickapp_lvgl_mount_host PkgConfig::SDL3)
    quickapp_lvgl_configure(quickapp_lvgl_sdl_backends)
  endif()
endif()

if(QUICKAPP_LVGL_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)

  add_executable(lv_s01_contract_tests tests/lv_s01_contract_tests.cpp)
  target_link_libraries(lv_s01_contract_tests PRIVATE
    quickapp_lvgl_foundation_fakes Threads::Threads)
  quickapp_lvgl_configure(lv_s01_contract_tests)
  target_compile_options(lv_s01_contract_tests PRIVATE
    -fno-exceptions -fno-rtti)
  add_test(NAME lv_s01_contract_tests COMMAND lv_s01_contract_tests)
  add_test(
    NAME lv_s01_foundation_boundary_scan
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_foundation_boundaries.cmake)

  if(QUICKAPP_LVGL_BUILD_S02)
    add_executable(lv_s02_embedded_isolated_probe
      tests/lv_s02_embedded_isolated_probe.cpp)
    target_link_libraries(lv_s02_embedded_isolated_probe PRIVATE
      quickapp_lvgl_embedded_backends
      quickapp_js_engine_quickjs)
    quickapp_lvgl_configure(lv_s02_embedded_isolated_probe)
    add_test(NAME lv_s02_embedded_isolated_probe
             COMMAND lv_s02_embedded_isolated_probe)

    if(QUICKAPP_LVGL_BUILD_SIMULATOR)
      add_executable(lv_s02_contract_tests tests/lv_s02_contract_tests.cpp)
      target_link_libraries(lv_s02_contract_tests PRIVATE
        quickapp_lvgl_foundation_fakes
        quickapp_lvgl_embedded_backends
        quickapp_lvgl_libuv_backends
        quickapp_lvgl_sdl_backends
        quickapp_js_engine_quickjs
        Threads::Threads)
      quickapp_lvgl_configure(lv_s02_contract_tests)
      add_test(NAME lv_s02_contract_tests COMMAND lv_s02_contract_tests)
      set_tests_properties(lv_s02_contract_tests PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")

      add_executable(lv_s02_simulator_isolated_probe
        tests/lv_s02_simulator_isolated_probe.cpp)
      target_link_libraries(lv_s02_simulator_isolated_probe PRIVATE
        quickapp_lvgl_libuv_backends
        quickapp_lvgl_sdl_backends
        quickapp_js_engine_quickjs)
      quickapp_lvgl_configure(lv_s02_simulator_isolated_probe)
      add_test(NAME lv_s02_simulator_isolated_probe
               COMMAND lv_s02_simulator_isolated_probe)

      add_test(
        NAME lv_s02_boundary_and_trimming_scan
        COMMAND ${CMAKE_COMMAND}
          -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
          -DEMBEDDED_PROBE=$<TARGET_FILE:lv_s02_embedded_isolated_probe>
          -DSIMULATOR_PROBE=$<TARGET_FILE:lv_s02_simulator_isolated_probe>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_lv_s02_boundaries.cmake)
    endif()

  endif()

  if(QUICKAPP_LVGL_BUILD_S03)
    add_executable(lv_s03_contract_tests tests/lv_s03_contract_tests.cpp)
    target_link_libraries(lv_s03_contract_tests PRIVATE
      quickapp_lvgl_surface_host quickapp_lvgl_foundation_fakes
      Threads::Threads)
    quickapp_lvgl_configure(lv_s03_contract_tests)
    add_test(NAME lv_s03_contract_tests COMMAND lv_s03_contract_tests)
    add_test(
      NAME lv_s03_boundary_scan
      COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_lv_s03_boundaries.cmake)
  endif()

  if(QUICKAPP_LVGL_BUILD_S04)
    if(QUICKAPP_LVGL_BUILD_S06)
      add_executable(lv_s04_font_profile_probe
        tests/lv_s04_font_profile_probe.cpp)
      target_link_libraries(lv_s04_font_profile_probe PRIVATE
        quickapp_lvgl_font_measure quickapp_lvgl_font_asset lvgl::lvgl)
      if(QUICKAPP_LVGL_BUILD_SIMULATOR)
        target_compile_definitions(lv_s04_font_profile_probe PRIVATE
          QUICKAPP_LVGL_EMBEDDED_PROFILE=0)
      else()
        target_compile_definitions(lv_s04_font_profile_probe PRIVATE
          QUICKAPP_LVGL_EMBEDDED_PROFILE=1)
      endif()
      quickapp_lvgl_configure(lv_s04_font_profile_probe)
      add_test(NAME lv_s04_font_profile_probe
        COMMAND lv_s04_font_profile_probe)
    endif()
    if(QUICKAPP_LVGL_BUILD_SIMULATOR)
      add_executable(lv_s04_mount_contract_tests tests/lv_s04_mount_contract_tests.cpp)
      target_link_libraries(lv_s04_mount_contract_tests PRIVATE
        quickapp_lvgl_mount_host quickapp_lvgl_surface_host
        quickapp_lvgl_font_measure quickapp_lvgl_foundation_fakes
        Threads::Threads lvgl::lvgl
        PkgConfig::SDL3)
      target_compile_definitions(lv_s04_mount_contract_tests PRIVATE
        CONFIG_LV_USE_SDL=1)
      quickapp_lvgl_configure(lv_s04_mount_contract_tests)
      add_test(NAME lv_s04_mount_contract_tests COMMAND lv_s04_mount_contract_tests)
      set_tests_properties(lv_s04_mount_contract_tests PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")

      add_executable(lv_s04_core_mount_integration_tests
        tests/lv_s04_core_mount_integration_tests.cpp)
      target_link_libraries(lv_s04_core_mount_integration_tests PRIVATE
        quickapp_lvgl_mount_host quickapp_lvgl_surface_host
        quickapp_lvgl_foundation_fakes quickapp_core_render_fakes
        Threads::Threads lvgl::lvgl PkgConfig::SDL3)
      target_compile_definitions(lv_s04_core_mount_integration_tests PRIVATE
        CONFIG_LV_USE_SDL=1)
      quickapp_lvgl_configure(lv_s04_core_mount_integration_tests)
      add_test(NAME lv_s04_core_mount_integration_tests
        COMMAND lv_s04_core_mount_integration_tests)
      set_tests_properties(lv_s04_core_mount_integration_tests PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endif()
    add_test(
      NAME lv_s04_boundary_scan
      COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_lv_s04_boundaries.cmake)
  endif()

  if(QUICKAPP_LVGL_BUILD_S06)
    add_executable(lv_s06_contract_tests tests/lv_s06_contract_tests.cpp)
    target_link_libraries(lv_s06_contract_tests PRIVATE
      quickapp_lvgl_font_measure quickapp_lvgl_foundation_fakes
      Threads::Threads)
    quickapp_lvgl_configure(lv_s06_contract_tests)
    add_test(NAME lv_s06_contract_tests COMMAND lv_s06_contract_tests)
    add_test(
      NAME lv_s06_boundary_scan
      COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_lv_s06_boundaries.cmake)
  endif()
endif()
