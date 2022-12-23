# Evaluation results

The following equations calculate the MSE ratio (%) and encoding time ratio (%):

```math
    \text{\textrm{MSE} ratio (\%)} = \frac{\text{\textrm{MSE} by MOI encoder}}{\text{\textrm{MSE} by libsoundfile encoder}} \times 100
```

```math
    \text{Encoding time ratio (\%)} = \frac{\text{encode processing time (sec)}}{\text{wave file length (sec)}} \times 100
```

The above criteria are small is better. e.g., MSE ratio = 100 (%) means MOI encoder MSE is equal to the libsoundfile one (not good).

## MSE (Mean squared error)

### sin wave

![sin100Hz_MSE](sin100Hz_mse_compare.png)

![sin1000Hz_MSE](sin1000Hz_mse_compare.png)

![sin10000Hz_MSE](sin10000Hz_mse_compare.png)

### rectangle wave

![square100Hz_MSE](square100Hz_mse_compare.png)

![square1000Hz_MSE](square1000Hz_mse_compare.png)

![square10000Hz_MSE](square10000Hz_mse_compare.png)

### sawtooth wave

![sawtooth100Hz_MSE](sawtooth100Hz_mse_compare.png)

![sawtooth1000Hz_MSE](sawtooth1000Hz_mse_compare.png)

![sawtooth10000Hz_MSE](sawtooth10000Hz_mse_compare.png)

### chirp (0 - 5000Hz, 5000 - 10000Hz)

![low_freqency_chirp_MSE](low_freqency_chirp_mse_compare.png)

![high_freqency_chirp_MSE](high_freqency_chirp_mse_compare.png)

### white noise

![white_noise_MSE](white_noise_mse_compare.png)

## Encoding time

### sin wave

![sin100Hz_time](sin100Hz_time_compare.png)

![sin1000Hz_time](sin1000Hz_time_compare.png)

![sin10000Hz_time](sin10000Hz_time_compare.png)

### rectangle wave

![square100Hz_time](square100Hz_time_compare.png)

![square1000Hz_time](square1000Hz_time_compare.png)

![square10000Hz_time](square10000Hz_time_compare.png)

### sawtooth wave

![sawtooth100Hz_time](sawtooth100Hz_time_compare.png)

![sawtooth1000Hz_time](sawtooth1000Hz_time_compare.png)

![sawtooth10000Hz_time](sawtooth10000Hz_time_compare.png)

### chirp (0 - 5000Hz, 5000 - 10000Hz)

![low_freqency_chirp_time](low_freqency_chirp_time_compare.png)

![high_freqency_chirp_time](high_freqency_chirp_time_compare.png)

### white noise

![white_noise_time](white_noise_time_compare.png)
