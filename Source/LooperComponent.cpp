#include "LooperComponent.h"

#include <cmath>

LooperComponent::LooperComponent(const juce::String& titleToUse)
    : title(titleToUse)
{
    buildSpeedControls();
    buildFilterControls();
    buildDelayControls();
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

void LooperComponent::buildDelayControls()
{
    auto configureKnob = [this](juce::Slider& slider, juce::Label& label,
                                const juce::String& text)
    {
        addAndMakeVisible(slider);
        slider.setSliderStyle(
            juce::Slider::RotaryHorizontalVerticalDrag
        );
        slider.setTextBoxStyle(
            juce::Slider::TextBoxBelow,
            false,
            70,
            18
        );

        addAndMakeVisible(label);
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.attachToComponent(&slider, false);
    };

    configureKnob(delayTimeSlider, delayTimeLabel, "Time");
    delayTimeSlider.setRange(10.0, maxDelaySeconds * 1000.0);
    delayTimeSlider.setSkewFactorFromMidPoint(300.0);
    delayTimeSlider.setTextValueSuffix(" ms");
    delayTimeSlider.setValue(300.0);

    delayTimeSlider.onValueChange = [this]
    {
        delayTimeMs.store(
            static_cast<float>(delayTimeSlider.getValue())
        );
    };

    configureKnob(feedbackSlider, feedbackLabel, "Feedback");
    feedbackSlider.setRange(0.0, 100.0, 1.0);
    feedbackSlider.setTextValueSuffix(" %");
    feedbackSlider.setValue(30.0);

    feedbackSlider.onValueChange = [this]
    {
        feedbackAmount.store(
            static_cast<float>(feedbackSlider.getValue()) * 0.01f
        );
    };

    // Tone : passe-bas sur la première moitié, passe-haut sur la
    // seconde. Le centre est neutre.
    configureKnob(toneSlider, toneLabel, "Tone");
    toneSlider.setRange(-1.0, 1.0, 0.01);
    toneSlider.setValue(0.0);
    toneSlider.setDoubleClickReturnValue(true, 0.0);

    toneSlider.textFromValueFunction = [](double value)
    {
        const int amount = juce::roundToInt(std::abs(value) * 100.0);

        if (amount == 0)
            return juce::String("Neutre");

        return juce::String(value < 0.0 ? "LP " : "HP ")
             + juce::String(amount);
    };

    toneSlider.onValueChange = [this]
    {
        toneValue.store(static_cast<float>(toneSlider.getValue()));
    };

    // Mix à 0 par défaut : le delay est présent mais inaudible tant
    // qu'on ne l'ouvre pas.
    configureKnob(delayMixSlider, delayMixLabel, "Mix");
    delayMixSlider.setRange(0.0, 100.0, 1.0);
    delayMixSlider.setTextValueSuffix(" %");
    delayMixSlider.setValue(0.0);

    delayMixSlider.onValueChange = [this]
    {
        delayMix.store(
            static_cast<float>(delayMixSlider.getValue()) * 0.01f
        );
    };
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
        70,
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
        70,
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

    // Send vers la réverbe globale.
    addAndMakeVisible(reverbSendSlider);
    reverbSendSlider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag
    );
    reverbSendSlider.setRange(0.0, 100.0, 1.0);
    reverbSendSlider.setTextValueSuffix(" %");
    reverbSendSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow,
        false,
        70,
        18
    );
    reverbSendSlider.setValue(0.0);

    reverbSendSlider.onValueChange = [this]
    {
        reverbSend.store(
            static_cast<float>(reverbSendSlider.getValue()) * 0.01f
        );
    };

    addAndMakeVisible(reverbSendLabel);
    reverbSendLabel.setText("Send", juce::dontSendNotification);
    reverbSendLabel.setJustificationType(juce::Justification::centred);
    reverbSendLabel.attachToComponent(&reverbSendSlider, false);
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
    // Empilement vertical : le composant est pensé pour vivre dans une
    // colonne étroite (4 côte à côte). Les espaces laissés avant chaque
    // contrôle accueillent son label, attaché juste au-dessus.
    auto area = getLocalBounds().reduced(8);

    area.removeFromTop(26); // Titre (dessiné dans paint).

    // Vitesse : fader cranté pleine largeur, puis réglage fin.
    area.removeFromTop(18);
    speedSlider.setBounds(area.removeFromTop(36));

    area.removeFromTop(24);
    auto fineRow = area.removeFromTop(84);
    fineTuneSlider.setBounds(fineRow.withSizeKeepingCentre(90, 84));

    // Filtre : switch LP/HP, puis cutoff + résonance.
    area.removeFromTop(10);
    filterTypeButton.setBounds(
        area.removeFromTop(32).reduced(20, 0)
    );

    area.removeFromTop(24);
    auto filterKnobs = area.removeFromTop(90);
    auto cutoffArea =
        filterKnobs.removeFromLeft(filterKnobs.getWidth() / 2);

    cutoffSlider.setBounds(cutoffArea.reduced(6, 0));
    resonanceSlider.setBounds(filterKnobs.reduced(6, 0));

    // Delay : time + feedback, puis tone + mix.
    area.removeFromTop(24);
    auto delayTopRow = area.removeFromTop(90);
    auto timeArea =
        delayTopRow.removeFromLeft(delayTopRow.getWidth() / 2);

    delayTimeSlider.setBounds(timeArea.reduced(6, 0));
    feedbackSlider.setBounds(delayTopRow.reduced(6, 0));

    area.removeFromTop(24);
    auto delayBottomRow = area.removeFromTop(90);
    auto toneArea =
        delayBottomRow.removeFromLeft(delayBottomRow.getWidth() / 2);

    toneSlider.setBounds(toneArea.reduced(6, 0));
    delayMixSlider.setBounds(delayBottomRow.reduced(6, 0));

    // Mix : volume + panoramique + send réverbe.
    area.removeFromTop(24);
    auto mixKnobs = area.removeFromTop(90);
    const int mixKnobWidth = mixKnobs.getWidth() / 3;

    volumeSlider.setBounds(
        mixKnobs.removeFromLeft(mixKnobWidth).reduced(3, 0)
    );

    panSlider.setBounds(
        mixKnobs.removeFromLeft(mixKnobWidth).reduced(3, 0)
    );

    reverbSendSlider.setBounds(mixKnobs.reduced(3, 0));
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
// Presets
//==============================================================================

LooperState LooperComponent::captureState() const
{
    LooperState state;

    state.speedIndex =
        static_cast<int>(std::round(speedSlider.getValue()));
    state.fineTune = fineTuneSlider.getValue();

    state.highPass = filterTypeButton.getToggleState();
    state.cutoffHz = cutoffSlider.getValue();
    state.resonance = resonanceSlider.getValue();

    state.delayTimeMs = delayTimeSlider.getValue();
    state.feedbackPercent = feedbackSlider.getValue();
    state.tone = toneSlider.getValue();
    state.delayMixPercent = delayMixSlider.getValue();

    state.volumeDecibels = volumeSlider.getValue();
    state.pan = panSlider.getValue();
    state.reverbSendPercent = reverbSendSlider.getValue();

    return state;
}

void LooperComponent::applyState(const LooperState& state)
{
    // dontSendNotification : c'est le moteur de presets qui publie les
    // valeurs, pas les callbacks des sliders. Sinon on créerait une
    // boucle de réaction entre l'affichage et les paramètres.
    speedSlider.setValue(
        state.speedIndex,
        juce::dontSendNotification
    );

    fineTuneSlider.setValue(
        state.fineTune,
        juce::dontSendNotification
    );

    filterTypeButton.setToggleState(
        state.highPass,
        juce::dontSendNotification
    );

    filterTypeButton.setButtonText(
        state.highPass ? "Passe-haut" : "Passe-bas"
    );

    cutoffSlider.setValue(state.cutoffHz, juce::dontSendNotification);

    resonanceSlider.setValue(
        state.resonance,
        juce::dontSendNotification
    );

    delayTimeSlider.setValue(
        state.delayTimeMs,
        juce::dontSendNotification
    );

    feedbackSlider.setValue(
        state.feedbackPercent,
        juce::dontSendNotification
    );

    toneSlider.setValue(state.tone, juce::dontSendNotification);

    delayMixSlider.setValue(
        state.delayMixPercent,
        juce::dontSendNotification
    );

    volumeSlider.setValue(
        state.volumeDecibels,
        juce::dontSendNotification
    );

    panSlider.setValue(state.pan, juce::dontSendNotification);

    reverbSendSlider.setValue(
        state.reverbSendPercent,
        juce::dontSendNotification
    );

    // Publication vers le thread audio.
    updatePlaybackSpeed();

    highPassMode.store(state.highPass);
    cutoffHz.store(static_cast<float>(state.cutoffHz));
    resonanceValue.store(static_cast<float>(state.resonance));

    delayTimeMs.store(static_cast<float>(state.delayTimeMs));
    feedbackAmount.store(
        static_cast<float>(state.feedbackPercent) * 0.01f
    );
    toneValue.store(static_cast<float>(state.tone));
    delayMix.store(
        static_cast<float>(state.delayMixPercent) * 0.01f
    );

    playbackGain.store(
        juce::Decibels::decibelsToGain(
            static_cast<float>(state.volumeDecibels),
            -60.0f
        )
    );

    panPosition.store(static_cast<float>(state.pan));
    reverbSend.store(
        static_cast<float>(state.reverbSendPercent) * 0.01f
    );
}

void LooperComponent::applyMorphedState(
    const LooperState& from,
    const LooperState& to,
    double progress
)
{
    const double t = juce::jlimit(0.0, 1.0, progress);

    auto lerp = [t](double a, double b)
    {
        return a + t * (b - a);
    };

    // Vitesse : glissando continu dans l'espace des VITESSES. On
    // interpole le ratio de lecture lui-même, pas le nombre de
    // demi-tons — d'où la sensation d'accélération / décélération.
    const float speedA = speedFrom(
        from.speedIndex,
        static_cast<float>(from.fineTune)
    );

    const float speedB = speedFrom(
        to.speedIndex,
        static_cast<float>(to.fineTune)
    );

    playbackSpeed.store(
        static_cast<float>(lerp(speedA, speedB))
    );

    // Le type de filtre ne s'interpole pas : il bascule à la fin.
    highPassMode.store(t >= 1.0 ? to.highPass : from.highPass);

    cutoffHz.store(
        static_cast<float>(lerp(from.cutoffHz, to.cutoffHz))
    );

    resonanceValue.store(
        static_cast<float>(lerp(from.resonance, to.resonance))
    );

    delayTimeMs.store(
        static_cast<float>(lerp(from.delayTimeMs, to.delayTimeMs))
    );

    feedbackAmount.store(
        static_cast<float>(
            lerp(from.feedbackPercent, to.feedbackPercent)
        ) * 0.01f
    );

    toneValue.store(
        static_cast<float>(lerp(from.tone, to.tone))
    );

    delayMix.store(
        static_cast<float>(
            lerp(from.delayMixPercent, to.delayMixPercent)
        ) * 0.01f
    );

    playbackGain.store(
        juce::Decibels::decibelsToGain(
            static_cast<float>(
                lerp(from.volumeDecibels, to.volumeDecibels)
            ),
            -60.0f
        )
    );

    panPosition.store(
        static_cast<float>(lerp(from.pan, to.pan))
    );

    reverbSend.store(
        static_cast<float>(
            lerp(from.reverbSendPercent, to.reverbSendPercent)
        ) * 0.01f
    );
}

void LooperComponent::updateControlsForMorph(
    const LooperState& from,
    const LooperState& to,
    double progress
)
{
    const double t = juce::jlimit(0.0, 1.0, progress);

    auto lerp = [t](double a, double b)
    {
        return a + t * (b - a);
    };

    auto show = [](juce::Slider& slider, double value)
    {
        slider.setValue(value, juce::dontSendNotification);
    };

    // Le fader de vitesse est cranté : il ne peut pas afficher la
    // valeur continue du glissando. Il suit donc le cran le plus
    // proche, à titre indicatif.
    show(
        speedSlider,
        std::round(lerp(from.speedIndex, to.speedIndex))
    );

    show(fineTuneSlider, lerp(from.fineTune, to.fineTune));
    show(cutoffSlider, lerp(from.cutoffHz, to.cutoffHz));
    show(resonanceSlider, lerp(from.resonance, to.resonance));
    show(delayTimeSlider, lerp(from.delayTimeMs, to.delayTimeMs));

    show(
        feedbackSlider,
        lerp(from.feedbackPercent, to.feedbackPercent)
    );

    show(toneSlider, lerp(from.tone, to.tone));

    show(
        delayMixSlider,
        lerp(from.delayMixPercent, to.delayMixPercent)
    );

    show(
        volumeSlider,
        lerp(from.volumeDecibels, to.volumeDecibels)
    );

    show(panSlider, lerp(from.pan, to.pan));

    show(
        reverbSendSlider,
        lerp(from.reverbSendPercent, to.reverbSendPercent)
    );
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

    currentSampleRate = sampleRate;

    delayLine.setMaximumDelayInSamples(
        static_cast<int>(sampleRate * maxDelaySeconds) + 1
    );

    delayLine.prepare(spec);
    delayLine.reset();

    toneFilter.prepare(spec);
    toneFilter.reset();
    toneFilter.setResonance(0.707f);

    // Rampe de 50 ms sur le temps de delay : évite le clic quand on
    // tourne le potard.
    smoothedDelaySamples.reset(sampleRate, 0.05);
    smoothedDelaySamples.setCurrentAndTargetValue(
        static_cast<float>(delayTimeMs.load() * 0.001 * sampleRate)
    );

    // Le cutoff doit rester sous Nyquist.
    maxCutoffHz = juce::jmin(
        20000.0f,
        static_cast<float>(sampleRate) * 0.49f
    );

    fadeLengthSamples = static_cast<int>(sampleRate * fadeSeconds);

    // Scratch mono : cette voix y construit son signal avant de
    // l'additionner dans la sortie partagée.
    renderBuffer.setSize(1, maximumBlockSize, false, true, false);
    renderBuffer.clear();

    playbackPosition = 0.0;
}

void LooperComponent::startPlayback(int sampleLength)
{
    if (sampleLength <= 0)
    {
        portionStart = 0;
        portionLength = 0;

        return;
    }

    // Longueur minimale : de quoi loger le fondu d'entrée/sortie, et de
    // quoi alimenter l'interpolation Hermite (4 points).
    const int minPortionSamples =
        juce::jmax(64, fadeLengthSamples * 2);

    // On utilise le RNG système plutôt qu'un juce::Random par voix :
    // quatre instances construites dans la même milliseconde
    // pourraient être semées identiquement et tirer les MÊMES portions.
    auto& random = juce::Random::getSystemRandom();

    const double fraction =
        minPortionFraction
        + (1.0 - minPortionFraction) * random.nextDouble();

    portionLength = juce::jlimit(
        juce::jmin(minPortionSamples, sampleLength),
        sampleLength,
        juce::roundToInt(sampleLength * fraction)
    );

    portionStart = random.nextInt(sampleLength - portionLength + 1);

    playbackPosition = 0.0;

    filter.reset();

    // Purge la queue du delay de la boucle précédente.
    delayLine.reset();
    toneFilter.reset();
}

float LooperComponent::fadeGainAt(double positionInPortion) const
{
    const int fade = juce::jmin(fadeLengthSamples, portionLength / 2);

    if (fade <= 0)
        return 1.0f;

    if (positionInPortion < fade)
    {
        return static_cast<float>(positionInPortion)
             / static_cast<float>(fade);
    }

    const double distanceToEnd = portionLength - positionInPortion;

    if (distanceToEnd < fade)
    {
        return static_cast<float>(distanceToEnd)
             / static_cast<float>(fade);
    }

    return 1.0f;
}

void LooperComponent::renderNextBlock(
    juce::AudioBuffer<float>& outputBuffer,
    juce::AudioBuffer<float>& reverbSendBuffer,
    int startSample,
    int numberOfSamples,
    const float* sampleData,
    int sampleLength
)
{
    if (sampleData == nullptr || sampleLength <= 0)
        return;

    // La portion doit être valide et tenir dans l'échantillon.
    if (portionLength <= 0
        || portionStart < 0
        || portionStart + portionLength > sampleLength)
    {
        return;
    }

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
        speed
    );

    applyFilter(numberOfSamples);
    applyDelay(numberOfSamples);

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

    // Send vers la réverbe, pris après volume et pan : le placement
    // stéréo de la voix est donc conservé dans la réverbe.
    const float send = reverbSend.load();

    if (send <= 0.0f || reverbSendBuffer.getNumChannels() < 2)
        return;

    reverbSendBuffer.addFrom(
        0,
        0,
        mono,
        numberOfSamples,
        leftGain * send
    );

    reverbSendBuffer.addFrom(
        1,
        0,
        mono,
        numberOfSamples,
        rightGain * send
    );
}

void LooperComponent::readSampleIntoRenderBuffer(
    int numberOfSamples,
    const float* sampleData,
    float speed
)
{
    auto* destination = renderBuffer.getWritePointer(0);

    for (int i = 0; i < numberOfSamples; ++i)
    {
        // playbackPosition est RELATIVE à la portion et reste dans
        // [0, portionLength) : la troncature équivaut au plancher.
        const int index = static_cast<int>(playbackPosition);
        const float fraction =
            static_cast<float>(playbackPosition - index);

        // Les 4 points encadrant la position (-1, 0, +1, +2). Ils se
        // replient DANS la portion : c'est elle, la boucle.
        const float y0 =
            sampleData[portionStart + wrapIndex(index - 1, portionLength)];
        const float y1 =
            sampleData[portionStart + index];
        const float y2 =
            sampleData[portionStart + wrapIndex(index + 1, portionLength)];
        const float y3 =
            sampleData[portionStart + wrapIndex(index + 2, portionLength)];

        // Interpolation Hermite 4 points, 3e ordre (Catmull-Rom).
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        const float interpolated =
            ((c3 * fraction + c2) * fraction + c1) * fraction + c0;

        // Enveloppe anti-clic : la portion démarre et finit à zéro.
        destination[i] =
            interpolated * fadeGainAt(playbackPosition);

        playbackPosition += speed;

        // Bouclage dans les deux sens (vitesse négative comprise).
        while (playbackPosition >= portionLength)
            playbackPosition -= portionLength;

        while (playbackPosition < 0.0)
            playbackPosition += portionLength;
    }
}

void LooperComponent::applyDelay(int numberOfSamples)
{
    const float mix = delayMix.load();
    const float feedback =
        juce::jlimit(0.0f, maxFeedback, feedbackAmount.load());

    smoothedDelaySamples.setTargetValue(
        static_cast<float>(
            delayTimeMs.load() * 0.001f * currentSampleRate
        )
    );

    // Tone : passe-bas sur la première moitié du potard, passe-haut sur
    // la seconde. Au centre (0), le passe-bas est à 20 kHz : neutre.
    const float tone = toneValue.load();

    if (tone <= 0.0f)
    {
        toneFilter.setType(
            juce::dsp::StateVariableTPTFilterType::lowpass
        );

        // 20 kHz au centre -> 200 Hz à fond à gauche.
        const float cutoff = 20000.0f * std::pow(0.01f, -tone);

        toneFilter.setCutoffFrequency(
            juce::jlimit(20.0f, maxCutoffHz, cutoff)
        );
    }
    else
    {
        toneFilter.setType(
            juce::dsp::StateVariableTPTFilterType::highpass
        );

        // 20 Hz au centre -> 5 kHz à fond à droite.
        const float cutoff = 20.0f * std::pow(250.0f, tone);

        toneFilter.setCutoffFrequency(
            juce::jlimit(20.0f, maxCutoffHz, cutoff)
        );
    }

    // --- Saturation du signal retardé ---
    //
    // Le feedback pilote son intensité : à feedback bas elle reste très
    // discrète. Le mix la conditionne (delay fermé = rien à saturer).
    const float amount =
        mix * (saturationFeedbackFloor
               + (1.0f - saturationFeedbackFloor) * feedback);

    const float drive =
        1.0f + amount * (maxSaturationDrive - 1.0f);

    // Couleur pilotée par le tone :
    //   tone < 0 (passe-bas)  -> chaud : asymétrique et doux
    //   tone > 0 (passe-haut) -> froid : symétrique et dur
    const float warmth = juce::jmax(0.0f, -tone);
    const float coldness = juce::jmax(0.0f, tone);

    const float bias = warmBias * warmth * amount;

    // L'asymétrie décale le signal : sans cette soustraction, une
    // composante continue se propagerait jusqu'au master.
    const float dcOffset = shape(bias, coldness);

    // À faible niveau la courbe est quasi linéaire (pente = drive) :
    // diviser par le drive garde le volume constant et ne laisse que la
    // compression des crêtes et les harmoniques ajoutées.
    const float makeup = 1.0f / drive;

    auto* channelData = renderBuffer.getWritePointer(0);

    for (int i = 0; i < numberOfSamples; ++i)
    {
        delayLine.setDelay(smoothedDelaySamples.getNextValue());

        const float dry = channelData[i];
        const float delayed = delayLine.popSample(0);

        // Réinjection : le tone est DANS la boucle (chaque répétition
        // est filtrée une fois de plus), mais PAS la saturation de
        // couleur, qui se cumulerait à chaque tour.
        const float fedBack = toneFilter.processSample(0, delayed);

        // L'écrêteur borne ce qui entre dans la ligne : c'est lui qui
        // rend le feedback à 100 % stable au lieu de divergent.
        delayLine.pushSample(
            0,
            softClip(dry + fedBack * feedback)
        );

        // Saturation appliquée au seul signal retardé. Le fondu par
        // amount garantit qu'à saturation nulle le delay reste
        // parfaitement propre (la courbe n'est pas l'identité à
        // drive = 1).
        const float driven = delayed * drive + bias;

        const float saturated =
            (shape(driven, coldness) - dcOffset) * makeup;

        const float wet =
            delayed + amount * (saturated - delayed);

        channelData[i] = dry + wet * mix;
    }
}

float LooperComponent::softClip(float x)
{
    const float magnitude = std::abs(x);

    // En dessous du seuil : strictement l'identité. La boucle reste
    // donc parfaitement propre tant qu'elle ne s'emballe pas.
    if (magnitude <= loopClipThreshold)
        return x;

    const float headroom = 1.0f - loopClipThreshold;

    // Au-dessus : saturation douce, asymptotique à 1,0. C'est ce qui
    // fait qu'une boucle à gain unitaire se stabilise au lieu de
    // diverger.
    const float excess = (magnitude - loopClipThreshold) / headroom;

    const float clipped =
        loopClipThreshold + headroom * std::tanh(excess);

    return x < 0.0f ? -clipped : clipped;
}

float LooperComponent::shape(float x, float coldness)
{
    // Chaud : courbe douce, saturation progressive.
    const float soft = std::tanh(x);

    // Froid : écrêtage franc, plus cassant.
    const float hard = juce::jlimit(-1.0f, 1.0f, x);

    return soft + coldness * (hard - soft);
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
