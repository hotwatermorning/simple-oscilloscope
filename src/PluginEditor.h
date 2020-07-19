#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

//==============================================================================
class AudioPluginAudioProcessorEditor
:   public juce::AudioProcessorEditor
,   juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    juce::ComboBox cmb_duration_;
    juce::ToggleButton btn_left_pre_;
    juce::ToggleButton btn_left_post_;
    juce::ToggleButton btn_right_pre_;
    juce::ToggleButton btn_right_post_;
    juce::Slider sl_cutoff_;

    enum class DurationId : int {
        k10ms = 1,
        k100ms,
        k1s,
        k3s,
    };

    enum class ChannelId : int {
        kLeftPre = 0,
        kRightPre,
        kLeftPost,
        kRightPost
    };

    float * getBufferData(ChannelId);
    float const * getBufferData(ChannelId) const;

    juce::AudioSampleBuffer buffer_;
    juce::AudioThumbnailCache thumb_cache_;
    juce::AudioFormatManager afm_;
    juce::AudioThumbnail thumb_;
    std::int64_t saved_thumbnail_position_ = 0;
    std::int64_t saved_written_size_ = 0;
    double saved_sample_rate_ = 1.0;
    int saved_block_size_ = 0;
    DurationId dur_ = DurationId::k10ms;

    static
    int getSampleCountForDuration(double sample_rate, DurationId d);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
