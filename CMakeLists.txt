cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
endif()

project(neots CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    indicators
    GIT_REPOSITORY https://github.com/p-ranav/indicators.git
    GIT_TAG        v2.3
)
FetchContent_MakeAvailable(indicators)

find_program(NINJA ninja)
if(NINJA)
    set(CMAKE_MAKE_PROGRAM ${NINJA})
endif()

find_package(OpenCV REQUIRED)
find_package(Freetype REQUIRED)
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/bindings.cpp")

add_library(ocr_core STATIC ${CORE_SOURCES})

target_include_directories(ocr_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(ocr_core PUBLIC
    ${OpenCV_LIBS}
    Freetype::Freetype
    nlohmann_json::nlohmann_json
    indicators::indicators
    yaml-cpp::yaml-cpp
    harfbuzz::harfbuzz
    Threads::Threads
)

# Platform-appropriate optimization flags
if(MSVC)
    target_compile_options(ocr_core PRIVATE /O2)
else()
    target_compile_options(ocr_core PRIVATE -O2)
endif()

set_target_properties(ocr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_core src/bindings.cpp)
target_link_libraries(_core PRIVATE ocr_core)

install(TARGETS _core DESTINATION ${SKBUILD_PROJECT_NAME})