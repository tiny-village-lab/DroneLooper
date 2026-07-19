#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

// Tous les réglages d'une voix, dans les unités de ses sliders (pour
// que les réappliquer soit direct). Sert à mémoriser un preset et à
// interpoler entre deux presets.
struct LooperState
{
    int speedIndex = 37; // semitoneRange + 1 -> 0 demi-ton.
    double fineTune = 0.0;

    bool highPass = false;
    double cutoffHz = 20000.0;
    double resonance = 0.707;

    double delayTimeMs = 300.0;
    double feedbackPercent = 30.0;
    double tone = 0.0;
    double delayMixPercent = 0.0;

    double tremoloRateHz = 4.0;
    double tremoloDepthPercent = 0.0;
    double tremoloShape = 0.0; // 0 = sinus, 1 = dent de scie inversée.

    double volumeDecibels = 0.0;
    double pan = 0.0;
    double reverbSendPercent = 0.0;
};

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

    // --- Presets (thread message) ---

    // Lit les réglages courants depuis les contrôles.
    LooperState captureState() const;

    // Applique un état exactement : contrôles et paramètres audio.
    void applyState(const LooperState& state);

    // Applique un état intermédiaire, à mi-chemin entre deux presets.
    // progress va de 0 (source) à 1 (cible).
    //
    // Deux paramètres échappent à l'interpolation :
    //  - la vitesse glisse continûment dans l'espace des VITESSES (pas
    //    des demi-tons), donc hors des crans : d'où la sensation
    //    d'accélération / décélération,
    //  - le type de filtre bascule à la toute fin.
    void applyMorphedState(
        const LooperState& from,
        const LooperState& to,
        double progress
    );

    // Recale les contrôles sur un état intermédiaire. Séparé du calcul
    // audio pour pouvoir rafraîchir l'écran moins souvent.
    void updateControlsForMorph(
        const LooperState& from,
        const LooperState& to,
        double progress
    );

    // --- Thread message ---

    // Alloue le buffer de travail et prépare le filtre.
    void prepare(double sampleRate, int maximumBlockSize);

    // À appeler quand la lecture (re)démarre, pendant que le thread
    // audio n'appelle pas renderNextBlock : tire au sort la portion de
    // l'échantillon que cette voix va boucler, puis remet la position
    // de lecture et l'état du filtre à zéro.
    void startPlayback(int sampleLength);

    // --- Thread audio ---

    // Ajoute la contribution de cette voix dans outputBuffer (stéréo),
    // et sa part de send dans reverbSendBuffer (stéréo, indexé depuis 0).
    // Le send est pris APRÈS volume et pan, comme sur une console : une
    // voix pannée à gauche alimente surtout la réverbe de gauche.
    // sampleData / sampleLength décrivent l'échantillon partagé.
    void renderNextBlock(
        juce::AudioBuffer<float>& outputBuffer,
        juce::AudioBuffer<float>& reverbSendBuffer,
        int startSample,
        int numberOfSamples,
        const float* sampleData,
        int sampleLength
    );

private:
    void buildSpeedControls();
    void buildFilterControls();
    void buildDelayControls();
    void buildTremoloControls();
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

    // Delay avec réinjection. Le tone est placé DANS la boucle de
    // réinjection : chaque répétition est filtrée une fois de plus que
    // la précédente, donc les échos s'assombrissent (ou s'amincissent)
    // progressivement.
    // Contient aussi la saturation, appliquée au SEUL signal retardé,
    // après la ligne à retard et hors de la boucle de réinjection.
    void applyDelay(int numberOfSamples);

    // Tremolo : module le volume VERS LE BAS uniquement (le gain reste
    // dans [1 - profondeur, 1], jamais au-dessus de l'unité). Placé
    // AVANT le delay : le delay capture donc le signal déjà haché et
    // en répète le motif rythmique, au lieu de subir la modulation.
    void applyTremolo(int numberOfSamples);

    // Forme d'onde du tremolo, à phase donnée (dans [0, 1)).
    // Les deux formes valent 1 en début de cycle, ce qui permet de les
    // fondre l'une dans l'autre sans rupture :
    //   morph = 0 -> sinus, décroissance douce,
    //   morph = 1 -> dent de scie inversée, attaque franche.
    static float tremoloShapeAt(double phase, float morph);

    // Écrêteur doux placé DANS la boucle de réinjection. Ce n'est pas
    // une couleur : il est strictement linéaire en dessous du seuil, et
    // ne sert qu'à borner le niveau. C'est lui qui permet un feedback à
    // 100 % : au lieu de diverger, la boucle se stabilise et oscille.
    static float softClip(float x);

    // Courbe de transfert de la saturation. coldness morphe entre une
    // courbe douce (chaud, harmoniques paires via l'asymétrie) et un
    // écrêtage dur (froid, harmoniques impaires).
    static float shape(float x, float coldness);

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

    // Delay.
    juce::Slider delayTimeSlider;
    juce::Label delayTimeLabel;
    juce::Slider feedbackSlider;
    juce::Label feedbackLabel;
    juce::Slider toneSlider;
    juce::Label toneLabel;
    juce::Slider delayMixSlider;
    juce::Label delayMixLabel;

    // Tremolo.
    juce::Slider tremoloRateSlider;
    juce::Label tremoloRateLabel;
    juce::Slider tremoloDepthSlider;
    juce::Label tremoloDepthLabel;
    juce::Slider tremoloShapeSlider;
    juce::Label tremoloShapeLabel;

    // Mix.
    juce::Slider volumeSlider;
    juce::Label volumeLabel;
    juce::Slider panSlider;
    juce::Label panLabel;
    juce::Slider reverbSendSlider;
    juce::Label reverbSendLabel;

    // Paramètres publiés par l'UI, relus par le thread audio.
    std::atomic<float> playbackSpeed { 1.0f };
    std::atomic<bool> highPassMode { false };
    std::atomic<float> cutoffHz { 20000.0f };
    std::atomic<float> resonanceValue { 0.707f };
    std::atomic<float> playbackGain { 1.0f };
    std::atomic<float> panPosition { 0.0f }; // -1 gauche, +1 droite.
    std::atomic<float> reverbSend { 0.0f };

    std::atomic<float> delayTimeMs { 300.0f };
    std::atomic<float> feedbackAmount { 0.0f };
    std::atomic<float> toneValue { 0.0f };    // <0 passe-bas, >0 passe-haut.
    std::atomic<float> delayMix { 0.0f };     // 0 = delay absent.

    std::atomic<float> tremoloRateHz { 4.0f };
    std::atomic<float> tremoloDepth { 0.0f }; // 0 = tremolo absent.
    std::atomic<float> tremoloShapeMorph { 0.0f };

    // État audio (thread audio uniquement).
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::AudioBuffer<float> renderBuffer; // Scratch mono pré-alloué.

    juce::dsp::DelayLine<
        float,
        juce::dsp::DelayLineInterpolationTypes::Linear
    > delayLine;

    // Filtre de la boucle de réinjection du delay.
    juce::dsp::StateVariableTPTFilter<float> toneFilter;

    // Le temps de delay est lissé : un changement brutal produirait un
    // clic. Le lissage donne une glissade de hauteur, comme une bande.
    juce::SmoothedValue<float> smoothedDelaySamples;

    double currentSampleRate = 44100.0;

    // Phase du tremolo, dans [0, 1). Avance d'un incrément par
    // échantillon.
    double tremoloPhase = 0.0;

    // Le rebouclage de la dent de scie est une discontinuité de gain :
    // sans lissage elle produirait un clic, pas une attaque. Un lissage
    // très court (~1,5 ms) supprime le clic tout en restant perçu comme
    // franc.
    float tremoloSmoothedGain = 1.0f;
    float tremoloSmoothingCoefficient = 1.0f;

    static constexpr double tremoloSmoothingSeconds = 0.0015;

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

    static constexpr double maxDelaySeconds = 2.0;

    // 100 % est désormais sûr : l'écrêteur de boucle borne le niveau.
    static constexpr float maxFeedback = 1.0f;

    // Seuil de l'écrêteur de boucle. En dessous, la boucle est
    // parfaitement transparente ; au-dessus, elle sature en douceur
    // jusqu'à un plafond de 1,0.
    static constexpr float loopClipThreshold = 0.7f;

    // --- Réglage de la couleur de la saturation (à ajuster à l'oreille) ---

    // Gain d'attaque au maximum de saturation. Plus haut = plus sale.
    static constexpr float maxSaturationDrive = 4.0f;

    // Part de saturation restante quand le feedback est à zéro. C'est
    // ce qui rend la distorsion discrète à faible feedback. À 0, un
    // feedback nul donne un delay parfaitement propre.
    static constexpr float saturationFeedbackFloor = 0.15f;

    // Asymétrie du côté chaud. C'est elle qui crée les harmoniques
    // paires. Plus haut = plus « lampe », mais aussi plus de DC à
    // compenser. 0 = saturation purement symétrique.
    static constexpr float warmBias = 0.3f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LooperComponent)
};
