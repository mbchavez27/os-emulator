cmake_minimum_required(VERSION 3.16)
project(os-emulator VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(os-emulator
  src/main.cpp
  src/CommandHandler.cpp
  src/AsciiArt.cpp
)
target_include_directories(os-emulator PRIVATE include)
target_compile_options(os-emulator PRIVATE -Wall -Wextra)

# Build + run in one step: cmake --build build --target run
add_custom_target(run
  COMMAND os-emulator
  DEPENDS os-emulator
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
  COMMENT "Build and run os-emulator"
)
