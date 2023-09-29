cmake_minimum_required (VERSION 2.8.8)

project (Pinhão1.0)

execute_process (COMMAND llvm-config-3.7 --cxxflags --ldflags 
  OUTPUT_VARIABLE LLVM_CONFIG_N)

string (REPLACE "\n" " " LLVM_CONFIG "${LLVM_CONFIG_N}")

set (CMAKE_CXX_FLAGS "-O3 ${LLVM_CONFIG}")

add_library (PinhaoStaticProfiler SHARED 
  BlockEdgeFrequencyPass.cpp  
  BranchPredictionDot.cpp   
  BranchPredictionPass.cpp  
  StaticFunctionCost.cpp
  BranchHeuristicsInfo.cpp    
  BranchPredictionInfo.cpp)

