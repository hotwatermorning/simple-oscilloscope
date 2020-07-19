#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
{
    active_audio_data_.store(nullptr);

    addParameter(cutoff_ = new juce::AudioParameterFloat("cutoff",
                                                         "Cut Off",
                                                         juce::NormalisableRange<float> { 0.0, 1.0 },
                                                         0.0,
                                                         " Hz",
                                                         juce::AudioProcessorParameter::genericParameter,
                                                         [this](float value, int len) { return floatToString(value, len); },
                                                         [this](juce::String const &str) { return stringToFloat(str); }
                                                         ));
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int AudioPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    juce::ignoreUnused (sampleRate);

    std::unique_lock<AudioData> lock;
    if(auto p = active_audio_data_.load()) {
        lock = std::unique_lock<AudioData>(*p);
    }

    auto buffer = RingBuffer<float>(2, (int)std::round(sampleRate));

    datas_[0].getPreBuffer() = buffer;
    datas_[0].getPostBuffer() = buffer;
    datas_[1].getPreBuffer() = buffer;
    datas_[1].getPostBuffer() = buffer;

    if(lock.owns_lock() == false) {
        active_audio_data_.store(&datas_[0]);
    }

    tmp_buf_ = juce::AudioSampleBuffer(2, samplesPerBlock);
    tmp_buf_.clear();
    smoothed_cutoff_.reset(5);
    smoothed_cutoff_.setTargetValue(cutoff_->get());
    smoothed_cutoff_.skip(5);
    last_cutoff_ = smoothed_cutoff_.getNextValue();
}

void AudioPluginAudioProcessor::releaseResources()
{
    active_audio_data_.store(nullptr);
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    // buffer に読み書きするサンプル長
    auto const length = std::min<int>(buffer.getNumSamples(), tmp_buf_.getNumSamples());

    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    assert(totalNumInputChannels == 2);
    assert(totalNumOutputChannels == 2);
    assert(buffer.getNumChannels() >= totalNumInputChannels);

    for(int ch = 0; ch < totalNumInputChannels; ++ch) {
        auto range = buffer.findMinMax(ch, 0, length);
        //assert(range.getStart() >= -3.0 && range.getEnd() <= 3.0);
    }

    // エフェクト処理前のデータを退避
    for(int ch = 0; ch < totalNumInputChannels; ++ch) {
        tmp_buf_.copyFrom(ch, 0, buffer, ch, 0, length);
    }

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch) {
        buffer.clear (ch, 0, buffer.getNumSamples());
    }

    smoothed_cutoff_.setTargetValue(cutoff_->get());
    auto const new_cutoff = smoothed_cutoff_.getNextValue();

    // 前回処理時から cutoff パラメータの値が変わっていたら、新しいカットオフ周波数で各チャンネルのフィルタ設定を更新する
    if(new_cutoff != last_cutoff_) {
        last_cutoff_ = new_cutoff;
        double sample_rate = getSampleRate();

        // カットオフ周波数をナイキスト周波数限界まで設定すると発振してしまうので、それ以下に制限する。
        float freq = std::min<float>(paramToHz(new_cutoff), sample_rate / 2.0 - 1);
        filters_[0].setCoefficients(juce::IIRCoefficients::makeLowPass(sample_rate, freq));
        filters_[1].setCoefficients(juce::IIRCoefficients::makeLowPass(sample_rate, freq));
    }

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        filters_[ch].processSamples(buffer.getWritePointer(ch), length);
    }

    for(int ch = 0; ch < totalNumInputChannels; ++ch) {
        auto ch_data = buffer.getWritePointer(ch);
        for(int smp = 0; smp < length; ++smp) {
            ch_data[smp] = juce::jlimit<double>(-1.0, 1.0, ch_data[smp]);
        }
    }

    // AudioData に書き込みたい、エフェクト処理前のデータ（事前にprocessBlock の先頭で退避しておいたもの）
    float const * const * pre_data = tmp_buf_.getArrayOfReadPointers();
    // AudioData に書き込みたい、エフェクト処理後のデータ
    float const * const * post_data = buffer.getArrayOfReadPointers();

    auto ad = active_audio_data_.load();
    std::unique_lock<AudioData> lock(*ad, std::try_to_lock);
    if(lock) {
       // ロックに成功した場合は、 GUI がこの AudioData を使用していない状態なので、
       // そのままデータを書き込む。
       ad->getPreBuffer().write(pre_data, 0, length);
       ad->getPostBuffer().write(post_data, 0, length);
    } else {
        // ロックに失敗した場合は、 GUI が使用中ということ。
        // その場合はアクティブではない方の AudioData にデータを書き込み、それが完了した段階で
        // active_audio_data_ のポインタを入れ替える。
        // 現在アクティブでない方の AudioData を取得
        auto *opposite_ad = (&datas_[0] == ad) ? &datas_[1] : &datas_[0];
        // アクティブな AudioData に書き込まれたデータを opposite_ad にコピーする。
        //
        // opposite_ad は前回書き込み処理を行った AudioData ではないので、ここで新たに pre_data, post_data を書き込むと、
        // バッファの内容が不連続になる。そのため、ここでアクティブな AudioData の中身をコピーし、
        // 新しくアクティブになる AudioData にリングバッファの内容が引き継がれるようにする。
        //
        // ここではリングバッファのすべてのデータを複製しているが、今回の length 個分の領域が上書きされることを考慮して、
        // 必要の最低限のデータだけをコピーするほうが、コピー処理の負荷が小さくて済むので良い。
        //
        // opposite_ad はこの瞬間 GUI スレッドで使用されていないはずなので、書き込み処理を行ってもデータ競合は発生しない。
        // ad はこの瞬間 GUI スレッドでも使用されているかもしれないが、どちらも読み込み処理なのでデータ競合は発生しない。
        opposite_ad->getPreBuffer() = ad->getPreBuffer();
        opposite_ad->getPostBuffer() = ad->getPostBuffer();
        opposite_ad->getPreBuffer().write(pre_data, 0, length);
        opposite_ad->getPostBuffer().write(post_data, 0, length);
        // アクティブな AudioData のポインタを置き換える
        auto prev = active_audio_data_.exchange(opposite_ad);
        assert(prev == ad && prev != opposite_ad);
        // 以降は opposite_ad が active_audio_data_ として使用される。
    }
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::ignoreUnused (destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}

auto log_of = [](double base, double value) {
    return log(value) / log(base);
};

constexpr double kCutoffMin = 20;
constexpr double kCutoffMaxLimit = 0.475;
constexpr double kCutoffBase = 1.2;


float AudioPluginAudioProcessor::HzToParam(float Hz) const
{
    double const sample_rate = getSampleRate();
    double const kCutoffMax = std::round(sample_rate * kCutoffMaxLimit);
    double const kLogMax = log_of(kCutoffBase, kCutoffMax - kCutoffMin + 1);
    Hz = juce::jlimit<float>(kCutoffMin, kCutoffMax, Hz);

    auto x = Hz - (kCutoffMin - 1);
    x = log_of(kCutoffBase, x);
    x = x / kLogMax;

    assert(0.0 <= x && x <= 1.0);
    return x;
}

float AudioPluginAudioProcessor::paramToHz(float value) const
{
    auto const sample_rate = getSampleRate();
    double const kCutoffMax = std::round(sample_rate * kCutoffMaxLimit);
    double const kLogMax = log_of(kCutoffBase, kCutoffMax - kCutoffMin + 1);

    double Hz = pow(kCutoffBase, value * kLogMax) + (kCutoffMin - 1);

    Hz = (std::int64_t)std::round(Hz * 100) / 100.0;
    assert(kCutoffMin <= Hz && Hz <= kCutoffMax);
    
    return Hz;
}

juce::String AudioPluginAudioProcessor::floatToString(float value, int maximumStringLength) const
{
    juce::String tmp = juce::String::formatted("%0.2f", paramToHz(value));
    return tmp;
}

float AudioPluginAudioProcessor::stringToFloat(juce::String const &str) const
{
    try {
        double x = std::stod(str.toRawUTF8());
        return HzToParam(x);
    } catch(std::exception &e) {
        return cutoff_->get();
    }
}
