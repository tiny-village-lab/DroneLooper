#include "LooperComponent.h"

#include <cmath>

LooperComponent::LooperComponent(const juce::String& titleToUse)
    : title(titleToUse)
{
    buildSpeedControls();
    buildFilterControls();
    buildMixControls();

    updatePlaybackSpeed();
}

//==============================================================================
// Construction de l'interface
//==============================================================================

void LooperComponent::buildSpeedControls()
{
    addAndMakeVisible(speedSlider);
    speedSlider.setSliderStyle(juce::Slider::LinearHorizontal);

    // Slider cranté : chaque pas entier = un demi-ton.
    // 0 = arrêt ; +/- = avant/inversé. 73 crans par côté
    // (demi-tons -36..+36).
    speedSlider.setRange(
        -(2 * semitoneRange + 1),
        2 * semitoneRange + 1,
        1.0
    );
    speedSlider.setDoubleClickReturnValue(true, 0.0);
    speedSlider.setTextBoxIsEditable(false);

    speedSlider.textFromValueFunction = [](double value)
    {
        return speedText(static_cast<int>(std::round(value)));
    };

    speedSlider.onValueChange = [this] { updatePlaybackSpeed(); };

    addAndMakeVisible(speedLabel);
    speedLabel.setText("Vitesse", juce::dontSendNotification);
    speedLabel.setJustificationType(juce::Justification::centred);
    speedLabel.attachToComponent(&speedSlider, false);

    // Réglage fin : +/- un demi-demi-ton, appliqué en décalage
    // fractionnaire sur la vitesse de lecture.
    addAndMakeVisible(fineTuneSlider);
    fineTuneSlider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag
    );
    fineTuneSlider.setRange(-0.5, 0.5, 0.01);
    fineTuneSlider.setValue(0.0);
    fineTuneSlider.setDoubleClickReturnValue(true, 0.0);
    fineTuneSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow,
        false,
        70,
        18
    );

    fineTuneSlider.onValueChange = [this] { updatePlaybackSpeed(); };

    addAndMakeVisible(fineTuneLabel);
    fineTuneLabel.setText("Fin", juce::dontSendNotification);
    fineTuneLabel.setJustificationType(juce::Justification::centred);
    fineTuneLabel.attachToComponent(&fineTuneSlider, false);

    // Défaut : +37 -> 0 demi-ton -> vitesse normale (1x).
    speedSlider.setValue(semitoneRange + 1);
}

void LooperComponent::buildFilterControls()
{
    addAndMakeVisible(filterTypeButton);
    filterTypeButton.setClickingTogglesState(true);

    filterTypeButton.onClick = [this]
    {
        const bool highPass = filterTypeButton.getToggleState();

        highPassMode.store(highPass);

        filterTypeButton.setButtonText(
            highPass ? "Passe-haut" : "Passe-bas"
        );
    };

    addAndMakeVisible(cutoffSlider);
    cutoffSlider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag
    );
    cutoffSlider.setRange(20.0, 20000.0);
    cutoffSlider.setSkewFactorFromMidPoint(1000.0);
    cutoffSlider.setTextValueSuffix(" Hz");
    cutoffSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow,
        false,
        90,
        18
    );
    cutoffSlider.setValue(20000.0);

    cutoffSlider.onValueChange = [this]
    {
        cutoffHz.store(static_cast<float>(cutoffSlider.getValue()));
    };

    addAndMakeVisible(cutoffLabel);
    cutoffLabel.setText("Cutoff", juce::dontSendNotification);
    cutoffLabel.setJustificationType(juce::Justification::centred);
    cutoffLabel.attachToComponent(&cutoffSlider, false);

    addAndMakeVisible(resonanceSlider);
    resonanceSlider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag
    );
    resonanceSlider.setRange(0.5, 10.0);
    resonanceSlider.setSkewFactorFromMidPoint(2.0);
    resonanceSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow,
        false,
        90,
        18
    );
    resonanceSlider.setValue(0.707);

    resonanceSlider.onValueChange = [this]
    {
        resonanceValue.store(
            static_cast<float>(resonanceSlider.getValue())
        );
    };

    addAndMakeVisible(resonanceLabel);
    resonanceLabel.setText("Resonance", juce::dontSendNotification);
    resonanceLabel.setJustificationType(juce::Justification::centred);
    resonanceLabel.attachToComponent(&resonanceSlider, false);
}

void LooperComponent::buildMixControls()
{
    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag
    );
    volumeSlider.setRange(-60.0, 6.0, 0.1);
    volumeSlider.setTextValueSuffix(" dB");
    volumeSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow,
        false,
        90,
        18
    );
    volumeSlider.setValue(0.0);
    volumeSlider.setDoubleClickReturnValue(true, 0.0);

    volumeSlider.onValueChange = [this]
    {
        // -60 dB est traité comme silence total.
        playbackGain.store(
            juce::Decibels::decibelsToGain(
                static_cast<float>(volumeSlider.getValue()),
                -60.0f
            )
        );
    };

    addAndMakeVisible(volumeLabel);
    volumeLabel.setText("Volume", juce::dontSendNotification);
    volumeLabel.setJustificationType(juce::Justification::centred);
    volumeLabel.attachToComponent(&volumeSlider, false);

    addAndMakeVisible(panSlider);
    panSlider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag
    );
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow,
        false,
        90,
        18
    );
    panSlider.setValue(0.0);
    panSlider.setDoubleClickReturnValue(true, 0.0);

    panSlider.textFromValueFunction = [](double value)
    {
        const int amount = juce::roundToInt(std::abs(value) * 100.0);

        if (amount == 0)
            return juce::String("C");

        return juce::String(value < 0.0 ? "G " : "D ")
             + juce::String(amount);
    };

    panSlider.onValueChange = [this]
    {
        panPosition.store(static_cast<float>(panSlider.getValue()));
    };

    addAndMakeVisible(panLabel);
    panLabel.setText("Pan", juce::dontSendNotification);
    panLabel.setJustificationType(juce::Justification::centred);
    panLabel.attachToComponent(&panSlider, false);
}

//==============================================================================
// Interface
//==============================================================================

void LooperComponent::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    graphics.setColour(juce::Colours::white.withAlpha(0.06f));
    graphics.fillRoundedRectangle(bounds, 6.0f);

    graphics.setColour(juce::Colours::white.withAlpha(0.25f));
    graphics.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(16.0f));

    graphics.drawText(
        title,
        getLocalBounds().removeFromTop(28),
        juce::Justification::centred
    );
}

void LooperComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    area.removeFromTop(24); // Place du titre (dessiné dans paint).

    // Vitesse : fader cranté + réglage fin.
    auto speedRow = area.removeFromTop(110);
    auto fineArea = speedRow.removeFromRight(110);

    fineTuneSlider.setBounds(fineArea.reduced(5, 22));
    speedSlider.setBounds(speedRow.reduced(10, 22));

    // Filtre : switch LP/HP, puis cutoff + résonance.
    auto filterRow = area.removeFromTop(140);

    filterTypeButton.setBounds(
        filterRow.removeFromTop(34).reduced(10, 0)
    );

    auto cutoffArea = filterRow.removeFromLeft(filterRow.getWidth() / 2);

    cutoffSlider.setBounds(cutoffArea.reduced(8, 22));
    resonanceSlider.setBounds(filterRow.reduced(8, 22));

    // Mix : volume + panoramique.
    auto mixRow = area.removeFromTop(130);
    auto volumeArea = mixRow.removeFromLeft(mixRow.getWidth() / 2);

    volumeSlider.setBounds(volumeArea.reduced(8, 22));
    panSlider.setBounds(mixRow.reduced(8, 22));
}

//==============================================================================
// Paramètres
//==============================================================================

void LooperComponent::updatePlaybackSpeed()
{
    const int index =
        static_cast<int>(std::round(speedSlider.getValue()));

    const float fineTune =
        static_cast<float>(fineTuneSlider.getValue());

    playbackSpeed.store(speedFrom(index, fineTune));
}

float LooperComponent::speedFrom(int index, float fineTuneSemitones)
{
    if (index == 0)
        return 0.0f;

    const int magnitude = std::abs(index);                 // 1..73
    const int semitones = magnitude - (semitoneRange + 1); // -36..+36

    const float totalSemitones =
        static_cast<float>(semitones) + fineTuneSemitones;

    const float ratio = std::pow(2.0f, totalSemitones / 12.0f);

    return index > 0 ? ratio : -ratio;
}

juce::String LooperComponent::speedText(int index)
{
    if (index == 0)
        return "Arret";

    const int magnitude = std::abs(index);
    const int semitones = magnitude - (semitoneRange + 1);

    juce::String text;

    if (semitones > 0)
        text << "+";

    text << semitones << " st";

    if (index < 0)
        text << " (inv)";

    return text;
}

int LooperComponent::wrapIndex(int index, int length)
{
    while (index < 0)
        index += length;

    while (index >= length)
        index -= length;

    return index;
}

//==============================================================================
// Audio
//==============================================================================

void LooperComponent::prepare(double sampleRate, int maximumBlockSize)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize =
        static_cast<juce::uint32>(maximumBlockSize);
    spec.numChannels = 1;

    filter.prepare(spec);
    filter.reset();

    // Le cutoff doit rester sous Nyquist.
    maxCutoffHz = juce::jmin(
        20000.0f,
        static_cast<float>(sampleRate) * 0.49f
    );

    // Scratch mono : cette voix y construit son signal avant de
    // l'additionner dans la sortie partagée.
    renderBuffer.setSize(1, maximumBlockSize, false, true, false);
    renderBuffer.clear();

    playbackPosition = 0.0;
}

void LooperComponent::resetPlayback()
{
    playbackPosition = 0.0;
    filter.reset();
}

void LooperComponent::renderNextBlock(
    juce::AudioBuffer<float>& outputBuffer,
    int startSample,
    int numberOfSamples,
    const float* sampleData,
    int sampleLength
)
{
    if (sampleData == nullptr || sampleLength <= 0)
        return;

    // Le scratch est pré-alloué : on ne dépasse jamais sa taille.
    if (numberOfSamples > renderBuffer.getNumSamples())
        return;

    const float speed = playbackSpeed.load();

    // Vitesse nulle (fader au centre) : cette voix ne joue rien.
    if (std::abs(speed) < 1.0e-6f)
        return;

    readSampleIntoRenderBuffer(
        numberOfSamples,
        sampleData,
        sampleLength,
        speed
    );

    applyFilter(numberOfSamples);

    // Volume, puis répartition stéréo à puissance constante (-3 dB au
    // centre, donc pas de creux de niveau perçu au milieu).
    const float gain = playbackGain.load();
    const float pan = panPosition.load();

    const float angle =
        (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;

    const float leftGain = std::cos(angle) * gain;
    const float rightGain = std::sin(angle) * gain;

    const auto* mono = renderBuffer.getReadPointer(0);

    // On ADDITIONNE : les autres voix écrivent dans le même buffer.
    outputBuffer.addFrom(
        0,
        startSample,
        mono,
        numberOfSamples,
        leftGain
    );

    if (outputBuffer.getNumChannels() >= 2)
    {
        outputBuffer.addFrom(
            1,
            startSample,
            mono,
            numberOfSamples,
            rightGain
        );
    }
}

void LooperComponent::readSampleIntoRenderBuffer(
    int numberOfSamples,
    const float* sampleData,
    int sampleLength,
    float speed
)
{
    auto* destination = renderBuffer.getWritePointer(0);

    for (int i = 0; i < numberOfSamples; ++i)
    {
        // playbackPosition reste dans [0, sampleLength) : la troncature
        // équivaut au plancher.
        const int index = static_cast<int>(playbackPosition);
        const float fraction =
            static_cast<float>(playbackPosition - index);

        // Les 4 points encadrant la position (-1, 0, +1, +2).
        const float y0 = sampleData[wrapIndex(index - 1, sampleLength)];
        const float y1 = sampleData[index];
        const float y2 = sampleData[wrapIndex(index + 1, sampleLength)];
        const float y3 = sampleData[wrapIndex(index + 2, sampleLength)];

        // Interpolation Hermite 4 points, 3e ordre (Catmull-Rom).
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        destination[i] =
            ((c3 * fraction + c2) * fraction + c1) * fraction + c0;

        playbackPosition += speed;

        // Bouclage dans les deux sens (vitesse négative comprise).
        while (playbackPosition >= sampleLength)
            playbackPosition -= sampleLength;

        while (playbackPosition < 0.0)
            playbackPosition += sampleLength;
    }
}

void LooperComponent::applyFilter(int numberOfSamples)
{
    // Paramètres publiés par l'UI : relus une fois par bloc.
    filter.setType(
        highPassMode.load()
            ? juce::dsp::StateVariableTPTFilterType::highpass
            : juce::dsp::StateVariableTPTFilterType::lowpass
    );

    filter.setCutoffFrequency(
        juce::jlimit(20.0f, maxCutoffHz, cutoffHz.load())
    );

    filter.setResonance(resonanceValue.load());

    auto* channelData = renderBuffer.getWritePointer(0);

    for (int i = 0; i < numberOfSamples; ++i)
        channelData[i] = filter.processSample(0, channelData[i]);
}
