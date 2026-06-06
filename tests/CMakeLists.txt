cmake_minimum_required(VERSION 3.14)
project(nrf24l01plus_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── ASAN flags ────────────────────────────────────────────────────────────────
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")

# ─── Fetch GoogleTest ─────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# ─── Test executable: RF setup register tests ──────────────────────────────────
add_executable(test_rf_setup test_rf_setup.cpp)
target_include_directories(test_rf_setup PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/nrf24l01plus/include)
target_link_libraries(test_rf_setup GTest::gtest_main)

# ─── Test executable: BLE dewhiten/swapbits tests ─────────────────────────────
add_executable(test_ble_dewhiten test_ble_dewhiten.cpp)
target_include_directories(test_ble_dewhiten PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/nrf24l01plus/include)
# Link ble.cpp which contains dewhiten(); swapbits() is inline in ble.h
target_sources(test_ble_dewhiten PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/nrf24l01plus/src/ble.cpp)
target_link_libraries(test_ble_dewhiten GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_rf_setup)
gtest_discover_tests(test_ble_dewhiten)