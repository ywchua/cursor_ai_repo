cmake_minimum_required(VERSION 3.16)
project(dbw_rules LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(DBW_USE_NLOHMANN_JSON "Enable JSON loader using nlohmann/json" ON)
option(DBW_USE_SPDLOG "Enable Logging using spdlog" ON)

add_library(dbw_rules STATIC
    src/dbw/RuleEngine.cpp
)

target_include_directories(dbw_rules
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(DBW_USE_NLOHMANN_JSON)
    find_package(nlohmann_json 3.2.0 QUIET)
    if(nlohmann_json_FOUND)
        target_link_libraries(dbw_rules PUBLIC nlohmann_json::nlohmann_json)
        target_compile_definitions(dbw_rules PUBLIC DBW_USE_NLOHMANN_JSON=1)
        message(STATUS "dbw_rules: nlohmann_json found; JSON loader enabled")
    else()
        message(STATUS "dbw_rules: nlohmann_json not found; JSON loader disabled")
    endif()
endif()

if (USE_SPDLOG)
  find_package(spdlog 1.8 QUIET)
  if (spdlog_FOUND)
    target_link_libraries(dbw_rules PUBLIC spdlog::spdlog)
    target_link_libraries(dbw_rules_example PRIVATE spdlog::spdlog)
    target_compile_definitions(dbw_rules PUBLIC HAVE_SPDLOG=1)
    # Enable TRACE in Debug builds
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
      target_compile_definitions(dbw_rules PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
    endif()
    message(STATUS "spdlog enabled")
  else()
    message(STATUS "spdlog not found; logging disabled")
  endif()
endif()

add_executable(dbw_rules_example
    tools/example_main.cpp
)
target_link_libraries(dbw_rules_example PRIVATE dbw_rules)

