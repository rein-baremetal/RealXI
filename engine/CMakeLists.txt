cmake_minimum_required(VERSION 3.18)
project(realxi_engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pure C++ game engine: physics, rules, hard-level AI, drills. No Android dependencies.
add_library(realxi_engine STATIC
  src/core.cpp src/match_core.cpp src/match_actions.cpp src/match_rules.cpp
  src/ai_team.cpp src/ai_decide.cpp src/drills.cpp src/api.cpp)
target_include_directories(realxi_engine PUBLIC include PRIVATE src)
set_target_properties(realxi_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(REALXI_BUILD_TOOLS "Build desktop self-play tool and tests" OFF)
if(REALXI_BUILD_TOOLS)
  add_executable(rx_sim tools/sim.cpp)
  target_link_libraries(rx_sim realxi_engine)
  add_executable(rx_tests tests/engine_tests.cpp)
  target_link_libraries(rx_tests realxi_engine)
  enable_testing()
  add_test(NAME engine_tests COMMAND rx_tests)
endif()
