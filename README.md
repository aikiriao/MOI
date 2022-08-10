# MOI

My Optimized IMA-ADPCM encoder

This encoder is strongly inspired by [adpcm-xq](https://github.com/dbry/adpcm-xq). I applied beam search and tree pruning to optimization.

# How to build

## Requirement

* [CMake](https://cmake.org) >= 3.15

## Build codec

```bash
git clone https://github.com/aikiriao/MOI.git
cd MOI/tools/moi
cmake -B build
cmake --build build
```

# Usage

## Encode/Decode

### Encode

```bash
./moi -e INPUT.wav OUTPUT.wav
```

### Decode

```bash
./moi -d INPUT.wav OUTPUT.wav
```

# License

MIT
