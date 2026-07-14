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

    // setSize déclenche resized() : il doit venir APRÈS la création des
    // enfants, sinon les loopers n'existent pas encore et restent sans
    // bounds.
    setSize(1200, 1010);

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

        const float* sampleData = recordingBuffer.getReadPointer(0);

        for (auto* looper : loopers)
        {
            looper->renderNextBlock(
                *buffer,
                startSample,
                numberOfSamples,
                sampleData,
                recordedLength
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

    recordButton.setBounds(
        buttonRow.withSizeKeepingCentre(200, 40)
    );

    auto topRow = area.removeFromTop(150);

    auto masterArea = topRow.removeFromRight(180);
    masterVolumeSlider.setBounds(masterArea.reduced(35, 30));

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

    repaint(meterArea);
}
