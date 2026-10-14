add_library(pbv_dsp STATIC
    BirdEngine.cpp
    ForestEcho.cpp
    Preset.cpp)

target_include_directories(pbv_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(pbv_dsp PUBLIC cxx_std_17)
set_target_properties(pbv_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(pbv_dsp PRIVATE /W4)
else()
    target_compile_options(pbv_dsp PRIVATE -Wall -Wextra)
endif()
