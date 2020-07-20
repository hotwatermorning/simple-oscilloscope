#pragma once

#include <cassert>
#include <algorithm>
#include <vector>

// リングバッファクラス
template<class T>
struct RingBuffer
{
    //! 空のリングバッファを構築する
    RingBuffer()
    {}

    //! コンストラクタ
    //! 指定したチャンネル数とサンプル数のバッファを構築する。
    /*! @pre num_channels >= 0 && num_samples >= 0
     */
    RingBuffer(std::int64_t num_channels, std::int64_t num_samples)
    {
        buffer_.resize(num_channels);
        for(auto &&ch_data: buffer_) {
            ch_data.resize(num_samples);
        }

        num_channels_ = num_channels;
        num_samples_ = num_samples;
    }

    //! samples のデータを内部バッファに書き込む。
    //! 古いデータは上書きされる。
    /*! @param src 書き込むデータ
     *  @param src_start_index samples のデータを何サンプル目から読み込むか。
     *  @param length samples から読み込むサンプル数
     *  @pre samples に対して、各チャンネルで [start_index, start_index + length) の範囲の読み込みが可能であること。
     *  @pre length <= getNumSamples()
     */
    void write(T const * const * src, std::int64_t src_start_sample, std::int64_t length)
    {
        if(length == 0 || num_samples_ == 0) { return; }
        assert(length <= num_samples_);

        // write_pos_ から書き込むサイズ
        int const num_copy1 = std::min<int>(write_pos_ + length, num_samples_) - write_pos_;

        // 先頭から書き込むサイズ
        int const num_copy2 = length - num_copy1;

        for(int ch = 0, end = num_channels_; ch < end; ++ch) {
            auto const  *ch_src    = src[ch];
            auto        *ch_dest   = buffer_[ch].data();

            std::copy_n(ch_src + src_start_sample,              num_copy1, ch_dest + write_pos_);
            std::copy_n(ch_src + src_start_sample + num_copy1,  num_copy2, ch_dest             );
        }

        write_pos_ += length;
        if(write_pos_ >= num_samples_) { write_pos_ -= num_samples_; }
        num_written_ += length;
    }

    //! 内部のバッファのデータを samples に読み込む。
    /*! @pre samples に対して、各チャンネルで [start_index, start_index + length) の範囲の書き込みが可能であること。
     *  @pre length <= getNumSamples()
     */
    void read(T **dest, std::int64_t dest_start_index, std::int64_t length) const
    {
        if(length == 0 || num_samples_ == 0) { return; }

        assert(length <= num_samples_);

        // 末尾からコピーする量
        int const num_copy1
        = write_pos_ >= length
        ? 0
        : length - write_pos_;

        // write_pos_ からさかのぼってコピーする量
        int const num_copy2 = length - num_copy1;

        for(int ch = 0, end = num_channels_; ch < end; ++ch) {
            auto const  *ch_src    = buffer_[ch].data();
            auto        *ch_dest   = dest[ch];
            std::copy_n(ch_src + (num_samples_ - num_copy1),   num_copy1, ch_dest + dest_start_index               );
            std::copy_n(ch_src + (write_pos_ - num_copy2),     num_copy2, ch_dest + dest_start_index + num_copy1   );
        }
    }

    std::int64_t getNumChannels() const noexcept { return num_channels_; }
    std::int64_t getNumSamples() const noexcept { return num_samples_; }
    int getNumWritten() const noexcept { return num_written_; }

private:
    std::int64_t num_channels_ = 0;
    std::int64_t num_samples_ = 0;
    std::int64_t write_pos_ = 0;
    std::int64_t num_written_ = 0;
    std::vector<std::vector<T>> buffer_;
};
