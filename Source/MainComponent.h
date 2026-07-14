#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "LooperComponent.h"

// Détient ce qui est PARTAGÉ entre toutes les voix :
//  - l'échantillon enregistré (un seul, lu par tous les loopers),
//  - l'état enregistrement / lecture,
//  - le bouton commun qui déclenche les deux,
//  - le vu-mètre d'entrée.
//
// Chaque LooperComponent lit ce même échantillon avec ses propres
// réglages, et additionne sa contribution dans la sortie.
class MainComponent final
    : public juce::AudioAppComponent,
      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(
        int samplesPerBlockExpected,
        double sampleRate
    ) override;

    void getNextAudioBlock(
        const juce::AudioSourceChannelInfo& bufferToFill
    ) override;

    void releaseResources() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;

    // Bascule enregistrement -> lecture -> nouvel enregistrement.
    void toggleRecording();

    // Fondu d'entrée/sortie sur l'échantillon : la boucle démarre et
    // finit à zéro, ce qui supprime le clic au raccord.
    void applyLoopFade();

    // --- Thread audio ---
    void writeInputToHistory(
        const juce::AudioBuffer<float>& sourceBuffer,
        int sourceStartSample,
        int numberOfSamples
    );

    void appendToRecording(
        const juce::AudioBuffer<float>& sourceBuffer,
        int sourceStartSample,
        int numberOfSamples
    );

    // Nombre de voix. Passer à 4 suffit à en créer autant.
    static constexpr int numberOfLoopers = 1;

    juce::OwnedArray<LooperComponent> loopers;

    juce::TextButton recordButton { "Enregistrer" };

    // Vu-mètre d'entrée.
    std::atomic<float> inputLevel { 0.0f };
    float displayedLevel = 0.0f;
    juce::Rectangle<int> meterArea;

    // Historique glissant de l'entrée (non exploité pour l'instant).
    juce::AudioBuffer<float> inputHistoryBuffer;
    int historyWritePosition = 0;
    int historyBufferSize = 0;

    static constexpr double historyDurationSeconds = 5.0;

    // L'échantillon partagé.
    juce::AudioBuffer<float> recordingBuffer;
    int recordingCapacity = 0;
    int recordingWritePosition = 0;
    int recordedLength = 0;

    static constexpr double maxRecordingSeconds = 60.0;

    std::atomic<bool> isRecording { false };
    std::atomic<bool> isPlaying { false };

    int fadeLengthSamples = 0;
    static constexpr double fadeSeconds = 0.005; // ~5 ms.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
