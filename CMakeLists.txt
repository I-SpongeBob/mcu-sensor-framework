cmake_minimum_required(VERSION 3.16)
project(sensorfw
        VERSION 1.0.0
        DESCRIPTION "Portable sensor framework for MCUs"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Target selection. The framework, the drivers, the filters and the application
# layer are identical for every target; only the port/ directory differs.
# ---------------------------------------------------------------------------
set(SENSORFW_TARGET "host" CACHE STRING "Target platform: host | stm32 | esp32")
set_property(CACHE SENSORFW_TARGET PROPERTY STRINGS host stm32 esp32)

option(SENSORFW_BUILD_TESTS   "Build the unit tests" ON)
option(SENSORFW_BUILD_EXAMPLE "Build the host demo"  ON)
option(SENSORFW_FREESTANDING  "Compile without exceptions and RTTI (MCU profile)" ON)

set(CMAKE_CXX_STANDARD 11)          # C++11: supported by every MCU toolchain
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Portable core: framework + filters + drivers + application components.
# Not one line in here is platform specific.
# ---------------------------------------------------------------------------
add_library(sensorfw
    src/core/types.cpp
    src/service/sensor_service.cpp
    drivers/ntc/ntc_thermistor.cpp
    drivers/lm75/lm75.cpp
    app/gui/temperature_view.cpp
    app/mqtt/mqtt_reporter.cpp
    app/logic/thermostat.cpp
)

target_include_directories(sensorfw PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Warning set applied to every target in the project, not just the library.
if(MSVC)
    set(SENSORFW_WARNINGS /W4)
else()
    set(SENSORFW_WARNINGS -Wall -Wextra -Wshadow -Wconversion -Wold-style-cast)
endif()
target_compile_options(sensorfw PRIVATE ${SENSORFW_WARNINGS})

# MinGW links libstdc++/libgcc/libwinpthread dynamically by default, so the
# executables only start from a shell that has the toolchain on PATH. Link them
# statically instead: the demos are meant to be double-clickable, and a
# firmware-oriented project has no business shipping a runtime DLL anyway.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(SENSORFW_LINK_OPTIONS -static -static-libgcc -static-libstdc++)
else()
    set(SENSORFW_LINK_OPTIONS "")
endif()

# The MCU profile: no exceptions, no RTTI, and therefore no hidden heap use.
# Kept on for the host build too, so the tests exercise the same code shape
# that runs on the device.
if(SENSORFW_FREESTANDING AND NOT MSVC)
    target_compile_options(sensorfw PUBLIC -fno-exceptions -fno-rtti)
endif()

if(SENSORFW_TARGET STREQUAL "stm32")
    target_compile_definitions(sensorfw PUBLIC SENSORFW_TARGET_STM32)
elseif(SENSORFW_TARGET STREQUAL "esp32")
    target_compile_definitions(sensorfw PUBLIC SENSORFW_TARGET_ESP32)
else()
    target_compile_definitions(sensorfw PUBLIC SENSORFW_TARGET_HOST)
endif()

# ---------------------------------------------------------------------------
# Host port: simulated ADC / I2C / display / MQTT / relay.
# On a real target this is replaced by port/stm32 or port/esp32.
# ---------------------------------------------------------------------------
if(SENSORFW_TARGET STREQUAL "host")
    add_library(sensorfw_port_host
        port/host/host_clock.cpp
        port/host/dashboard.cpp
        port/host/host_peripherals.cpp
        port/host/scenario.cpp
        port/host/simulated_adc.cpp
        port/host/simulated_i2c.cpp
    )
    target_link_libraries(sensorfw_port_host PUBLIC sensorfw)
    target_compile_options(sensorfw_port_host PRIVATE ${SENSORFW_WARNINGS})

    if(SENSORFW_BUILD_EXAMPLE)
        add_executable(host_demo examples/host_demo/main.cpp)
        target_link_libraries(host_demo PRIVATE sensorfw_port_host)
        target_compile_options(host_demo PRIVATE ${SENSORFW_WARNINGS})
        target_link_options(host_demo PRIVATE ${SENSORFW_LINK_OPTIONS})

        # Real-time terminal dashboard. Same stack, driven from the keyboard;
        # not part of ctest because it runs until the user quits.
        add_executable(live_demo examples/live_demo/main.cpp)
        target_link_libraries(live_demo PRIVATE sensorfw_port_host)
        target_compile_options(live_demo PRIVATE ${SENSORFW_WARNINGS})
        target_link_options(live_demo PRIVATE ${SENSORFW_LINK_OPTIONS})
    endif()

    if(SENSORFW_BUILD_TESTS)
        enable_testing()
        foreach(test_name test_filters test_publisher test_drivers test_service test_app)
            add_executable(${test_name} tests/${test_name}.cpp)
            target_link_libraries(${test_name} PRIVATE sensorfw_port_host)
            target_compile_options(${test_name} PRIVATE ${SENSORFW_WARNINGS})
            target_link_options(${test_name} PRIVATE ${SENSORFW_LINK_OPTIONS})
            add_test(NAME ${test_name} COMMAND ${test_name})
        endforeach()
    endif()
endif()
