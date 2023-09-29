project(EstimateCostPass)

cmake_minimum_required(VERSION 3.4.3)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

# STEP 1. Make sure that LLVMConfig.cmake _is_ on CMake's search path.
set(LT_LLVM_INSTALL_DIR "" CACHE PATH "LLVM installation directory")
set(LT_LLVM_CMAKE_CONFIG_DIR "${LT_LLVM_INSTALL_DIR}/lib/cmake/llvm/")
list(APPEND CMAKE_PREFIX_PATH "${LT_LLVM_CMAKE_CONFIG_DIR}")

# STEP 2. Load LLVM config from ... LLVMConfig.cmake.
find_package(LLVM 15.0.0 REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
if (NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# STEP 3. Define the plugin/pass/library.
add_library(EstimateCostPass SHARED pass.cc)
