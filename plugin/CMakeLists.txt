# Plugin JUCE (VST3 + CLAP), étape 2a : façade.
# La compilation se fait sur GitHub (voir .github/workflows/build.yml).
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4)
FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG main)
FetchContent_MakeAvailable(JUCE clap-juce-extensions)

# Image de la façade intégrée au plugin (BinaryData::facade_png)
juce_add_binary_data(MAQ16Assets SOURCES ../assets/facade.png)

juce_add_plugin(MAQ16
    PLUGIN_MANUFACTURER_CODE Maqp
    PLUGIN_CODE Maq1
    COMPANY_NAME "MAQ16 Plugin"
    BUNDLE_ID com.maq16plugin.maq16
    PRODUCT_NAME "MAQ 16-3"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

clap_juce_extensions_plugin(TARGET MAQ16
    CLAP_ID "com.maq16plugin.maq16"
    CLAP_FEATURES note-effect utility)

target_sources(MAQ16 PRIVATE
    PluginProcessor.cpp PluginProcessor.h
    Facade.cpp Facade.h)

target_compile_definitions(MAQ16 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MAQ16 PRIVATE
    MAQ16Assets
    maq_engine
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
