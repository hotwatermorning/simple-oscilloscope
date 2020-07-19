# Oscilloscope Sample

A Simple Oscilloscope Application with JUCE 6.

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
