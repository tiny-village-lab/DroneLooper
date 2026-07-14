#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>

class MainComponent final
    : public juce::AudioAppComponent,
      private juce::Timer
{
public:
    MainComponent()
    {
        setSize(800, 500);

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
        // 0 = arrêt ; +/- = avant/inversé. 25 crans par côté
        // (demi-tons -12..+12).
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

        speedSlider.onValueChange = [this]
        {
            playbackSpeed.store(
                speedFromIndex(
                    static_cast<int>(std::round(speedSlider.getValue()))
                )
            );
        };

        // Défaut : +13 -> 0 demi-ton -> vitesse normale (1x).
        speedSlider.setValue(semitoneRange + 1);

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
        juce::ignoreUnused(samplesPerBlockExpected);

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

        if (buffer->getNumChannels() >= 2)
        {
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
        bounds.removeFromBottom(160); // Espace réservé slider + bouton.
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

        auto bottomStrip = area.removeFromBottom(160);

        auto buttonRow = bottomStrip.removeFromBottom(70);

        recordButton.setBounds(
            buttonRow.withSizeKeepingCentre(200, 40)
        );

        speedSlider.setBounds(
            bottomStrip.reduced(40, 20)
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

    // Convertit l'index du slider en vitesse de lecture.
    // 0 -> arrêt ; sinon |index| donne le demi-ton (magnitude - 13 =>
    // -12..+12) et le signe donne le sens (avant / inversé).
    static float speedFromIndex(int index)
    {
        if (index == 0)
            return 0.0f;

        const int magnitude = std::abs(index);       // 1..25
        const int semitones = magnitude - (semitoneRange + 1); // -12..+12

        const float ratio =
            std::pow(2.0f, static_cast<float>(semitones) / 12.0f);

        return index > 0 ? ratio : -ratio;
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
            const int index0 = static_cast<int>(playbackPosition);
            const float fraction =
                static_cast<float>(playbackPosition - index0);

            int index1 = index0 + 1;
            if (index1 >= recordedLength)
                index1 = 0; // Interpolation continue au raccord.

            // Interpolation linéaire pour les vitesses fractionnaires.
            destination[i] =
                source[index0] * (1.0f - fraction)
                + source[index1] * fraction;

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
    std::atomic<float> playbackSpeed { 1.0f };

    static constexpr int semitoneRange = 12; // +/- une octave.

    int fadeLengthSamples = 0;
    static constexpr double fadeSeconds = 0.005; // ~5 ms.
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