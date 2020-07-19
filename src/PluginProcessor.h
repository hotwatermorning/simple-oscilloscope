#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "RingBuffer.hpp"
#include <atomic>
#include <memory>

// オーディオスレッドと GUI スレッドで共有するデータを表すクラス
struct AudioData
{
    // エフェクト処理前のサンプルを表すリングバッファを返す。
    RingBuffer<float> & getPreBuffer() { return pre_buffer_; }
    // エフェクト処理前のサンプルを表すリングバッファを返す。
    RingBuffer<float> const & getPreBuffer() const { return pre_buffer_; }
    // エフェクト処理後のサンプルを表すリングバッファを返す。
    RingBuffer<float> & getPostBuffer() { return post_buffer_; }
    // エフェクト処理後のサンプルを表すリングバッファを返す。
    RingBuffer<float> const & getPostBuffer() const { return post_buffer_; }

    // Support Lockable Concept of C++ Standard.
    void lock() { lock_.enter(); }
    void unlock() { lock_.exit(); }
    bool try_lock() { return lock_.tryEnter(); }

private:
    juce::SpinLock lock_;
    RingBuffer<float> pre_buffer_;
    RingBuffer<float> post_buffer_;
};

//==============================================================================
class AudioPluginAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // 最新のデータが書き込まれた AudioData を取得する。
    AudioData * getActiveAudioData() { return active_audio_data_.load(); }

    // フィルタのカットオフ周波数を変更するためのパラメータ。
    // 0.0 .. 1.0 の範囲の値を取り、 20 [Hz] .. sampleRate / 2.0 [Hz] の範囲のカットオフ周波数を表す。
    juce::AudioParameterFloat *cutoff_;

    float HzToParam(float Hz) const;
    float paramToHz(float value) const;
    juce::String floatToString(float value, int maximumStringLength) const;
    float stringToFloat(juce::String const &str) const;
private:
    juce::AudioSampleBuffer tmp_buf_;
    AudioData datas_[2];
    std::atomic<AudioData *> active_audio_data_;
    juce::IIRFilter filters_[2];
    juce::SmoothedValue<float> smoothed_cutoff_;
    float last_cutoff_ = 0;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
