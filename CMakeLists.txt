cmake_minimum_required(VERSION 3.16)
project(athrill3 VERSION 0.1.0 LANGUAGES C CXX)

# The Athrill sources are C11 with GNU extensions. GCC 15 defaults to C23, where
# bool is a keyword and std_types.h's typedef no longer compiles.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
include(GNUInstallDirs)
include(CTest)
find_package(Threads REQUIRED)

# The Athrill core (toppers/athrill, branch cpp) next to this checkout.
set(ATHRILL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../athrill" CACHE PATH "Athrill core checkout")
get_filename_component(ATHRILL_SOURCE_DIR "${ATHRILL_SOURCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# The common part of Athrill's Arm targets (hakoniwalab/athrill-arm-common) next to this checkout.
set(ATHRILL_ARM_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../athrill-arm-common" CACHE PATH "athrill-arm-common checkout")
get_filename_component(ATHRILL_ARM_COMMON_DIR "${ATHRILL_ARM_COMMON_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ATHRILL_ADDRESS_WIDTH 32 CACHE STRING "Athrill bus address width" FORCE)
include("${ATHRILL_SOURCE_DIR}/cmake/AthrillCore.cmake")
add_subdirectory(examples/host-functions)
set(ATHRILL_TARGET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
option(ATHRILL_ENABLE_EXDEV "Enable external device loading" ON)

# Batched execution (cpu_supply_clocks): the core runs up to this many instructions between device ticks.
add_compile_definitions(ATHRILL_TARGET_BATCH_CLOCKS=1024U)
add_library(athrill_stm32_config INTERFACE)
target_compile_definitions(athrill_stm32_config INTERFACE
    CPUEMU_CLOCK_BUG_FIX SEARCH_REGION_OPTIMIZE DISABLE_BUS_ACCESS_LOG
    OPTIMIZE_USE_ONLY_1CPU MINIMUM_DEVICE_CONFIG DISABLE_DEVICE_VDEV DISABLE_CAN)
if(ATHRILL_ENABLE_EXDEV)
  target_compile_definitions(athrill_stm32_config INTERFACE EXDEV_ENABLE)
endif()
target_link_libraries(athrill_stm32_config INTERFACE Threads::Threads m ${CMAKE_DL_LIBS})

set(STM32_INCLUDES
    "${ATHRILL_SRC_DIR}" "${ATHRILL_SRC_DIR}/lib/dwarf" "${ATHRILL_SRC_DIR}/bus"
    "${ATHRILL_SRC_DIR}/cpu" "${ATHRILL_SRC_DIR}/device/mpu"
    "${ATHRILL_SRC_DIR}/debugger/interaction" "${ATHRILL_SRC_DIR}/debugger/interaction/inc"
    "${ATHRILL_SRC_DIR}/debugger/executor" "${ATHRILL_SRC_DIR}/main"
    "${ATHRILL_TARGET_SOURCE_DIR}/cpu" "${ATHRILL_TARGET_SOURCE_DIR}/cpu/config"
    "${ATHRILL_TARGET_SOURCE_DIR}/cpu/decoder" "${ATHRILL_ARM_COMMON_DIR}/include"
    "${ATHRILL_TARGET_SOURCE_DIR}/device" "${ATHRILL_TARGET_SOURCE_DIR}/device/peripheral"
    "${ATHRILL_TARGET_SOURCE_DIR}/debugger/interaction/inc")
function(stm32_target name)
  athrill_apply_common_settings(${name})
  target_include_directories(${name} PRIVATE ${STM32_INCLUDES})
  target_link_libraries(${name} PRIVATE athrill_stm32_config)
endfunction()

add_library(athrill_main STATIC ${ATHRILL_MAIN_SOURCES})
stm32_target(athrill_main)
target_link_libraries(athrill_main PUBLIC ${ATHRILL_HAKONIWA_LIBRARIES})

add_library(athrill_cui STATIC ${ATHRILL_CUI_SOURCES})
target_include_directories(athrill_cui PRIVATE "${ATHRILL_SRC_DIR}/debugger/executor/concrete_executor/target")
stm32_target(athrill_cui)

# Source-based code coverage of the CPU (clang), see tools/coverage.sh.
option(ATHRILL_STM32_COVERAGE "Build the CPU with code coverage" OFF)

add_library(athrill_stm32_cpu STATIC
  src/cpu/config/cpu_config.c
  src/cpu/host_function_arm.c
  src/cpu/idle_spin_halt_arm.c
  src/cpu/thumb_exec.c
  src/cpu/thumb_fast.c
  src/cpu/memory_window.c
  src/cpu/thumb_exec16.c
  src/cpu/thumb_exec32.c
  src/cpu/thumb_gen16.c
  src/cpu/thumb_gen32.c
  src/cpu/decoder/t32_decode16.c
  src/cpu/decoder/t32_decode32.c
  "${ATHRILL_SRC_DIR}/lib/dbg_log.c")
stm32_target(athrill_stm32_cpu)
if(ATHRILL_STM32_COVERAGE)
  target_compile_options(athrill_stm32_cpu PRIVATE -fprofile-instr-generate -fcoverage-mapping -O0)
endif()

set(DEVICE_SOURCES ${ATHRILL_DEVICE_COMMON_SOURCES} ${ATHRILL_DEVICE_THREAD_SOURCES}
  src/device/device.c src/device/peripheral/stm32_uart.c
  src/device/peripheral/stm32_uart_tcp.c
  src/device/peripheral/arm_m_scs_device.c
  src/device/peripheral/arm_m_dwt.c
  src/device/peripheral/stm32_periph_probe.c
  src/device/peripheral/stm32h7_boot.c
  src/device/peripheral/stm32_tim.c
  src/device/peripheral/stm32_gpio.c
  src/device/peripheral/stm32_spi.c
  src/device/peripheral/stm32h7_spi.c
  src/device/peripheral/stm32_dmamux.c
  src/device/sensor/sensor_source.c
  src/device/sensor/ms5611.c
  src/device/sensor/icm20689.c
  src/device/sensor/icm42688p.c
  src/device/sensor/bmi055.c
  src/device/peripheral/stm32_exti.c
  src/device/peripheral/stm32_dma.c
  src/device/peripheral/stm32_i2c.c
  src/device/sensor/ist8310.c
  src/device/sensor/ublox.c
  src/device/memory/fm25v02.c
  src/device/memory/sdcard.c
  src/device/peripheral/stm32_sdmmc.c
  src/device/board/board_profile.c
  src/device/board/fmu_v6c.c
  src/device/board/board.c
  src/debugger/executor/concrete_executor/dbg_target_cpu.c
  src/debugger/executor/concrete_executor/dbg_target_serial.c)
if(ATHRILL_ENABLE_EXDEV)
  list(APPEND DEVICE_SOURCES ${ATHRILL_DEVICE_EXDEV_SOURCES} ${ATHRILL_DEVICE_EXDEV_POSIX_SOURCES})
endif()
add_library(athrill_stm32_device STATIC ${DEVICE_SOURCES})
target_include_directories(athrill_stm32_device PRIVATE
  "${ATHRILL_SRC_DIR}/device/peripheral/target" "${ATHRILL_SRC_DIR}/device/peripheral/serial/fifo"
  "${ATHRILL_APL_DIR}/include")
stm32_target(athrill_stm32_device)

add_library(athrill_bus STATIC ${ATHRILL_BUS_SOURCES})
stm32_target(athrill_bus)
add_library(athrill_loader STATIC ${ATHRILL_LOADER_SOURCES})
stm32_target(athrill_loader)
add_library(athrill_mpu STATIC ${ATHRILL_MPU_SOURCES} src/cpu/config/mpu_config.c)
target_include_directories(athrill_mpu PRIVATE "${ATHRILL_SRC_DIR}/device/peripheral/serial/fifo" "${ATHRILL_SRC_DIR}/lib/dwarf/data_type")
stm32_target(athrill_mpu)
set(STD_SOURCES ${ATHRILL_STD_COMMON_SOURCES})
if(NOT WIN32)
  list(APPEND STD_SOURCES ${ATHRILL_STD_POSIX_SOURCES})
endif()
add_library(athrill_std STATIC ${STD_SOURCES})
stm32_target(athrill_std)

add_executable(athrill3 "${ATHRILL_MAIN_ENTRY_SOURCE}")
stm32_target(athrill3)
target_link_libraries(athrill3 PRIVATE athrill_main athrill_cui athrill_stm32_cpu
  athrill_stm32_device athrill_bus athrill_loader athrill_mpu athrill_std athrill_stm32_config)
if(ATHRILL_HAKONIWA_RPATH)
  set_target_properties(athrill3 PROPERTIES BUILD_RPATH "${ATHRILL_HAKONIWA_RPATH}" INSTALL_RPATH "${ATHRILL_HAKONIWA_RPATH}")
endif()
if(ATHRILL_STM32_COVERAGE)
  target_link_options(athrill3 PRIVATE -fprofile-instr-generate)
endif()
install(TARGETS athrill3 RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(BUILD_TESTING)
  add_test(NAME athrill3-usage COMMAND athrill3)
  # Generated decoders on a few known instructions (docs/decoder.md).
  add_executable(decode_dump test/decoder/decode_dump.c
    src/cpu/decoder/t32_decode16.c src/cpu/decoder/t32_decode32.c)
  target_include_directories(decode_dump PRIVATE src/cpu/decoder)
  add_test(NAME decoder-known COMMAND decode_dump 4770 f000d000 f8d00004 fb91f0f2 eeb70bc0 f3ef8008)
  set_tests_properties(decoder-known PROPERTIES PASS_REGULAR_EXPRESSION
    "BX_T1.*BL_i_T1.*LDR_i_T3.*SDIV_T1.*VCVT_sd_T1.*MRS_T1_AS")
  # The timer model against RM0410, the emulator stubbed (test/device).
  add_executable(test_stm32_tim test/device/test_stm32_tim.c test/device/test_arm_m_dwt.c
    src/device/peripheral/stm32_tim.c src/device/peripheral/arm_m_dwt.c)
  stm32_target(test_stm32_tim)
  target_include_directories(test_stm32_tim PRIVATE src/device)
  target_link_libraries(test_stm32_tim PRIVATE athrill_stm32_config)
  add_test(NAME device-stm32-tim COMMAND test_stm32_tim)
  add_executable(test_stm32_uart test/device/test_stm32_uart.c src/device/peripheral/stm32_uart.c)
  stm32_target(test_stm32_uart)
  target_include_directories(test_stm32_uart PRIVATE src/device src/debugger/executor)
  target_link_libraries(test_stm32_uart PRIVATE athrill_stm32_config)
  add_test(NAME device-stm32-uart COMMAND test_stm32_uart)
  # USART transmit DMA with the real DMA model
  add_executable(test_uart_dma_tx test/device/test_uart_dma_tx.c src/device/peripheral/stm32_uart.c
    src/device/peripheral/stm32_dma.c)
  stm32_target(test_uart_dma_tx)
  target_include_directories(test_uart_dma_tx PRIVATE src/device src/debugger/executor)
  target_link_libraries(test_uart_dma_tx PRIVATE athrill_stm32_config)
  add_test(NAME device-uart-dma-tx COMMAND test_uart_dma_tx)
  add_executable(test_spi_ms5611 test/device/test_spi_ms5611.c src/device/peripheral/stm32_gpio.c
    src/device/peripheral/stm32_exti.c src/device/peripheral/stm32_spi.c src/device/sensor/ms5611.c)
  stm32_target(test_spi_ms5611)
  target_include_directories(test_spi_ms5611 PRIVATE src/device)
  target_link_libraries(test_spi_ms5611 PRIVATE athrill_stm32_config)
  add_test(NAME device-spi-ms5611 COMMAND test_spi_ms5611)
  add_executable(test_dma_icm20689 test/device/test_dma_icm20689.c src/device/peripheral/stm32_gpio.c
    src/device/peripheral/stm32_exti.c src/device/peripheral/stm32_spi.c src/device/peripheral/stm32h7_spi.c src/device/peripheral/stm32_dma.c
    src/device/sensor/icm20689.c)
  stm32_target(test_dma_icm20689)
  target_include_directories(test_dma_icm20689 PRIVATE src/device)
  target_link_libraries(test_dma_icm20689 PRIVATE athrill_stm32_config)
  add_test(NAME device-dma-icm20689 COMMAND test_dma_icm20689)
  add_executable(test_i2c_ist8310 test/device/test_i2c_ist8310.c src/device/peripheral/stm32_i2c.c
    src/device/sensor/ist8310.c)
  stm32_target(test_i2c_ist8310)
  target_include_directories(test_i2c_ist8310 PRIVATE src/device)
  target_link_libraries(test_i2c_ist8310 PRIVATE athrill_stm32_config)
  add_test(NAME device-i2c-ist8310 COMMAND test_i2c_ist8310)
  add_executable(test_ublox test/device/test_ublox.c src/device/sensor/ublox.c)
  stm32_target(test_ublox)
  target_include_directories(test_ublox PRIVATE src/device)
  target_link_libraries(test_ublox PRIVATE athrill_stm32_config)
  add_test(NAME device-ublox COMMAND test_ublox)
  add_executable(test_fm25v02 test/device/test_fm25v02.c src/device/memory/fm25v02.c)
  stm32_target(test_fm25v02)
  target_include_directories(test_fm25v02 PRIVATE src/device)
  target_link_libraries(test_fm25v02 PRIVATE athrill_stm32_config)
  add_test(NAME device-fm25v02 COMMAND test_fm25v02)
  add_executable(test_sdmmc_sdcard test/device/test_sdmmc_sdcard.c src/device/peripheral/stm32_sdmmc.c
    src/device/peripheral/stm32_dma.c src/device/memory/sdcard.c)
  stm32_target(test_sdmmc_sdcard)
  target_include_directories(test_sdmmc_sdcard PRIVATE src/device)
  target_link_libraries(test_sdmmc_sdcard PRIVATE athrill_stm32_config)
  add_test(NAME device-sdmmc-sdcard COMMAND test_sdmmc_sdcard)
  add_executable(test_stm32h7_boot test/device/test_stm32h7_boot.c
    src/device/peripheral/stm32h7_boot.c src/device/board/board_profile.c)
  stm32_target(test_stm32h7_boot)
  target_include_directories(test_stm32h7_boot PRIVATE src/device)
  target_link_libraries(test_stm32h7_boot PRIVATE athrill_stm32_config)
  add_test(NAME device-stm32h7-boot COMMAND test_stm32h7_boot)
  add_executable(test_stm32h7_spi test/device/test_stm32h7_spi.c
    src/device/peripheral/stm32h7_spi.c src/device/peripheral/stm32_spi.c
    src/device/peripheral/stm32_gpio.c src/device/peripheral/stm32_exti.c
    src/device/peripheral/stm32_dmamux.c src/device/memory/fm25v02.c)
  stm32_target(test_stm32h7_spi)
  target_include_directories(test_stm32h7_spi PRIVATE src/device)
  target_link_libraries(test_stm32h7_spi PRIVATE athrill_stm32_config)
  add_test(NAME device-stm32h7-spi COMMAND test_stm32h7_spi)
  add_executable(test_v6c_sensors test/device/test_v6c_sensors.c
    src/device/sensor/bmi055.c src/device/sensor/icm42688p.c src/device/sensor/ms5611.c)
  stm32_target(test_v6c_sensors)
  target_include_directories(test_v6c_sensors PRIVATE src/device)
  target_link_libraries(test_v6c_sensors PRIVATE athrill_stm32_config)
  add_test(NAME device-v6c-sensors COMMAND test_v6c_sensors)
  # The fast executors against the generic ones (src/cpu/thumb_fast.c).
  add_executable(test_thumb_fast test/cpu/test_thumb_fast.c src/cpu/thumb_fast.c src/cpu/thumb_exec16.c
    src/cpu/thumb_exec32.c src/cpu/thumb_gen16.c src/cpu/thumb_gen32.c
    src/cpu/decoder/t32_decode16.c src/cpu/decoder/t32_decode32.c)
  stm32_target(test_thumb_fast)
  add_test(NAME cpu-thumb-fast COMMAND test_thumb_fast)
  add_library(test_host_function_library SHARED test/cpu/test_host_function_library.c)
  target_include_directories(test_host_function_library PRIVATE "${ATHRILL_SRC_DIR}/inc")
  add_executable(test_host_function
    test/cpu/test_host_function.c
    src/cpu/host_function_arm.c
    "${ATHRILL_SRC_DIR}/main/athrill_host_function.c"
    "${ATHRILL_SRC_DIR}/lib/shared_library.c")
  stm32_target(test_host_function)
  target_compile_definitions(test_host_function PRIVATE
    TEST_HOST_LIBRARY="$<TARGET_FILE:test_host_function_library>")
  target_link_libraries(test_host_function PRIVATE ${CMAKE_DL_LIBS})
  add_dependencies(test_host_function test_host_function_library)
  add_test(NAME cpu-host-function COMMAND test_host_function)
  # Bare-metal hello world (test/hello, prebuilt ELF from the Docker toolchain).
  add_test(NAME hello-usart3
    COMMAND athrill3 -c1 -m "${CMAKE_CURRENT_SOURCE_DIR}/test/hello/memory.txt"
            -d "${CMAKE_CURRENT_SOURCE_DIR}/test/hello/device_config.txt"
            -t 100000 "${CMAKE_CURRENT_SOURCE_DIR}/test/hello/hello.elf")
  set_tests_properties(hello-usart3 PROPERTIES PASS_REGULAR_EXPRESSION "Hello, Athrill STM32!")
  # Instruction tests (test/insn): every encoding of the instructions run
  # through athrill-arm-common, compared with the output of the same
  # program on QEMU (test/insn/build.sh makes the ELFs and the expected output).
  foreach(t insn dp dp2 ls br mx sy dsp fpm fpa fpc exc)
    add_test(NAME insn-${t} COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test/insn/run.sh" $<TARGET_FILE:athrill3> ${t})
  endforeach()
  # Every instruction test also with the pseudo-code only (DEVICE_CONFIG_ARM_FP_MODE and _EXEC_MODE strict).
  foreach(t insn dp dp2 ls br mx sy dsp fpm fpa fpc exc)
    add_test(NAME insn-${t}-strict COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test/insn/run.sh" $<TARGET_FILE:athrill3> ${t} strict)
  endforeach()
endif()
