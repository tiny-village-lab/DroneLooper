#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "LooperComponent.h"
#include "SpringReverb.h"

#include <array>
#include <vector>

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

    // --- Presets ---

    // Un emplacement A/B/C/D : les réglages des quatre voix. La réverbe
    // et le master en sont volontairement exclus.
    struct Preset
    {
        std::vector<LooperState> loopers;
        bool hasContent = false;
    };

    void storePreset(int index);
    void recallPreset(int index);
    void advanceMorph();
    void refreshPresetButtons();

    static constexpr int numberOfPresets = 4;

    std::array<Preset, numberOfPresets> presets;
    juce::OwnedArray<juce::TextButton> presetButtons;

    // Quand il est armé, cliquer sur A/B/C/D enregistre au lieu de
    // rappeler.
    juce::TextButton storeButton { "Memoriser" };

    juce::Slider transitionTimeSlider;
    juce::Label transitionTimeLabel;

    // Transition en cours. Le morphing est piloté depuis le thread
    // message : il ne fait que publier des valeurs dans les atomiques
    // déjà lues par le thread audio, donc rien à synchroniser de plus.
    bool morphing = false;
    double morphProgress = 0.0;
    double morphDurationSeconds = 0.0;
    int morphUiCounter = 0;

    std::vector<LooperState> morphFrom;
    std::vector<LooperState> morphTo;

    // Rafraîchir 48 sliders à chaque tick serait inutilement lourd :
    // l'affichage suit trois fois moins vite que les paramètres.
    static constexpr int morphUiInterval = 3;

    // --- Thread audio ---

    // Réduit l'entrée à un seul canal dans monoInputBuffer : si le
    // périphérique fournit deux canaux, on somme L+R plutôt que
    // d'ignorer la droite. Tout le moteur travaille ensuite en mono ;
    // la stéréo naît du panoramique des voix.
    void buildMonoInput(
        const juce::AudioBuffer<float>& inputBuffer,
        int startSample,
        int numberOfSamples
    );

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

    // Nombre de voix.
    static constexpr int numberOfLoopers = 4;

    juce::OwnedArray<LooperComponent> loopers;

    juce::TextButton recordButton { "Enregistrer" };

    // Master : les voix s'additionnent, il faut donc un point de
    // réglage global. Défaut -12 dB = 1/4, soit l'unité quand les 4
    // voix jouent le même signal à volume nominal.
    juce::Slider masterVolumeSlider;
    juce::Label masterVolumeLabel;
    std::atomic<float> masterGain { 1.0f };

    // Réverbe à ressort, alimentée par les sends des voix. Deux
    // instances mono (gauche / droite) plutôt qu'une stéréo : ça laisse
    // la possibilité de les régler séparément plus tard.
    SpringReverb reverbLeft;
    SpringReverb reverbRight;

    juce::AudioBuffer<float> reverbSendBuffer;

    juce::Slider reverbToneSlider;
    juce::Label reverbToneLabel;
    juce::Slider reverbTimeSlider;
    juce::Label reverbTimeLabel;

    // Solo : coupe le signal direct, on n'entend plus que le wet.
    juce::TextButton reverbSoloButton { "Solo Rev" };
    std::atomic<bool> reverbSolo { false };

    // Le retour de réverbe est à l'unité : le dosage se fait
    // uniquement via les sends de chaque voix.
    std::atomic<float> reverbTone { 0.5f };
    std::atomic<float> reverbTime { 2.0f };

    // Filet de sécurité : empêche toute saturation numérique si on
    // remonte les volumes.
    juce::dsp::Limiter<float> limiter;

    static constexpr double defaultMasterDecibels = -12.0;

    // Nombre de canaux réellement fournis par le périphérique, relu à
    // chaque prepareToPlay : brancher une interface stéréo en cours de
    // route est donc pris en compte.
    int activeInputChannels = 1;

    // L'entrée, réduite à un canal.
    juce::AudioBuffer<float> monoInputBuffer;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
