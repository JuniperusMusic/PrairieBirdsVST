juce_add_plugin(PrairieBirdVoice
    COMPANY_NAME "Juniperus Music"
    BUNDLE_ID com.juniperusmusic.prairiebirdvoice
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${PBV_COPY_PLUGIN}
    PLUGIN_MANUFACTURER_CODE Jnpr
    PLUGIN_CODE Pbv1
    VST3_CATEGORIES Instrument Synth
    FORMATS VST3 Standalone
    PRODUCT_NAME "Prairie Bird Voice")

juce_add_binary_data(PrairieBirdPresets
    NAMESPACE PresetData
    HEADER_NAME PresetData.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/presets/init.json
        ${CMAKE_SOURCE_DIR}/presets/white_throated_sparrow.json)

target_sources(PrairieBirdVoice PRIVATE
    PluginEditor.cpp
    PluginProcessor.cpp)

target_compile_definitions(PrairieBirdVoice PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PrairieBirdVoice
    PRIVATE
        pbv_dsp
        PrairieBirdPresets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Plugin-level tests: MIDI learn, mappings saved with the project, presets.
juce_add_console_app(pbv_plugin_tests PRODUCT_NAME "pbv_plugin_tests")
target_sources(pbv_plugin_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/plugin_tests.cpp
    PluginEditor.cpp
    PluginProcessor.cpp)
target_include_directories(pbv_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pbv_plugin_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(pbv_plugin_tests PRIVATE
    pbv_dsp
    PrairieBirdPresets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
add_test(NAME plugin_tests COMMAND pbv_plugin_tests)
