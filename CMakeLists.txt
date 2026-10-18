cmake_minimum_required(VERSION 3.20)
project(Driftwood VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(DRIFTWOOD_BUILD_GAME "Build the game executable (needs raylib)" ON)
option(DRIFTWOOD_BUILD_TESTS "Build headless simulation tests" ON)

# ---- simulation: pure C++, no graphics ----
add_library(driftwood_sim STATIC
  src/sim/SimData.cpp
  src/sim/SimWorld.cpp
  src/sim/SimPeople.cpp
  src/sim/SimProjects.cpp
  src/sim/SimJobs.cpp
  src/sim/SimNarrator.cpp
  src/sim/SimTick.cpp
  src/sim/SimSave.cpp
  src/sim/SimSites.cpp
  src/sim/SimTalk.cpp
  src/sim/SimLedger.cpp
  src/sim/SimHomes.cpp
  src/sim/SimStatus.cpp)
target_include_directories(driftwood_sim PUBLIC src)
if(MSVC)
  target_compile_options(driftwood_sim PRIVATE /W3 /utf-8)
else()
  target_compile_options(driftwood_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(DRIFTWOOD_BUILD_TESTS)
  enable_testing()
  add_executable(sim_tests tests/sim_tests.cpp)
  target_link_libraries(sim_tests PRIVATE driftwood_sim)
  add_test(NAME sim_tests COMMAND sim_tests)
endif()

# ---- game: engine + renderer on top of raylib ----
if(DRIFTWOOD_BUILD_GAME)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(raylib)

  # everything except main(), so tests can link the real renderer code
  add_library(driftwood_game STATIC
    src/engine/Draw.cpp
    src/engine/Ui.cpp
    src/engine/Lighting.cpp
    src/game/Art.cpp
    src/game/Depth.cpp
    src/game/Figure.cpp
    src/game/Solid.cpp
    src/game/Building.cpp
    src/game/Labels.cpp
    src/game/WorldView.cpp
    src/game/Hud.cpp
    src/game/Saves.cpp
    src/game/Inspect.cpp
    src/game/Menus.cpp
    src/game/Game.cpp)
  target_link_libraries(driftwood_game PUBLIC driftwood_sim raylib)
  target_include_directories(driftwood_game PUBLIC src)
  if(MSVC)
    target_compile_options(driftwood_game PRIVATE /W3 /utf-8)
  endif()
  if(APPLE)
    target_link_libraries(driftwood_game PUBLIC "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
  endif()

  add_executable(driftwood src/main.cpp)
  target_link_libraries(driftwood PRIVATE driftwood_game)
  if(MSVC)
    target_compile_options(driftwood PRIVATE /W3 /utf-8)
  endif()

  # geometry checks on the renderer (projection, picking); never opens a window
  if(DRIFTWOOD_BUILD_TESTS)
    add_executable(game_tests tests/game_tests.cpp)
    target_link_libraries(game_tests PRIVATE driftwood_game)
    add_test(NAME game_tests COMMAND game_tests)
  endif()
  # copy assets next to the executable
  add_custom_command(TARGET driftwood POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:driftwood>/assets)
endif()

# ---- packaging: `cmake --install build --prefix dist` gives a ready-to-run folder ----
if(DRIFTWOOD_BUILD_GAME)
  install(TARGETS driftwood RUNTIME DESTINATION .)
  install(DIRECTORY assets DESTINATION .)
endif()
