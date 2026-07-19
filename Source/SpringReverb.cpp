#include "SpringReverb.h"

#include <cmath>

void SpringReverb::prepare(
    double sampleRate,
    int maximumBlockSize,
    float lengthScale
)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize =
        static_cast<juce::uint32>(maximumBlockSize);
    spec.numChannels = 1;

    for (int i = 0; i < numberOfSprings; ++i)
    {
        auto& spring = springs[static_cast<size_t>(i)];

        const float lengthMs =
            springLengthsMs[static_cast<size_t>(i)] * lengthScale;

        spring.lengthInSamples = static_cast<float>(
            lengthMs * 0.001 * sampleRate
        );

        // Alloué pour le plus grand facteur possible : prepare peut
        // être rappelé avec un lengthScale différent.
        spring.delay.setMaximumDelayInSamples(
            static_cast<int>(
                springLengthsMs[static_cast<size_t>(i)]
                * maxLengthScale * 0.001 * sampleRate
            ) + 4
        );

        spring.delay.prepare(spec);
        spring.delay.setDelay(spring.lengthInSamples);
    }

    // Passe-haut d'entrée à un pôle : un ressort ne transmet pas les
    // graves.
    highPassCoefficient = static_cast<float>(
        std::exp(
            -2.0 * juce::MathConstants<double>::pi
            * inputHighPassHz / sampleRate
        )
    );

    outputScale =
        outputGain
        / std::sqrt(static_cast<float>(numberOfSprings));

    updateFeedback();
    reset();
}

void SpringReverb::reset()
{
    for (auto& spring : springs)
    {
        spring.delay.reset();
        spring.damperState = 0.0f;

        for (auto& allpass : spring.allpasses)
            allpass.reset();
    }

    highPassState = 0.0f;
}

void SpringReverb::setTime(float seconds)
{
    decaySeconds = juce::jmax(0.1f, seconds);

    updateFeedback();
}

void SpringReverb::updateFeedback()
{
    for (auto& spring : springs)
    {
        if (spring.lengthInSamples <= 0.0f)
            continue;

        const float loopSeconds =
            spring.lengthInSamples
            / static_cast<float>(currentSampleRate);

        // Gain donnant la décroissance voulue : chaque tour de boucle
        // doit faire perdre 60 dB au bout de decaySeconds.
        const float gain = std::pow(
            10.0f,
            -3.0f * loopSeconds / decaySeconds
        );

        spring.feedback = juce::jmin(maxSpringFeedback, gain);
    }
}

void SpringReverb::setTone(float tone)
{
    const float normalised = juce::jlimit(0.0f, 1.0f, tone);

    // 0 -> 500 Hz (sombre), 1 -> 6 kHz (brillant), en échelle log.
    const float cutoff = 500.0f * std::pow(12.0f, normalised);

    const float safeCutoff = juce::jmin(
        cutoff,
        static_cast<float>(currentSampleRate) * 0.45f
    );

    // Passe-bas à un pôle.
    damperCoefficient = 1.0f - std::exp(
        -2.0f * juce::MathConstants<float>::pi
        * safeCutoff / static_cast<float>(currentSampleRate)
    );
}

float SpringReverb::softClip(float x)
{
    const float magnitude = std::abs(x);

    // Strictement l'identité sous le seuil. Un tanh brut retirerait
    // ~0,3 % à chaque passage, ce qui tuerait une queue de 16 s bien
    // avant l'heure (des centaines de passages dans la boucle).
    if (magnitude <= clipThreshold)
        return x;

    const float headroom = 1.0f - clipThreshold;

    const float excess = (magnitude - clipThreshold) / headroom;

    const float clipped =
        clipThreshold + headroom * std::tanh(excess);

    return x < 0.0f ? -clipped : clipped;
}

void SpringReverb::process(float* channelData, int numberOfSamples)
{
    for (int i = 0; i < numberOfSamples; ++i)
    {
        // Passe-haut d'entrée (un pôle) : on retire les graves, que le
        // ressort ne transmettrait pas.
        highPassState =
            highPassCoefficient * highPassState
            + (1.0f - highPassCoefficient) * channelData[i];

        const float input = channelData[i] - highPassState;

        float wet = 0.0f;

        for (auto& spring : springs)
        {
            const float delayed = spring.delay.popSample(0);

            // La cascade de passe-tout : le cœur du son de ressort.
            float dispersed = delayed;

            for (auto& allpass : spring.allpasses)
                dispersed = allpass.process(dispersed, allpassCoefficient);

            // Amortissement (tone) : il n'agit QUE dans la boucle de
            // réinjection, pour que chaque répétition soit un peu plus
            // sombre que la précédente.
            spring.damperState +=
                damperCoefficient * (dispersed - spring.damperState);

            spring.delay.pushSample(
                0,
                softClip(input + spring.damperState * spring.feedback)
            );

            // On prélève AVANT l'amortissement : c'est ce qui préserve
            // la bande 1-4 kHz, là où vit le caractère métallique du
            // ressort.
            wet += dispersed;
        }

        channelData[i] = wet * outputScale;
    }
}
