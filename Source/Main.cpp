#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>

class MainComponent final
    : public juce::AudioAppComponent,
      private juce::Timer
{
public:
    MainComponent()
    {
        setSize(800, 780);

        addAndMakeVisible(recordButton);

        recordButton.onClick = [this]
        {
            if (isRecording.load())
            {
                // Enregistrement -> on arrête et on lance la lecture
                // en boucle de ce qui vient d'être capturé.
                isRecording.store(false);

                recordedLength = recordingWritePosition;

                // Fondu d'entrée/sortie pour que la boucle démarre et
                // finisse à zéro : supprime les clics au raccord.
                applyLoopFade();

                playbackPosition = 0;

                // isPlaying est publié en dernier : le thread audio ne
                // lira recordedLength/playbackPosition qu'après ce store.
                isPlaying.store(true);

                recordButton.setButtonText("Enregistrer");
            }
            else
            {
                // Arrêté ou en lecture -> on démarre un nouvel
                // enregistrement (par-dessus le précédent).
                isPlaying.store(false);

                // On repart de zéro AVANT d'activer, pendant que le
                // thread audio ignore encore le buffer.
                recordingWritePosition = 0;
                isRecording.store(true);

                recordButton.setButtonText("Arreter");
            }
        };

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

        // Potard de réglage fin : +/- un demi-demi-ton, appliqué en
        // décalage fractionnaire sur la vitesse de lecture.
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

        updatePlaybackSpeed();

        // --- Volume et panoramique de la boucle ---

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
            const int amount =
                juce::roundToInt(std::abs(value) * 100.0);

            if (amount == 0)
                return juce::String("C");

            return juce::String(value < 0.0 ? "G " : "D ")
                 + juce::String(amount);
        };

        panSlider.onValueChange = [this]
        {
            panPosition.store(
                static_cast<float>(panSlider.getValue())
            );
        };

        addAndMakeVisible(panLabel);
        panLabel.setText("Pan", juce::dontSendNotification);
        panLabel.setJustificationType(juce::Justification::centred);
        panLabel.attachToComponent(&panSlider, false);

        // --- Section filtre ---

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
            cutoffHz.store(
                static_cast<float>(cutoffSlider.getValue())
            );
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
        resonanceLabel.setJustificationType(
            juce::Justification::centred
        );
        resonanceLabel.attachToComponent(&resonanceSlider, false);

        addAndMakeVisible(speedLabel);
        speedLabel.setText("Vitesse", juce::dontSendNotification);
        speedLabel.setJustificationType(juce::Justification::centred);
        speedLabel.attachToComponent(&speedSlider, false);

        setAudioChannels(1, 2);

        // Rafraîchit l'interface environ 30 fois par seconde.
        startTimerHz(30);
    }

    ~MainComponent() override
    {
        shutdownAudio();
    }

    void prepareToPlay(
        int samplesPerBlockExpected,
        double sampleRate
    ) override
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize =
            static_cast<juce::uint32>(samplesPerBlockExpected);
        spec.numChannels = 1;

        filter.prepare(spec);
        filter.reset();

        // Le cutoff doit rester sous Nyquist.
        maxCutoffHz = juce::jmin(
            20000.0f,
            static_cast<float>(sampleRate) * 0.49f
        );

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

        fadeLengthSamples = static_cast<int>(
            sampleRate * fadeSeconds
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

        // Un changement de périphérique/sample rate réinitialise la
        // boucle en cours.
        isPlaying.store(false);
        recordedLength = 0;
        playbackPosition = 0;
    }

    void getNextAudioBlock(
        const juce::AudioSourceChannelInfo& bufferToFill
    ) override
    {
        auto* buffer = bufferToFill.buffer;

        if (buffer == nullptr)
            return;

        const int startSample = bufferToFill.startSample;
        const int numberOfSamples = bufferToFill.numSamples;

        const float rms = buffer->getRMSLevel(
            0,
            startSample,
            numberOfSamples
        );

        inputLevel.store(rms);

        writeInputToHistory(
            *buffer,
            startSample,
            numberOfSamples
        );

        if (isRecording.load())
        {
            appendToRecording(
                *buffer,
                startSample,
                numberOfSamples
            );
        }

        if (isPlaying.load())
        {
            readLoopIntoOutput(
                *buffer,
                startSample,
                numberOfSamples
            );
        }

        applyFilter(
            *buffer,
            startSample,
            numberOfSamples
        );

        if (isPlaying.load())
        {
            // Volume + pan : seulement sur la lecture de l'échantillon.
            applyGainAndPan(
                *buffer,
                startSample,
                numberOfSamples
            );
        }
        else if (buffer->getNumChannels() >= 2)
        {
            // Monitoring d'entrée : dual-mono à l'unité.
            buffer->copyFrom(
                1,
                startSample,
                *buffer,
                0,
                startSample,
                numberOfSamples
            );
        }
    }

    void releaseResources() override
    {
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(
            getLookAndFeel().findColour(
                juce::ResizableWindow::backgroundColourId
            )
        );

        auto bounds = getLocalBounds();
        bounds.removeFromBottom(480); // Espace réservé contrôles.
        bounds = bounds.reduced(40);

        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(28.0f));

        graphics.drawText(
            "Niveau d'entree",
            bounds.removeFromTop(60),
            juce::Justification::centred
        );

        auto meterBounds = bounds
            .withSizeKeepingCentre(80, 300)
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

    void resized() override
    {
        auto area = getLocalBounds();

        auto buttonRow = area.removeFromBottom(70);

        recordButton.setBounds(
            buttonRow.withSizeKeepingCentre(200, 40)
        );

        // Rangée mix : volume + panoramique de la boucle.
        auto mixRow = area.removeFromBottom(140);

        auto mixArea =
            mixRow.withSizeKeepingCentre(360, mixRow.getHeight());

        volumeSlider.setBounds(
            mixArea.removeFromLeft(180).reduced(20, 25)
        );

        panSlider.setBounds(mixArea.reduced(20, 25));

        // Rangée filtre : switch LP/HP + cutoff + résonance.
        auto filterRow = area.removeFromBottom(140);

        auto switchArea = filterRow.removeFromLeft(180);

        filterTypeButton.setBounds(
            switchArea.withSizeKeepingCentre(140, 40)
        );

        auto cutoffArea =
            filterRow.removeFromLeft(filterRow.getWidth() / 2);

        cutoffSlider.setBounds(cutoffArea.reduced(20, 25));
        resonanceSlider.setBounds(filterRow.reduced(20, 25));

        // Rangée vitesse : fader cranté + réglage fin.
        auto speedRow = area.removeFromBottom(130);

        auto knobColumn = speedRow.removeFromRight(130);

        fineTuneSlider.setBounds(
            knobColumn.reduced(15, 25)
        );

        speedSlider.setBounds(
            speedRow.reduced(40, 25)
        );
    }

private:
    void writeInputToHistory(
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

        const int remainingSamples =
            numberOfSamples - firstCopySize;

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
            (historyWritePosition + numberOfSamples)
            % historyBufferSize;
    }

    void appendToRecording(
        const juce::AudioBuffer<float>& sourceBuffer,
        int sourceStartSample,
        int numberOfSamples
    )
    {
        if (recordingCapacity <= 0)
            return;

        const int spaceLeft =
            recordingCapacity - recordingWritePosition;

        const int samplesToCopy =
            juce::jmin(numberOfSamples, spaceLeft);

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

    // Convertit l'index du slider (+ le réglage fin) en vitesse de
    // lecture. 0 -> arrêt ; sinon |index| donne le demi-ton
    // (magnitude - 37 => -36..+36), le réglage fin ajoute une fraction
    // de demi-ton, et le signe donne le sens (avant / inversé).
    static float speedFrom(int index, float fineTuneSemitones)
    {
        if (index == 0)
            return 0.0f;

        const int magnitude = std::abs(index);       // 1..73
        const int semitones = magnitude - (semitoneRange + 1); // -36..+36

        const float totalSemitones =
            static_cast<float>(semitones) + fineTuneSemitones;

        const float ratio = std::pow(2.0f, totalSemitones / 12.0f);

        return index > 0 ? ratio : -ratio;
    }

    // Recalcule la vitesse à partir des deux contrôles (thread message)
    // et la publie pour le thread audio.
    void updatePlaybackSpeed()
    {
        const int index =
            static_cast<int>(std::round(speedSlider.getValue()));

        const float fineTune =
            static_cast<float>(fineTuneSlider.getValue());

        playbackSpeed.store(speedFrom(index, fineTune));
    }

    // Texte affiché sous le slider pour un index donné.
    static juce::String speedText(int index)
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

    // Applique le volume puis répartit le mono du canal 0 en stéréo
    // selon le panoramique (loi à puissance constante : -3 dB au
    // centre, donc pas de creux de niveau perçu au milieu).
    void applyGainAndPan(
        juce::AudioBuffer<float>& buffer,
        int startSample,
        int numberOfSamples
    )
    {
        const float gain = playbackGain.load();
        const float pan = panPosition.load(); // -1..+1

        // pan -1..+1 -> angle 0..pi/2
        const float angle =
            (pan + 1.0f) * 0.25f
            * juce::MathConstants<float>::pi;

        const float leftGain = std::cos(angle) * gain;
        const float rightGain = std::sin(angle) * gain;

        auto* left = buffer.getWritePointer(0, startSample);

        if (buffer.getNumChannels() < 2)
        {
            juce::FloatVectorOperations::multiply(
                left,
                leftGain,
                numberOfSamples
            );

            return;
        }

        auto* right = buffer.getWritePointer(1, startSample);

        for (int i = 0; i < numberOfSamples; ++i)
        {
            const float sample = left[i];

            left[i] = sample * leftGain;
            right[i] = sample * rightGain;
        }
    }

    void applyFilter(
        juce::AudioBuffer<float>& buffer,
        int startSample,
        int numberOfSamples
    )
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

        auto* channelData = buffer.getWritePointer(0, startSample);

        for (int i = 0; i < numberOfSamples; ++i)
            channelData[i] = filter.processSample(0, channelData[i]);
    }

    // Replie un index dans [0, length) : la boucle est continue, donc
    // les voisins débordant d'un bout reviennent par l'autre.
    static int wrapIndex(int index, int length)
    {
        while (index < 0)
            index += length;

        while (index >= length)
            index -= length;

        return index;
    }

    void applyLoopFade()
    {
        if (recordedLength <= 0)
            return;

        // Fondu au plus égal à la moitié de l'échantillon.
        const int fade =
            juce::jmin(fadeLengthSamples, recordedLength / 2);

        if (fade <= 0)
            return;

        recordingBuffer.applyGainRamp(0, 0, fade, 0.0f, 1.0f);

        recordingBuffer.applyGainRamp(
            0,
            recordedLength - fade,
            fade,
            1.0f,
            0.0f
        );
    }

    void readLoopIntoOutput(
        juce::AudioBuffer<float>& destBuffer,
        int destStartSample,
        int numberOfSamples
    )
    {
        if (recordedLength <= 0)
            return;

        auto* destination =
            destBuffer.getWritePointer(0, destStartSample);

        const float speed = playbackSpeed.load();

        // Vitesse nulle (fader au centre) : le sample n'est pas lu.
        if (std::abs(speed) < 1.0e-6f)
        {
            juce::FloatVectorOperations::clear(
                destination,
                numberOfSamples
            );
            return;
        }

        const auto* source = recordingBuffer.getReadPointer(0);

        for (int i = 0; i < numberOfSamples; ++i)
        {
            // playbackPosition reste dans [0, recordedLength) : la
            // troncature équivaut au plancher.
            const int index = static_cast<int>(playbackPosition);
            const float fraction =
                static_cast<float>(playbackPosition - index);

            // Les 4 points encadrant la position (-1, 0, +1, +2).
            const float y0 =
                source[wrapIndex(index - 1, recordedLength)];
            const float y1 = source[index];
            const float y2 =
                source[wrapIndex(index + 1, recordedLength)];
            const float y3 =
                source[wrapIndex(index + 2, recordedLength)];

            // Interpolation Hermite 4 points, 3e ordre (Catmull-Rom).
            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 =
                y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 =
                0.5f * (y3 - y0) + 1.5f * (y1 - y2);

            destination[i] =
                ((c3 * fraction + c2) * fraction + c1) * fraction + c0;

            playbackPosition += speed;

            // Bouclage dans les deux sens (vitesse négative comprise).
            while (playbackPosition >= recordedLength)
                playbackPosition -= recordedLength;

            while (playbackPosition < 0.0)
                playbackPosition += recordedLength;
        }
    }

    void timerCallback() override
    {
        const float newLevel = inputLevel.load();

        // Monte rapidement, redescend plus doucement.
        if (newLevel > displayedLevel)
            displayedLevel = newLevel;
        else
            displayedLevel *= 0.90f;

        displayedLevel = juce::jlimit(
            0.0f,
            1.0f,
            displayedLevel
        );

        repaint();
    }

    std::atomic<float> inputLevel { 0.0f };
    float displayedLevel = 0.0f;

    juce::AudioBuffer<float> inputHistoryBuffer;

    int historyWritePosition = 0;
    int historyBufferSize = 0;

    static constexpr double historyDurationSeconds = 5.0;

    juce::TextButton recordButton { "Enregistrer" };

    std::atomic<bool> isRecording { false };

    juce::AudioBuffer<float> recordingBuffer;
    int recordingCapacity = 0;
    int recordingWritePosition = 0;

    static constexpr double maxRecordingSeconds = 60.0;

    std::atomic<bool> isPlaying { false };
    int recordedLength = 0;
    double playbackPosition = 0.0;

    juce::Slider speedSlider;
    juce::Label speedLabel;

    juce::Slider fineTuneSlider;
    juce::Label fineTuneLabel;

    std::atomic<float> playbackSpeed { 1.0f };

    static constexpr int semitoneRange = 36; // +/- trois octaves.

    int fadeLengthSamples = 0;
    static constexpr double fadeSeconds = 0.005; // ~5 ms.

    juce::Slider volumeSlider;
    juce::Label volumeLabel;
    juce::Slider panSlider;
    juce::Label panLabel;

    std::atomic<float> playbackGain { 1.0f };
    std::atomic<float> panPosition { 0.0f }; // -1 = gauche, +1 = droite.

    juce::dsp::StateVariableTPTFilter<float> filter;

    juce::TextButton filterTypeButton { "Passe-bas" };
    juce::Slider cutoffSlider;
    juce::Label cutoffLabel;
    juce::Slider resonanceSlider;
    juce::Label resonanceLabel;

    std::atomic<bool> highPassMode { false };
    std::atomic<float> cutoffHz { 20000.0f };
    std::atomic<float> resonanceValue { 0.707f };

    // Borne haute du cutoff, ajustée au sample rate (< Nyquist).
    float maxCutoffHz = 20000.0f;
};

class DroneLooperApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return "Drone Looper";
    }

    const juce::String getApplicationVersion() override
    {
        return "0.2.0";
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(
            getApplicationName()
        );
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(
                  name,
                  juce::Colours::black,
                  DocumentWindow::allButtons
              )
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()
                ->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(DroneLooperApplication)