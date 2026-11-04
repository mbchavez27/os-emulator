cmake_minimum_required(VERSION 3.16)
project(os-emulator VERSION 1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Kernel, config, and the pure report formatters; no console I/O. Shared by the CLI and checks.
add_library(emulator_core STATIC
  src/config/Config.cpp
  src/kernel/Instruction.cpp
  src/kernel/Process.cpp
  src/shell/ProcessReport.cpp
  src/shell/ProcessSmi.cpp
  src/kernel/Scheduler.cpp
)
target_include_directories(emulator_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(emulator_core PUBLIC Threads::Threads)

add_executable(os-emulator
  src/main.cpp
  src/shell/CommandHandler.cpp
  src/shell/AsciiArt.cpp
)
target_link_libraries(os-emulator PRIVATE emulator_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(scheduler-report-check
    checks/shell/SchedulerReportCheck.cpp
    src/shell/CommandHandler.cpp
    src/shell/AsciiArt.cpp
  )
  target_link_libraries(scheduler-report-check PRIVATE emulator_core)
  add_executable(process-smi-check checks/shell/ProcessSmiCheck.cpp)
  target_link_libraries(process-smi-check PRIVATE emulator_core)
  add_test(NAME process-smi COMMAND process-smi-check)
  set_tests_properties(process-smi PROPERTIES TIMEOUT 45)
  add_executable(process-screen-check
    checks/shell/ProcessScreenCheck.cpp
    src/shell/CommandHandler.cpp
    src/shell/AsciiArt.cpp
  )
  target_link_libraries(process-screen-check PRIVATE emulator_core)
  add_executable(scheduler-quantum-check checks/kernel/SchedulerQuantumCheck.cpp)
  target_link_libraries(scheduler-quantum-check PRIVATE emulator_core)
  add_test(NAME scheduler-quantum COMMAND scheduler-quantum-check)
  set_tests_properties(scheduler-quantum PROPERTIES TIMEOUT 45)
  add_executable(scheduler-generation-check checks/kernel/SchedulerGenerationCheck.cpp)
  target_link_libraries(scheduler-generation-check PRIVATE emulator_core)
  add_test(NAME scheduler-generation COMMAND scheduler-generation-check)
  set_tests_properties(scheduler-generation PROPERTIES TIMEOUT 120)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/report-check)
  configure_file(config.txt report-check/config.txt COPYONLY)
  add_test(NAME scheduler-report COMMAND scheduler-report-check)
  set_tests_properties(scheduler-report PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/report-check
  )
  add_test(NAME process-screen COMMAND process-screen-check)
  set_tests_properties(process-screen PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/report-check
    TIMEOUT 45
  )
endif()

# Compiler-specific warning flags
set(warning_targets emulator_core os-emulator)
if(BUILD_TESTING)
  list(APPEND warning_targets scheduler-report-check process-smi-check process-screen-check scheduler-quantum-check scheduler-generation-check)
endif()
foreach(target ${warning_targets})
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endforeach()

# Build + run in one step: cmake --build build --target run
# Run from the source dir so config.txt, logs/, and csopesy-log.txt resolve there.
add_custom_target(run
  COMMAND $<TARGET_FILE:os-emulator>
  DEPENDS os-emulator
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  USES_TERMINAL
  COMMENT "Build and run os-emulator"
)
