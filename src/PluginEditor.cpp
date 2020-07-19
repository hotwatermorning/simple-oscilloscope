#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cassert>
#include <thread>

constexpr int kButtonHeight = 20;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
:   AudioProcessorEditor(&p)
,   processorRef (p)
,   thumb_cache_(5)
,   thumb_(1.0, afm_, thumb_cache_)
{
    juce::ignoreUnused (processorRef);

    addAndMakeVisible(cmb_duration_);
    addAndMakeVisible(btn_left_pre_);
    addAndMakeVisible(btn_left_post_);
    addAndMakeVisible(btn_right_pre_);
    addAndMakeVisible(btn_right_post_);
    addAndMakeVisible(sl_cutoff_);

    cmb_duration_.addItem("10 ms",  (int)DurationId::k10ms);
    cmb_duration_.addItem("100 ms", (int)DurationId::k100ms);
    cmb_duration_.addItem("1 s",  (int)DurationId::k1s);
    cmb_duration_.addItem("3 s",  (int)DurationId::k3s);
    cmb_duration_.setSelectedId((int)dur_);
    cmb_duration_.onChange = [this] {
        auto id = cmb_duration_.getSelectedId();
        if(id != 0) {
            dur_ = (DurationId)id;
        }

        repaint();
    };

    btn_left_pre_.setButtonText("Left Pre");
    btn_left_post_.setButtonText("Left Post");
    btn_right_pre_.setButtonText("Right Pre");
    btn_right_post_.setButtonText("Right Post");
    btn_left_pre_.setToggleState(true, juce::dontSendNotification);
    btn_left_post_.setToggleState(true, juce::dontSendNotification);

    sl_cutoff_.valueFromTextFunction = [this](juce::String const &str) -> double {
        return processorRef.stringToFloat(str);
    };

    sl_cutoff_.textFromValueFunction = [this](double value) -> juce::String {
        return processorRef.floatToString(value, 8);
    };

    sl_cutoff_.onDragStart = [this] {
        processorRef.cutoff_->beginChangeGesture();
    };

    sl_cutoff_.onDragEnd = [this] {
        processorRef.cutoff_->endChangeGesture();
    };

    sl_cutoff_.onValueChange = [this] {
        processorRef.cutoff_->setValueNotifyingHost(sl_cutoff_.getValue());
    };

    sl_cutoff_.setRange(0.0, 1.0);
    sl_cutoff_.setValue(0.5);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (800, 300);
    setResizeLimits(400, 300, 1920, 1200);
    setResizable(true, true);

    startTimer(16);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // g.fillAll(juce::Colour(0.0f, 0.0f, 0.2f));

    //g.setOrigin(0, kButtonHeight);
    int const w = getWidth();
    int const h = getHeight() - kButtonHeight;

    auto num_to_draw = getSampleCountForDuration(saved_sample_rate_, dur_);
    int const start_index = buffer_.getNumSamples() - num_to_draw;
    auto draw_start_time = (saved_thumbnail_position_ - num_to_draw) / saved_sample_rate_;
    auto draw_end_time = (saved_thumbnail_position_) / saved_sample_rate_;

    juce::Rectangle<int> b_waveform = getBounds();
    b_waveform.removeFromTop(kButtonHeight);

    auto draw_waveform = [&, this](float hue, ChannelId ch) {
        g.setColour (juce::Colour(hue, 0.7f, 0.9f, 1.0f));
        thumb_.drawChannel(g, b_waveform, draw_start_time, draw_end_time, (int)ch, 1.0);
    };

    if(btn_left_pre_.getToggleState()) {
        draw_waveform(0.0, ChannelId::kLeftPre);
    }

    if(btn_left_post_.getToggleState()) {
        draw_waveform(0.5, ChannelId::kLeftPost);
    }

    if(btn_right_pre_.getToggleState()) {
        draw_waveform(0.25, ChannelId::kRightPre);
    }

    if(btn_right_post_.getToggleState()) {
        draw_waveform(0.75, ChannelId::kRightPost);
    }
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto b = getBounds().removeFromTop(kButtonHeight);
    int const kButtonWidth = b.getWidth() / 6.0;

    cmb_duration_.setBounds(b.removeFromLeft(kButtonWidth));
    btn_left_pre_.setBounds(b.removeFromLeft(kButtonWidth));
    btn_left_post_.setBounds(b.removeFromLeft(kButtonWidth));
    btn_right_pre_.setBounds(b.removeFromLeft(kButtonWidth));
    btn_right_post_.setBounds(b.removeFromLeft(kButtonWidth));
    sl_cutoff_.setBounds(b.removeFromLeft(kButtonWidth));
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    auto const new_channel_count = processorRef.getTotalNumOutputChannels();
    auto const new_sample_rate = processorRef.getSampleRate();
    auto const new_block_size = processorRef.getBlockSize();

    assert(new_channel_count == 2);

    if(saved_sample_rate_ != new_sample_rate ||
       saved_block_size_ != new_block_size )
    {
        saved_sample_rate_ = new_sample_rate;
        saved_block_size_ = new_block_size;

        auto const max_samples = getSampleCountForDuration(new_sample_rate, DurationId::k3s);
        buffer_ = juce::AudioSampleBuffer(new_channel_count * 2, max_samples);
        thumb_.reset(new_channel_count * 2, saved_sample_rate_, max_samples);

        thumb_.addBlock(0, buffer_, 0, buffer_.getNumSamples());
        saved_thumbnail_position_ = buffer_.getNumSamples();
        saved_written_size_ = 0;
    }

    AudioData *ad = nullptr;
    std::unique_lock<AudioData> lock;

    // Processor の active_buffer_ を取得して、
    for( ; ; ) {
        ad = processorRef.getActiveAudioData();
        if(ad == nullptr) { return; }

        lock = std::unique_lock<AudioData>(*ad, std::try_to_lock);

        // ロックを取得できたので、 GUI スレッドでこの AudioData を使用する。
        if(lock) { break; }

        // ロックが取得できなかったということは、この AudioData が processor_ 側で使用中ということ。
        // （ロック処理には juce::SpinLock を使用していて、このクラスは Spurious Failure が発生しないはず）
        // 一度スレッドのタイムスライスを明け渡してからリトライする。
        std::this_thread::yield();
    }

    assert(lock.owns_lock());

    // ロックを取得したままの状態で、データのコピーを行う。

    auto &apre = ad->getPreBuffer();
    auto &apost = ad->getPostBuffer();

    assert(apre.getNumWritten() == apost.getNumWritten());

    auto const new_written_size = apre.getNumWritten();
    auto num_progressed = std::max<std::int64_t>(new_written_size, saved_written_size_) - saved_written_size_;
    std::int64_t num_to_read = std::min<std::int64_t>(num_progressed, apre.getNumSamples());
    std::int64_t num_to_discard = buffer_.getNumSamples() - num_to_read;
    saved_written_size_ = new_written_size;

    ad->getPreBuffer().read(buffer_.getArrayOfWritePointers(), 0, num_to_read);
    ad->getPostBuffer().read(buffer_.getArrayOfWritePointers() + 2, 0, num_to_read);

    lock.unlock();

    thumb_.addBlock(saved_thumbnail_position_, buffer_, 0, num_to_read);
    saved_thumbnail_position_ += num_to_read;

    repaint(getBounds().withTrimmedTop(kButtonHeight));
}

float * AudioPluginAudioProcessorEditor::getBufferData(ChannelId ch)
{
    return buffer_.getWritePointer((int)ch);
}

float const * AudioPluginAudioProcessorEditor::getBufferData(ChannelId ch) const
{
    return buffer_.getReadPointer((int)ch);
}

int AudioPluginAudioProcessorEditor::getSampleCountForDuration(double sample_rate, DurationId d)
{
    double ratio = 0.0;

    switch(d) {
        case DurationId::k10ms:     ratio = 0.01; break;
        case DurationId::k100ms:    ratio = 0.1; break;
        case DurationId::k1s:       ratio = 1.0; break;
        case DurationId::k3s:       ratio = 3.0; break;
        default: assert("unknown duration id" && false);
     }

    return (int)std::round(sample_rate * ratio);
}
