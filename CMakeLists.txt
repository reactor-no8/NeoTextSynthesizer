cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
endif()

project(neots CXX)

set(CMAKE_CXX_STANDARD 20)
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

set(CORE_SOURCES
    src/algorithms/glyph_cache.cpp
    src/algorithms/paged_bitmap.cpp
    src/backgrounds/background_resources.cpp
    src/generation_tasks.cpp
    src/parallelization/parallel.cpp
    src/parallelization/tasks/singletext_task.cpp
    src/text_synth/renderer.cpp
    src/text_synth/text_synthesizer.cpp
    src/text_synth/textsampler.cpp
    src/text_synth/writer.cpp
    src/utils/utf8_helper.cpp
    src/utils/utils.cpp
    src/utils/yaml_utils.cpp
)

add_library(ocr_core STATIC ${CORE_SOURCES})

target_include_directories(ocr_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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