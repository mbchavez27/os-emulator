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

# Compiler-specific warning flags
foreach(target emulator_core os-emulator)
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
