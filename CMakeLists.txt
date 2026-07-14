cmake_minimum_required(VERSION 3.22)

project(DroneLooper VERSION 0.1.0)

# On utilise C++20 pour notre application.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" JUCE)

juce_add_gui_app(DroneLooper
    PRODUCT_NAME "Drone Looper"
    COMPANY_NAME "Brumestral"
)

target_sources(DroneLooper
    PRIVATE
        Source/Main.cpp
)

target_compile_definitions(DroneLooper
    PRIVATE
        # Nous n'utilisons pas de navigateur intégré.
        JUCE_WEB_BROWSER=0

        # Nous n'avons pas besoin de requêtes réseau.
        JUCE_USE_CURL=0
)

target_link_libraries(DroneLooper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra

    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)