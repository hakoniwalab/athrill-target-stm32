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
set(ATHRILL_DEVICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../athrill-device" CACHE PATH
    "hakoniwalab/athrill-device checkout with the Devicetree framework")
get_filename_component(ATHRILL_DEVICE_DIR "${ATHRILL_DEVICE_DIR}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ATHRILL_DEVICE_AVAILABLE OFF)
if(EXISTS "${ATHRILL_DEVICE_DIR}/include/athrill_device_if.h")
  set(ATHRILL_DEVICE_AVAILABLE ON)
endif()
if(ATHRILL_DEVICE_AVAILABLE)
  add_subdirectory("${ATHRILL_DEVICE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/athrill-device")
endif()
if(NOT ATHRILL_DEVICE_AVAILABLE)
  message(FATAL_ERROR "athrill-device is required: it holds the STM32 models (targets/stm32)")
endif()
# The STM32 peripheral models live in athrill-device (hakoniwalab/athrill-device#13
# step 3) and are compiled here until they are athrill-device models (step 4).
set(STM32_MODELS_DIR "${ATHRILL_DEVICE_DIR}/targets/stm32")

# Batched execution (cpu_supply_clocks): the core runs up to this many instructions between device ticks.
add_compile_definitions(ATHRILL_TARGET_BATCH_CLOCKS=1024U)
add_library(athrill_stm32_config INTERFACE)
target_compile_definitions(athrill_stm32_config INTERFACE
    CPUEMU_CLOCK_BUG_FIX SEARCH_REGION_OPTIMIZE DISABLE_BUS_ACCESS_LOG
    OPTIMIZE_USE_ONLY_1CPU MINIMUM_DEVICE_CONFIG DISABLE_DEVICE_VDEV DISABLE_CAN)
target_link_libraries(athrill_stm32_config INTERFACE Threads::Threads m ${CMAKE_DL_LIBS})
if(ATHRILL_ENABLE_EXDEV)
  target_compile_definitions(athrill_stm32_config INTERFACE EXDEV_ENABLE)
endif()

# Hard-coded model values the SVD does not hold (athrill-device targets/stm32/config/stm32_*_config.h):
# a directory with your own copies of these headers comes before the defaults.
# The defaults live in their own directory, not next to the sources, because a
# quoted #include searches the source's directory before the include path.
set(ATHRILL_STM32_CONFIG_DIR "" CACHE PATH "Directory with user model configuration headers (stm32_*_config.h)")
set(STM32_INCLUDES)
if(ATHRILL_STM32_CONFIG_DIR)
  get_filename_component(ATHRILL_STM32_CONFIG_DIR "${ATHRILL_STM32_CONFIG_DIR}" ABSOLUTE)
  list(APPEND STM32_INCLUDES "${ATHRILL_STM32_CONFIG_DIR}")
endif()
list(APPEND STM32_INCLUDES "${STM32_MODELS_DIR}/config")
list(APPEND STM32_INCLUDES
    "${ATHRILL_SRC_DIR}" "${ATHRILL_SRC_DIR}/lib/dwarf" "${ATHRILL_SRC_DIR}/bus"
    "${ATHRILL_SRC_DIR}/cpu" "${ATHRILL_SRC_DIR}/device/mpu"
    "${ATHRILL_SRC_DIR}/debugger/interaction" "${ATHRILL_SRC_DIR}/debugger/interaction/inc"
    "${ATHRILL_SRC_DIR}/debugger/executor" "${ATHRILL_SRC_DIR}/main"
    "${ATHRILL_TARGET_SOURCE_DIR}/cpu" "${ATHRILL_TARGET_SOURCE_DIR}/cpu/config"
    "${ATHRILL_TARGET_SOURCE_DIR}/cpu/decoder" "${ATHRILL_ARM_COMMON_DIR}/include"
    "${ATHRILL_TARGET_SOURCE_DIR}/device" "${ATHRILL_TARGET_SOURCE_DIR}/device/peripheral"
    "${STM32_MODELS_DIR}"
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
  src/device/device.c
  src/device/peripheral/arm_m_scs_device.c
  src/device/peripheral/arm_m_dwt.c
  src/device/target_host.c
  src/device/sensor/sensor_source.c
  src/device/board/board_profile.c
  src/debugger/executor/concrete_executor/dbg_target_cpu.c
  src/debugger/executor/concrete_executor/dbg_target_serial.c)
list(APPEND DEVICE_SOURCES src/device/device_tree.c)
if(ATHRILL_ENABLE_EXDEV)
  list(APPEND DEVICE_SOURCES ${ATHRILL_DEVICE_EXDEV_SOURCES} ${ATHRILL_DEVICE_EXDEV_POSIX_SOURCES})
endif()
add_library(athrill_stm32_device STATIC ${DEVICE_SOURCES})
target_include_directories(athrill_stm32_device PRIVATE
  "${ATHRILL_SRC_DIR}/device/peripheral/target" "${ATHRILL_SRC_DIR}/device/peripheral/serial/fifo"
  "${ATHRILL_APL_DIR}/include")
stm32_target(athrill_stm32_device)
target_link_libraries(athrill_stm32_device PRIVATE athrill_device)

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
  # The DWT cycle counter (the timer is an athrill-device model).
  add_executable(test_arm_m_dwt test/device/test_dwt_main.c test/device/test_arm_m_dwt.c
    src/device/peripheral/arm_m_dwt.c)
  stm32_target(test_arm_m_dwt)
  target_include_directories(test_arm_m_dwt PRIVATE src/device)
  target_link_libraries(test_arm_m_dwt PRIVATE athrill_stm32_config)
  add_test(NAME device-arm-m-dwt COMMAND test_arm_m_dwt)
  add_executable(test_board_profile test/device/test_board_profile.c
    src/device/board/board_profile.c)
  stm32_target(test_board_profile)
  target_include_directories(test_board_profile PRIVATE src/device)
  target_link_libraries(test_board_profile PRIVATE athrill_stm32_config)
  add_test(NAME device-board-profile COMMAND test_board_profile)
  add_executable(test_v6c_sensors test/device/test_v6c_sensors.c
    test/fixtures/sensor/bmi055.c test/fixtures/sensor/icm42688p.c test/fixtures/sensor/ms5611.c)
  stm32_target(test_v6c_sensors)
  target_include_directories(test_v6c_sensors PRIVATE test/fixtures test/fixtures/peripheral test/fixtures/sensor test/fixtures/memory src/device src/device/sensor)
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
  # The bare-metal tests print on USART3 (the console by default). The USART
  # is an athrill-device model built from a Devicetree, so they run with the
  # Pixhawk 4 board (F7, USART3 enabled); nothing else of the board is used.
  set(STM32_TEST_DTB "${ATHRILL_DEVICE_DIR}/boards/pixhawk4/pixhawk4.dtb")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/test/hello/device_config.txt" _hello_config)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test-hello-config.txt"
    "${_hello_config}DEVICE_CONFIG_DEVICE_TREE ${STM32_TEST_DTB}\n")
  # Bare-metal hello world (test/hello, prebuilt ELF from the Docker toolchain).
  add_test(NAME hello-usart3
    COMMAND athrill3 -c1 -m "${CMAKE_CURRENT_SOURCE_DIR}/test/hello/memory.txt"
            -d "${CMAKE_CURRENT_BINARY_DIR}/test-hello-config.txt"
            -t 100000 "${CMAKE_CURRENT_SOURCE_DIR}/test/hello/hello.elf")
  set_tests_properties(hello-usart3 PROPERTIES PASS_REGULAR_EXPRESSION "Hello, Athrill STM32!")
  # Instruction tests (test/insn): every encoding of the instructions run
  # through athrill-arm-common, compared with the output of the same
  # program on QEMU (test/insn/build.sh makes the ELFs and the expected output).
  foreach(t insn dp dp2 ls br mx sy dsp fpm fpa fpc exc)
    add_test(NAME insn-${t} COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test/insn/run.sh" $<TARGET_FILE:athrill3> ${t})
    set_tests_properties(insn-${t} PROPERTIES ENVIRONMENT "INSN_DEVICE_TREE=${STM32_TEST_DTB}")
  endforeach()
  # Every instruction test also with the pseudo-code only (DEVICE_CONFIG_ARM_FP_MODE and _EXEC_MODE strict).
  foreach(t insn dp dp2 ls br mx sy dsp fpm fpa fpc exc)
    add_test(NAME insn-${t}-strict COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test/insn/run.sh" $<TARGET_FILE:athrill3> ${t} strict)
    set_tests_properties(insn-${t}-strict PROPERTIES ENVIRONMENT "INSN_DEVICE_TREE=${STM32_TEST_DTB}")
  endforeach()

  # Runtime files such as log.txt and model images belong to the build tree.
  # A separate directory per test also keeps parallel CTest runs independent.
  get_property(ATHRILL_STM32_TESTS DIRECTORY PROPERTY TESTS)
  foreach(test_name IN LISTS ATHRILL_STM32_TESTS)
    set(test_work_dir "${CMAKE_CURRENT_BINARY_DIR}/test-work/${test_name}")
    file(MAKE_DIRECTORY "${test_work_dir}")
    set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${test_work_dir}")
  endforeach()
endif()
