cmake_minimum_required(VERSION 3.22)

project(PrairieBirdVoice VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The plugin needs JUCE; the engine and render tool don't, so the matching
# loop can be built and run anywhere without it.
option(PBV_BUILD_PLUGIN "Build the VST3 plugin (downloads JUCE)" ON)
option(PBV_COPY_PLUGIN "Copy the VST3 into the system VST3 folder after building (needs admin on Windows)" OFF)

add_subdirectory(dsp)
add_subdirectory(tools/render)

if(PBV_BUILD_PLUGIN)
    include(FetchContent)
    # To use a local JUCE checkout instead: -DFETCHCONTENT_SOURCE_DIR_JUCE=path/to/JUCE
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugin)
endif()

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME engine_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests -v
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(engine_tests PROPERTIES ENVIRONMENT "PBV_RENDER=$<TARGET_FILE:pbv_render>")
endif()
