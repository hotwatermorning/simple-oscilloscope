# simple-oscilloscope

A Simple Oscilloscope Application with JUCE 6.

<img width="480" alt="Screen Shot 2020-07-19 at 20 34 15" src="https://user-images.githubusercontent.com/359226/87873791-50f07200-c9ff-11ea-8ab7-4dfb2a0e72fd.png">

## How to build

```sh
git clone https://github.com/hotwatermorning/simple-oscilloscope.git
cd simple-oscilloscope
git submodule update --init --recursive
mkdir build
cd build
cmake -GXcode ..
cmake --build . --config Release
```
