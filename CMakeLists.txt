cmake_minimum_required(VERSION 3.16)
project(os-emulator VERSION 1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Domain logic with no UI dependency; shared by the CLI.
add_library(emulator_core STATIC
  src/Config.cpp
  src/Instruction.cpp
  src/Process.cpp
  src/ProcessReport.cpp
  src/Scheduler.cpp
)
target_include_directories(emulator_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(emulator_core PUBLIC Threads::Threads)

add_executable(os-emulator
  src/main.cpp
  src/CommandHandler.cpp
  src/AsciiArt.cpp
)
target_link_libraries(os-emulator PRIVATE emulator_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(scheduler-report-check
    checks/SchedulerReportCheck.cpp
    src/CommandHandler.cpp
    src/AsciiArt.cpp
  )
  target_link_libraries(scheduler-report-check PRIVATE emulator_core)
  add_executable(process-screen-check
    checks/ProcessScreenCheck.cpp
    src/CommandHandler.cpp
    src/AsciiArt.cpp
  )
  target_link_libraries(process-screen-check PRIVATE emulator_core)
  add_executable(scheduler-quantum-check checks/SchedulerQuantumCheck.cpp)
  target_link_libraries(scheduler-quantum-check PRIVATE emulator_core)
  add_test(NAME scheduler-quantum COMMAND scheduler-quantum-check)
  set_tests_properties(scheduler-quantum PROPERTIES TIMEOUT 45)
  add_executable(scheduler-generation-check checks/SchedulerGenerationCheck.cpp)
  target_link_libraries(scheduler-generation-check PRIVATE emulator_core)
  add_test(NAME scheduler-generation COMMAND scheduler-generation-check)
  set_tests_properties(scheduler-generation PROPERTIES TIMEOUT 45)
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
  list(APPEND warning_targets scheduler-report-check process-screen-check scheduler-quantum-check scheduler-generation-check)
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
