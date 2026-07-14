#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

class MainComponent final
    : public juce::AudioAppComponent,
      private juce::Timer
{
public:
    MainComponent()
    {
        setSize(800, 500);
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

        auto bounds = getLocalBounds().reduced(40);

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