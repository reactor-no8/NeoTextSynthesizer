cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
endif()

project(neots CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_program(NINJA ninja)
if(NINJA)
    set(CMAKE_MAKE_PROGRAM ${NINJA})
endif()

find_package(OpenCV REQUIRED)
find_package(Boost REQUIRED)
find_package(Freetype REQUIRED)
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(indicators REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)

set(CORE_SOURCES
    src/algorithms/glyph_cache.cpp
    src/algorithms/paged_bitmap.cpp
    src/algorithms/colors.cpp
    src/algorithms/alpha_blend.cpp
    src/algorithms/transforms.cpp
    src/backgrounds/background_resources.cpp
    src/backgrounds/background_sampler.cpp
    src/fonts/line_shaper.cpp
    src/fonts/text_effects.cpp
    src/fonts/font_resource.cpp
    src/fonts/font_selector.cpp
    src/fonts/library.cpp
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
    Boost::boost
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(ocr_core PRIVATE /O3)
    else()
        target_compile_options(ocr_core PRIVATE -O3)
    endif()
endif()

set_target_properties(ocr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(NEOTS_BUILD_PYTHON_BINDINGS "Build Python bindings with nanobind" ON)

if(NEOTS_BUILD_PYTHON_BINDINGS)
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(_core src/bindings.cpp)
    target_link_libraries(_core PRIVATE ocr_core)

    install(TARGETS _core DESTINATION ${SKBUILD_PROJECT_NAME})
endif()
