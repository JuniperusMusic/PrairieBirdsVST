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
