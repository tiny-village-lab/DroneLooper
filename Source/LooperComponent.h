#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

// Une voix de lecture indépendante.
//
// Toutes les instances lisent le MÊME échantillon (enregistré et
// possédé par le MainComponent), mais chacune garde sa propre position
// de lecture, sa vitesse, son filtre, son volume et son panoramique.
// Chaque voix ADDITIONNE sa contribution dans la sortie : c'est ce qui
// permet d'en empiler plusieurs.
class LooperComponent final : public juce::Component
{
public:
    explicit LooperComponent(const juce::String& titleToUse);

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    // --- Thread message ---

    // Alloue le buffer de travail et prépare le filtre.
    void prepare(double sampleRate, int maximumBlockSize);

    // À appeler quand la lecture (re)démarre, pendant que le thread
    // audio n'appelle pas renderNextBlock : tire au sort la portion de
    // l'échantillon que cette voix va boucler, puis remet la position
    // de lecture et l'état du filtre à zéro.
    void startPlayback(int sampleLength);

    // --- Thread audio ---

    // Ajoute la contribution de cette voix dans outputBuffer (stéréo).
    // sampleData / sampleLength décrivent l'échantillon partagé.
    void renderNextBlock(
        juce::AudioBuffer<float>& outputBuffer,
        int startSample,
        int numberOfSamples,
        const float* sampleData,
        int sampleLength
    );

private:
    void buildSpeedControls();
    void buildFilterControls();
    void buildMixControls();

    void updatePlaybackSpeed();

    // Remplit le buffer de travail par lecture interpolée de la portion
    // de l'échantillon partagé, à la vitesse courante, en appliquant
    // l'enveloppe anti-clic.
    void readSampleIntoRenderBuffer(
        int numberOfSamples,
        const float* sampleData,
        float speed
    );

    // Gain de l'enveloppe anti-clic à une position donnée dans la
    // portion. La portion démarre et finit à zéro : le raccord de
    // boucle passe 0 -> 0, donc pas de clic. Basé sur la position (et
    // non sur le temps) : fonctionne aussi en lecture inversée.
    float fadeGainAt(double positionInPortion) const;

    void applyFilter(int numberOfSamples);

    // Index du slider (+ réglage fin) -> vitesse de lecture.
    // 0 -> arrêt ; sinon |index| donne le demi-ton (magnitude - 37 =>
    // -36..+36), le réglage fin ajoute une fraction de demi-ton, et le
    // signe donne le sens (avant / inversé).
    static float speedFrom(int index, float fineTuneSemitones);

    // Texte affiché sous le fader de vitesse.
    static juce::String speedText(int index);

    // Replie un index dans [0, length) : la boucle est continue, donc
    // les voisins débordant d'un bout reviennent par l'autre.
    static int wrapIndex(int index, int length);

    juce::String title;

    // Vitesse.
    juce::Slider speedSlider;
    juce::Label speedLabel;
    juce::Slider fineTuneSlider;
    juce::Label fineTuneLabel;

    // Filtre.
    juce::TextButton filterTypeButton { "Passe-bas" };
    juce::Slider cutoffSlider;
    juce::Label cutoffLabel;
    juce::Slider resonanceSlider;
    juce::Label resonanceLabel;

    // Mix.
    juce::Slider volumeSlider;
    juce::Label volumeLabel;
    juce::Slider panSlider;
    juce::Label panLabel;

    // Paramètres publiés par l'UI, relus par le thread audio.
    std::atomic<float> playbackSpeed { 1.0f };
    std::atomic<bool> highPassMode { false };
    std::atomic<float> cutoffHz { 20000.0f };
    std::atomic<float> resonanceValue { 0.707f };
    std::atomic<float> playbackGain { 1.0f };
    std::atomic<float> panPosition { 0.0f }; // -1 gauche, +1 droite.

    // État audio (thread audio uniquement).
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::AudioBuffer<float> renderBuffer; // Scratch mono pré-alloué.

    // La voix ne lit qu'une portion de l'échantillon partagé, tirée au
    // sort à chaque démarrage de lecture. playbackPosition est RELATIVE
    // à cette portion : elle reste dans [0, portionLength).
    int portionStart = 0;
    int portionLength = 0;
    double playbackPosition = 0.0;

    int fadeLengthSamples = 0;

    // Borne haute du cutoff, ajustée au sample rate (< Nyquist).
    float maxCutoffHz = 20000.0f;

    static constexpr int semitoneRange = 36; // +/- trois octaves.

    static constexpr double fadeSeconds = 0.005;      // ~5 ms.
    static constexpr double minPortionFraction = 0.1; // 10 %.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LooperComponent)
};
