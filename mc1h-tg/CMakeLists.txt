cmake_minimum_required(VERSION 3.21)
project(mc1h_tg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MC1H_WITH_TDLIB "Build the TDLib console client (needs an installed TDLib 1.8.70)" OFF)
option(MC1H_BUILD_TESTS "Build unit tests" ON)
option(MC1H_SANITIZE "Build with AddressSanitizer + UBSan (GCC/Clang)" OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(MC1H_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# SQLite with FTS5. Linux: system package (libsqlite3-dev). Windows: vcpkg
# "sqlite3[fts5]" via CMAKE_TOOLCHAIN_FILE.
find_package(SQLite3 REQUIRED)

add_library(mc1h_core STATIC
  src/core/archive.cpp
  src/core/archive_recorder.cpp
  src/core/auth_flow.cpp
  src/core/ghost_client.cpp
  src/core/ghost_policy.cpp
  src/core/metadata_strip.cpp
  src/core/update_router.cpp
)
target_include_directories(mc1h_core PUBLIC src)
target_include_directories(mc1h_core SYSTEM PUBLIC third_party)
target_link_libraries(mc1h_core PUBLIC SQLite::SQLite3)

add_executable(mc1h_strip tools/mc1h_strip.cpp)
target_link_libraries(mc1h_strip PRIVATE mc1h_core)

if(MC1H_BUILD_TESTS)
  enable_testing()
  add_executable(mc1h_tests tests/test_main.cpp)
  target_link_libraries(mc1h_tests PRIVATE mc1h_core)
  add_test(NAME mc1h_tests COMMAND mc1h_tests)
endif()

if(MC1H_WITH_TDLIB)
  # Point CMAKE_PREFIX_PATH (or Td_DIR) at a TDLib install built from
  # third_party/td (commit 03220f6b, version 1.8.70).
  find_package(Td 1.8.70 EXACT CONFIG REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(mc1h_cli
    src/app/cli_main.cpp
    src/app/console.cpp
    src/app/td_json_transport.cpp
  )
  target_link_libraries(mc1h_cli PRIVATE mc1h_core Td::TdJson Threads::Threads)

  if(MC1H_BUILD_TESTS)
    add_executable(mc1h_td_smoke tests/td_smoke.cpp src/app/td_json_transport.cpp)
    target_link_libraries(mc1h_td_smoke PRIVATE mc1h_core Td::TdJson Threads::Threads)
    add_test(NAME mc1h_td_smoke COMMAND mc1h_td_smoke)
    set_tests_properties(mc1h_td_smoke PROPERTIES
      ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/lsan.supp")
  endif()
endif()
