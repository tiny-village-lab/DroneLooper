#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

// Émulation de réverbe à ressort.
//
// Ce qui caractérise un ressort n'est pas la densité (ça, c'est une
// plaque) mais la DISPERSION : les aigus s'y propagent moins vite que
// les graves, ce qui produit le « boïng » chirpé typique. On la modélise
// par une longue cascade de filtres passe-tout placée DANS la boucle de
// réinjection : chaque tour accentue la dispersion, et le chirp se
// construit répétition après répétition.
//
// Plusieurs ressorts de longueurs non harmoniquement liées sont mis en
// parallèle, comme dans un vrai tank, pour éviter un flottement
// périodique.
//
// La classe est MONO : le MainComponent en instancie deux (gauche et
// droite), ce qui laisse la possibilité de les régler séparément.
//
// IMPORTANT pour la stéréo : les deux instances doivent recevoir des
// lengthScale DIFFÉRENTS. Avec des ressorts identiques, elles
// produiraient exactement la même sortie pour une entrée centrée —
// donc du dual-mono, sans aucune largeur.
class SpringReverb
{
public:
    // lengthScale multiplie la longueur des ressorts. Deux instances
    // avec des facteurs non harmoniquement liés sont décorrélées, ce
    // qui ouvre l'image stéréo.
    void prepare(
        double sampleRate,
        int maximumBlockSize,
        float lengthScale
    );
    void reset();

    // Décroissance visée (T60), en secondes.
    void setTime(float seconds);

    // 0 = sombre, 1 = brillant. Pilote l'amortissement dans la boucle.
    void setTone(float tone);

    // Traite en place. Produit le signal WET seul : le dosage du retour
    // se fait à l'extérieur.
    void process(float* channelData, int numberOfSamples);

private:
    // Passe-tout du premier ordre. Sa phase varie avec la fréquence :
    // c'est en le cascadant qu'on obtient la dispersion du ressort.
    struct Allpass
    {
        float previousInput = 0.0f;
        float previousOutput = 0.0f;

        float process(float input, float coefficient)
        {
            const float output =
                -coefficient * input
                + previousInput
                + coefficient * previousOutput;

            previousInput = input;
            previousOutput = output;

            return output;
        }

        void reset()
        {
            previousInput = 0.0f;
            previousOutput = 0.0f;
        }
    };

    static constexpr int numberOfSprings = 3;
    static constexpr int numberOfAllpassStages = 32;

    // Coefficient des passe-tout : c'est lui qui règle la force du
    // chirp. Plus il est élevé, plus le « boïng » est marqué.
    static constexpr float allpassCoefficient = 0.65f;

    // Longueurs de base des ressorts, en millisecondes. Volontairement
    // non harmoniquement liées entre elles. Elles sont ensuite
    // multipliées par lengthScale, propre à chaque canal.
    static constexpr std::array<float, numberOfSprings>
        springLengthsMs { 29.7f, 37.1f, 43.3f };

    // Marge d'allocation de la ligne à retard, pour couvrir le plus
    // grand lengthScale utilisé.
    static constexpr float maxLengthScale = 2.0f;

    // Un ressort ne transmet pas les graves : on les retire en entrée.
    static constexpr float inputHighPassHz = 250.0f;

    // Doit rester assez haut pour permettre les très longues queues :
    // un ressort de 30 ms demande un gain de boucle de ~0,987 pour
    // décroître en 16 s.
    static constexpr float maxSpringFeedback = 0.995f;

    // Seuil de l'écrêteur de boucle. En dessous, il est strictement
    // linéaire : indispensable ici, car la moindre perte parasite se
    // multiplie sur les centaines de passages d'une longue queue.
    static constexpr float clipThreshold = 0.7f;

    // Gain de sortie du tank. À ajuster si la réverbe est trop discrète
    // ou trop envahissante par rapport au signal direct.
    static constexpr float outputGain = 2.0f;

    struct Spring
    {
        juce::dsp::DelayLine<
            float,
            juce::dsp::DelayLineInterpolationTypes::None
        > delay { 8192 };

        std::array<Allpass, numberOfAllpassStages> allpasses;

        float damperState = 0.0f;
        float lengthInSamples = 0.0f;
        float feedback = 0.0f;
    };

    // Écrêteur doux : borne la boucle si le temps est très long, sans
    // colorer aux niveaux normaux.
    static float softClip(float x);

    void updateFeedback();

    std::array<Spring, numberOfSprings> springs;

    double currentSampleRate = 44100.0;

    float damperCoefficient = 0.5f;
    float highPassCoefficient = 0.0f;
    float highPassState = 0.0f;

    // Les ressorts ont des longueurs différentes : ils sont décorrélés
    // et somment de façon incohérente. La normalisation est donc en
    // racine du nombre de ressorts, pas en nombre de ressorts.
    float outputScale = 1.0f;

    float decaySeconds = 2.0f;
};
