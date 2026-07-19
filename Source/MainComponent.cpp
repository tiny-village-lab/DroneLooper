#include "MainComponent.h"

MainComponent::MainComponent()
{
    for (int i = 0; i < numberOfLoopers; ++i)
    {
        auto* looper = loopers.add(
            new LooperComponent("Looper " + juce::String(i + 1))
        );

        addAndMakeVisible(looper);
    }

    addAndMakeVisible(recordButton);
    recordButton.onClick = [this] { toggleRecording(); };

    // --- Presets A/B/C/D ---

    addAndMakeVisible(storeButton);
    storeButton.setClickingTogglesState(true);
    storeButton.setColour(
        juce::TextButton::buttonOnColourId,
        juce::Colours::orangered
    );

    storeButton.onClick = [this] { refreshPresetButtons(); };

    for (int i = 0; i < numberOfPresets; ++i)
    {
        auto* button = presetButtons.add(
            new juce::TextButton(
                juce::String::charToString(
                    static_cast<juce::juce_wchar>('A' + i)
                )
            )
        );

        addAndMakeVisible(button);

        button->onClick = [this, i]
        {
            if (storeButton.getToggleState())
            {
                storePreset(i);

                // Le mode mémorisation se désarme après usage.
                storeButton.setToggleState(
                    false,
                    juce::dontSendNotification
                );
            }
            else
            {
                recallPreset(i);
            }

            refreshPresetButtons();
        };
    }

    refreshPresetButtons();

    addAndMakeVisible(transitionTimeSlider);
    transitionTimeSlider.setSliderStyle(
        juce::Slider::LinearHorizontal
    );
    transitionTimeSlider.setRange(0.0, 30.0, 0.1);
    transitionTimeSlider.setTextValueSuffix(" s");
    transitionTimeSlider.setValue(0.0);
    transitionTimeSlider.setTextBoxStyle(
        juce::Slider::TextBoxRight,
        false,
        70,
        20
    );

    addAndMakeVisible(transitionTimeLabel);
    transitionTimeLabel.setText(
        "Transition",
        juce::dontSendNotification
    );
    transitionTimeLabel.setJustificationType(
        juce::Justification::centred
    );
    transitionTimeLabel.attachToComponent(
        &transitionTimeSlider,
        false
    );

    addAndMakeVisible(masterVolumeSlider);
    masterVolumeSlider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag
    );
    masterVolumeSlider.setRange(-60.0, 6.0, 0.1);
    masterVolumeSlider.setTextValueSuffix(" dB");
    masterVolumeSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow,
        false,
        90,
        18
    );
    masterVolumeSlider.setValue(defaultMasterDecibels);
    masterVolumeSlider.setDoubleClickReturnValue(
        true,
        defaultMasterDecibels
    );

    masterVolumeSlider.onValueChange = [this]
    {
        masterGain.store(
            juce::Decibels::decibelsToGain(
                static_cast<float>(masterVolumeSlider.getValue()),
                -60.0f
            )
        );
    };

    addAndMakeVisible(masterVolumeLabel);
    masterVolumeLabel.setText("Master", juce::dontSendNotification);
    masterVolumeLabel.setJustificationType(
        juce::Justification::centred
    );
    masterVolumeLabel.attachToComponent(&masterVolumeSlider, false);

    // --- Réverbe à ressort ---

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
            80,
            18
        );

        addAndMakeVisible(label);
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.attachToComponent(&slider, false);
    };

    configureKnob(reverbToneSlider, reverbToneLabel, "Rev Tone");
    reverbToneSlider.setRange(0.0, 100.0, 1.0);
    reverbToneSlider.setTextValueSuffix(" %");
    reverbToneSlider.setValue(50.0);

    reverbToneSlider.onValueChange = [this]
    {
        reverbTone.store(
            static_cast<float>(reverbToneSlider.getValue()) * 0.01f
        );
    };

    configureKnob(reverbTimeSlider, reverbTimeLabel, "Rev Time");
    reverbTimeSlider.setRange(0.3, 16.0, 0.1);
    reverbTimeSlider.setSkewFactorFromMidPoint(3.0);
    reverbTimeSlider.setTextValueSuffix(" s");
    reverbTimeSlider.setValue(2.0);

    reverbTimeSlider.onValueChange = [this]
    {
        reverbTime.store(
            static_cast<float>(reverbTimeSlider.getValue())
        );
    };

    addAndMakeVisible(reverbSoloButton);
    reverbSoloButton.setClickingTogglesState(true);
    reverbSoloButton.setColour(
        juce::TextButton::buttonOnColourId,
        juce::Colours::orangered
    );

    reverbSoloButton.onClick = [this]
    {
        reverbSolo.store(reverbSoloButton.getToggleState());
    };

    // setSize déclenche resized() : il doit venir APRÈS la création des
    // enfants, sinon les loopers n'existent pas encore et restent sans
    // bounds.
    setSize(1200, 1100);

    // On demande deux canaux d'entrée ; le périphérique n'en fournira
    // peut-être qu'un (micro d'iPad, par exemple). prepareToPlay lit le
    // nombre réel et s'y adapte.
    setAudioChannels(2, 2);

    // Rafraîchit l'interface environ 30 fois par seconde.
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

//==============================================================================
// Enregistrement / lecture (thread message)
//==============================================================================

void MainComponent::toggleRecording()
{
    if (isRecording.load())
    {
        // Enregistrement -> on arrête et on lance la lecture en boucle
        // de ce qui vient d'être capturé.
        isRecording.store(false);

        recordedLength = recordingWritePosition;

        // Le thread audio n'appelle pas les loopers tant que isPlaying
        // est faux : on peut les réinitialiser ici sans risque. Chaque
        // voix tire au sort la portion qu'elle va boucler.
        for (auto* looper : loopers)
            looper->startPlayback(recordedLength);

        // isPlaying est publié en dernier : le thread audio ne lira
        // recordedLength ni l'état des voix qu'après ce store.
        isPlaying.store(true);

        recordButton.setButtonText("Enregistrer");
    }
    else
    {
        // Arrêté ou en lecture -> on démarre un nouvel enregistrement
        // (par-dessus le précédent).
        isPlaying.store(false);

        // On repart de zéro AVANT d'activer, pendant que le thread
        // audio ignore encore le buffer.
        recordingWritePosition = 0;
        isRecording.store(true);

        recordButton.setButtonText("Arreter");
    }
}

//==============================================================================
// Presets
//==============================================================================

void MainComponent::storePreset(int index)
{
    auto& preset = presets[static_cast<size_t>(index)];

    preset.loopers.clear();

    for (auto* looper : loopers)
        preset.loopers.push_back(looper->captureState());

    preset.hasContent = true;
}

void MainComponent::recallPreset(int index)
{
    const auto& preset = presets[static_cast<size_t>(index)];

    // Un emplacement jamais mémorisé n'a rien à rappeler.
    if (! preset.hasContent)
        return;

    if (preset.loopers.size() != static_cast<size_t>(loopers.size()))
        return;

    const double duration = transitionTimeSlider.getValue();

    if (duration <= 0.0)
    {
        // Transition instantanée.
        morphing = false;

        for (int i = 0; i < loopers.size(); ++i)
        {
            loopers[i]->applyState(
                preset.loopers[static_cast<size_t>(i)]
            );
        }

        return;
    }

    // On part de l'état affiché à l'instant du clic : une transition
    // interrompue par une autre repart donc du bon endroit.
    morphFrom.clear();

    for (auto* looper : loopers)
        morphFrom.push_back(looper->captureState());

    morphTo = preset.loopers;

    morphDurationSeconds = duration;
    morphProgress = 0.0;
    morphUiCounter = 0;
    morphing = true;
}

void MainComponent::advanceMorph()
{
    if (! morphing)
        return;

    // Le timer bat à 30 Hz.
    morphProgress += (1.0 / 30.0) / morphDurationSeconds;

    const bool finished = morphProgress >= 1.0;

    if (finished)
        morphProgress = 1.0;

    for (int i = 0; i < loopers.size(); ++i)
    {
        const auto index = static_cast<size_t>(i);

        if (index >= morphFrom.size() || index >= morphTo.size())
            break;

        loopers[i]->applyMorphedState(
            morphFrom[index],
            morphTo[index],
            morphProgress
        );
    }

    ++morphUiCounter;

    if (finished || (morphUiCounter % morphUiInterval) == 0)
    {
        for (int i = 0; i < loopers.size(); ++i)
        {
            const auto index = static_cast<size_t>(i);

            if (index >= morphFrom.size() || index >= morphTo.size())
                break;

            loopers[i]->updateControlsForMorph(
                morphFrom[index],
                morphTo[index],
                morphProgress
            );
        }
    }

    if (! finished)
        return;

    // Arrivé à destination : on recale exactement sur la cible, ce qui
    // remet notamment le fader de vitesse sur son cran.
    for (int i = 0; i < loopers.size(); ++i)
    {
        const auto index = static_cast<size_t>(i);

        if (index >= morphTo.size())
            break;

        loopers[i]->applyState(morphTo[index]);
    }

    morphing = false;
}

void MainComponent::refreshPresetButtons()
{
    const bool storing = storeButton.getToggleState();

    for (int i = 0; i < presetButtons.size(); ++i)
    {
        auto* button = presetButtons[i];

        const bool filled =
            presets[static_cast<size_t>(i)].hasContent;

        // Emplacement vide : grisé, sauf en mode mémorisation où il
        // devient une cible valide.
        button->setEnabled(storing || filled);

        button->setColour(
            juce::TextButton::buttonColourId,
            filled ? juce::Colours::steelblue
                   : juce::Colours::darkgrey
        );
    }
}

//==============================================================================
// Audio
//==============================================================================

void MainComponent::prepareToPlay(
    int samplesPerBlockExpected,
    double sampleRate
)
{
    activeInputChannels = 1;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        activeInputChannels = juce::jmax(
            1,
            device->getActiveInputChannels().countNumberOfSetBits()
        );
    }

    monoInputBuffer.setSize(
        1,
        samplesPerBlockExpected,
        false,
        true,
        false
    );

    monoInputBuffer.clear();

    historyBufferSize = static_cast<int>(
        sampleRate * historyDurationSeconds
    );

    inputHistoryBuffer.setSize(
        1,
        historyBufferSize,
        false,
        true,
        false
    );

    inputHistoryBuffer.clear();

    historyWritePosition = 0;

    recordingCapacity = static_cast<int>(
        sampleRate * maxRecordingSeconds
    );

    recordingBuffer.setSize(
        1,
        recordingCapacity,
        false,
        true,
        false
    );

    recordingBuffer.clear();

    recordingWritePosition = 0;

    // Un changement de périphérique/sample rate réinitialise la boucle
    // en cours.
    isPlaying.store(false);
    recordedLength = 0;

    for (auto* looper : loopers)
        looper->prepare(sampleRate, samplesPerBlockExpected);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize =
        static_cast<juce::uint32>(samplesPerBlockExpected);
    spec.numChannels = 2;

    limiter.prepare(spec);
    limiter.reset();
    limiter.setThreshold(-1.0f);
    limiter.setRelease(100.0f);

    reverbSendBuffer.setSize(
        2,
        samplesPerBlockExpected,
        false,
        true,
        false
    );

    reverbSendBuffer.clear();

    // Facteurs volontairement non harmoniquement liés : c'est ce qui
    // décorrèle les deux canaux et ouvre l'image stéréo. Avec des
    // ressorts identiques, la réverbe serait du dual-mono.
    reverbLeft.prepare(sampleRate, samplesPerBlockExpected, 0.87f);
    reverbRight.prepare(sampleRate, samplesPerBlockExpected, 1.23f);
}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill
)
{
    auto* buffer = bufferToFill.buffer;

    if (buffer == nullptr)
        return;

    const int startSample = bufferToFill.startSample;
    const int numberOfSamples = bufferToFill.numSamples;

    // Le scratch mono est pré-alloué : si le bloc dépasse la taille
    // annoncée, on sort en silence plutôt que de déborder.
    if (numberOfSamples > monoInputBuffer.getNumSamples())
    {
        buffer->clear(startSample, numberOfSamples);

        return;
    }

    // L'entrée est encore dans le buffer : on la consomme d'abord, en
    // la réduisant à un canal.
    buildMonoInput(*buffer, startSample, numberOfSamples);

    const float rms =
        monoInputBuffer.getRMSLevel(0, 0, numberOfSamples);

    inputLevel.store(rms);

    writeInputToHistory(monoInputBuffer, 0, numberOfSamples);

    if (isRecording.load())
        appendToRecording(monoInputBuffer, 0, numberOfSamples);

    if (isPlaying.load())
    {
        // La sortie est la SOMME des voix : on part du silence, puis
        // chaque looper ajoute sa contribution.
        buffer->clear(startSample, numberOfSamples);

        // Le bus de réverbe repart du silence à chaque bloc : les voix
        // y additionnent leur send.
        reverbSendBuffer.clear(0, numberOfSamples);

        const float* sampleData = recordingBuffer.getReadPointer(0);

        for (auto* looper : loopers)
        {
            looper->renderNextBlock(
                *buffer,
                reverbSendBuffer,
                startSample,
                numberOfSamples,
                sampleData,
                recordedLength
            );
        }

        // Solo : on coupe le signal direct. C'est fait APRÈS le rendu
        // des voix, donc les sends ont déjà été prélevés : la réverbe
        // reste alimentée normalement.
        if (reverbSolo.load())
            buffer->clear(startSample, numberOfSamples);

        // Retour de réverbe : deux instances mono indépendantes, une
        // par canal. Le retour est à l'unité ; c'est le send de chaque
        // voix qui dose la quantité de réverbe.
        //
        // La réverbe tourne en continu, même sans send actif : couper
        // son traitement tronquerait brutalement la queue en cours.
        const float tone = reverbTone.load();
        const float time = reverbTime.load();

        reverbLeft.setTone(tone);
        reverbLeft.setTime(time);
        reverbRight.setTone(tone);
        reverbRight.setTime(time);

        reverbLeft.process(
            reverbSendBuffer.getWritePointer(0),
            numberOfSamples
        );

        reverbRight.process(
            reverbSendBuffer.getWritePointer(1),
            numberOfSamples
        );

        for (int channel = 0;
             channel < juce::jmin(2, buffer->getNumChannels());
             ++channel)
        {
            buffer->addFrom(
                channel,
                startSample,
                reverbSendBuffer,
                channel,
                0,
                numberOfSamples
            );
        }

        buffer->applyGain(
            startSample,
            numberOfSamples,
            masterGain.load()
        );

        // Filet de sécurité contre la saturation numérique.
        juce::dsp::AudioBlock<float> block(*buffer);

        auto subBlock = block.getSubBlock(
            static_cast<size_t>(startSample),
            static_cast<size_t>(numberOfSamples)
        );

        juce::dsp::ProcessContextReplacing<float> context(subBlock);
        limiter.process(context);
    }
    else
    {
        // Monitoring d'entrée : l'entrée mono à l'unité sur les deux
        // canaux, non traitée.
        for (int channel = 0;
             channel < buffer->getNumChannels();
             ++channel)
        {
            buffer->copyFrom(
                channel,
                startSample,
                monoInputBuffer,
                0,
                0,
                numberOfSamples
            );
        }
    }
}

void MainComponent::buildMonoInput(
    const juce::AudioBuffer<float>& inputBuffer,
    int startSample,
    int numberOfSamples
)
{
    auto* mono = monoInputBuffer.getWritePointer(0);

    if (inputBuffer.getNumChannels() <= 0)
    {
        juce::FloatVectorOperations::clear(mono, numberOfSamples);

        return;
    }

    // Entrée stéréo : on somme L+R plutôt que d'ignorer la droite. Le
    // facteur 0,5 évite la saturation quand les deux canaux sont
    // corrélés (cas fréquent).
    if (activeInputChannels >= 2 && inputBuffer.getNumChannels() >= 2)
    {
        const auto* left = inputBuffer.getReadPointer(0, startSample);
        const auto* right = inputBuffer.getReadPointer(1, startSample);

        for (int i = 0; i < numberOfSamples; ++i)
            mono[i] = 0.5f * (left[i] + right[i]);

        return;
    }

    // Entrée mono : copie directe.
    juce::FloatVectorOperations::copy(
        mono,
        inputBuffer.getReadPointer(0, startSample),
        numberOfSamples
    );
}

void MainComponent::releaseResources()
{
}

void MainComponent::writeInputToHistory(
    const juce::AudioBuffer<float>& sourceBuffer,
    int sourceStartSample,
    int numberOfSamples
)
{
    if (historyBufferSize <= 0)
        return;

    const int samplesUntilEnd =
        historyBufferSize - historyWritePosition;

    const int firstCopySize =
        juce::jmin(numberOfSamples, samplesUntilEnd);

    inputHistoryBuffer.copyFrom(
        0,
        historyWritePosition,
        sourceBuffer,
        0,
        sourceStartSample,
        firstCopySize
    );

    const int remainingSamples = numberOfSamples - firstCopySize;

    if (remainingSamples > 0)
    {
        inputHistoryBuffer.copyFrom(
            0,
            0,
            sourceBuffer,
            0,
            sourceStartSample + firstCopySize,
            remainingSamples
        );
    }

    historyWritePosition =
        (historyWritePosition + numberOfSamples) % historyBufferSize;
}

void MainComponent::appendToRecording(
    const juce::AudioBuffer<float>& sourceBuffer,
    int sourceStartSample,
    int numberOfSamples
)
{
    if (recordingCapacity <= 0)
        return;

    const int spaceLeft = recordingCapacity - recordingWritePosition;
    const int samplesToCopy = juce::jmin(numberOfSamples, spaceLeft);

    if (samplesToCopy <= 0)
        return; // Buffer plein : on ignore (pas d'allocation).

    recordingBuffer.copyFrom(
        0,
        recordingWritePosition,
        sourceBuffer,
        0,
        sourceStartSample,
        samplesToCopy
    );

    recordingWritePosition += samplesToCopy;
}

//==============================================================================
// Interface
//==============================================================================

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(
        getLookAndFeel().findColour(
            juce::ResizableWindow::backgroundColourId
        )
    );

    auto bounds = meterArea.reduced(20);

    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(22.0f));

    graphics.drawText(
        "Niveau d'entree",
        bounds.removeFromTop(30),
        juce::Justification::centred
    );

    // Charge CPU : part du budget de callback audio consommée.
    const float cpuPercent = displayedCpuUsage * 100.0f;

    graphics.setColour(
        cpuPercent > 70.0f ? juce::Colours::orangered
                           : juce::Colours::lightgrey
    );

    graphics.setFont(juce::FontOptions(15.0f));

    graphics.drawText(
        "CPU audio : " + juce::String(cpuPercent, 1) + " %",
        bounds.removeFromBottom(24),
        juce::Justification::centred
    );

    auto meterBounds = bounds
        .withSizeKeepingCentre(60, bounds.getHeight())
        .toFloat();

    graphics.setColour(juce::Colours::darkgrey);
    graphics.fillRoundedRectangle(meterBounds, 8.0f);

    const float level = displayedLevel;

    auto activeBounds = meterBounds;

    activeBounds.removeFromTop(
        activeBounds.getHeight() * (1.0f - level)
    );

    graphics.setColour(juce::Colours::limegreen);
    graphics.fillRoundedRectangle(activeBounds, 8.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto buttonRow = area.removeFromBottom(70);

    // Rangée presets : A/B/C/D, mémorisation, temps de transition.
    auto presetRow = area.removeFromBottom(90);
    presetRow.removeFromTop(24); // Place du label du slider.

    auto presetArea = presetRow.removeFromLeft(560).reduced(20, 8);

    storeButton.setBounds(presetArea.removeFromLeft(140).reduced(6, 0));

    const int presetWidth =
        presetArea.getWidth() / juce::jmax(1, presetButtons.size());

    for (auto* button : presetButtons)
    {
        button->setBounds(
            presetArea.removeFromLeft(presetWidth).reduced(6, 0)
        );
    }

    transitionTimeSlider.setBounds(
        presetRow.removeFromLeft(430).reduced(20, 10)
    );

    recordButton.setBounds(
        buttonRow.withSizeKeepingCentre(200, 40)
    );

    auto topRow = area.removeFromTop(150);

    // À droite : master + les deux réglages de réverbe.
    auto knobStrip = topRow.removeFromRight(510);
    const int topKnobWidth = knobStrip.getWidth() / 3;

    masterVolumeSlider.setBounds(
        knobStrip.removeFromLeft(topKnobWidth).reduced(25, 28)
    );

    reverbToneSlider.setBounds(
        knobStrip.removeFromLeft(topKnobWidth).reduced(25, 28)
    );

    reverbTimeSlider.setBounds(knobStrip.reduced(25, 28));

    auto soloArea = topRow.removeFromRight(130);

    reverbSoloButton.setBounds(
        soloArea.withSizeKeepingCentre(110, 36)
    );

    meterArea = topRow;

    // Les voix se répartissent la largeur restante, en colonnes.
    const int columnWidth =
        area.getWidth() / juce::jmax(1, loopers.size());

    for (auto* looper : loopers)
        looper->setBounds(area.removeFromLeft(columnWidth));
}

void MainComponent::timerCallback()
{
    const float newLevel = inputLevel.load();

    // Monte rapidement, redescend plus doucement.
    if (newLevel > displayedLevel)
        displayedLevel = newLevel;
    else
        displayedLevel *= 0.90f;

    displayedLevel = juce::jlimit(0.0f, 1.0f, displayedLevel);

    displayedCpuUsage =
        static_cast<float>(deviceManager.getCpuUsage());

    advanceMorph();

    repaint(meterArea);
}
