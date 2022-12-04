" コーデック評価 "
import os
import subprocess
from timeit import timeit
import numpy as np
import soundfile as sf
import matplotlib.pyplot as plt
import scipy.signal as sig
import seaborn as sns

def _measure_execution_time(command):
    """ 実行時間の計測 """
    return timeit(stmt = f'subprocess.run(\'{command}\','\
        'shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)',
        setup = 'import subprocess', number = 1)

if __name__ == "__main__":
    # 一時ファイル名
    TMP_ADPCM_FILE = "compressed.wav"
    TMP_DECODE_FILE = "decoded.wav"
    BLOCK_SIZE = 256
    MAX_WIDTH = 16
    MAX_DEPTH = 6

    RATE = 44100
    DURATION_TIME = 4
    t = np.arange(0, RATE*DURATION_TIME) / RATE

    # テストデータ作成
    SIGNALS = {
            'sin100Hz.wav': np.sin(2.0 * np.pi * 100 * t),
            'square100Hz.wav': sig.square(2.0 * np.pi * 100 * t),
            'sawtooth100Hz.wav': sig.sawtooth(2.0 * np.pi * 100 * t),
            'sin1000Hz.wav': np.sin(2.0 * np.pi * 1000 * t),
            'square1000Hz.wav': sig.square(2.0 * np.pi * 1000 * t),
            'sawtooth1000Hz.wav': sig.sawtooth(2.0 * np.pi * 1000 * t),
            'sin10000Hz.wav': np.sin(2.0 * np.pi * 10000 * t),
            'square10000Hz.wav': sig.square(2.0 * np.pi * 10000 * t),
            'sawtooth10000Hz.wav': sig.sawtooth(2.0 * np.pi * 10000 * t),
            'low_freqency_chirp.wav':
                sig.chirp(t, f0=0, f1=5000, t1=DURATION_TIME, method='linear'),
            'high_freqency_chirp.wav':
                sig.chirp(t, f0=5000, f1=10000, t1=DURATION_TIME, method='linear'),
            'white_noise.wav': np.random.uniform(-1.0, 1.0, len(t)),
            }
    for name, data in SIGNALS.items():
        sf.write(name, data, RATE, format='WAV', subtype='PCM_16')
        sf.write(os.path.splitext(os.path.basename(name))[0] + '_adpcm.wav',
                data, RATE, format='WAV', subtype='IMA_ADPCM')

    WIDTH_LIST = np.arange(1, MAX_WIDTH + 1)
    DEPTH_LIST = np.arange(1, MAX_DEPTH + 1)
    mse_map = np.zeros((len(WIDTH_LIST), len(DEPTH_LIST)))
    time_map = np.zeros((len(WIDTH_LIST), len(DEPTH_LIST)))

    # 各テストデータに対して評価
    for input_file in SIGNALS:
        print(input_file)
        input_wav = sf.read(input_file)[0]
        # soundfile(libsndfile)でADPCMを作成しMSEを評価
        adpcm_wav = sf.read(os.path.splitext(os.path.basename(input_file))[0] + '_adpcm.wav')[0]
        adpcm_mse = np.mean((input_wav - adpcm_wav[:len(input_wav)]) ** 2)
        base_time =  len(input_wav) / RATE
        for x, width in enumerate(WIDTH_LIST):
            for y, depth in enumerate(DEPTH_LIST):
                print((width, depth))
                option_string = f'-W {width} -D {depth} -B {BLOCK_SIZE}'
                time = _measure_execution_time(
                        f'moi -e {option_string} {input_file} {TMP_ADPCM_FILE}')
                subprocess.run(f'moi -d {TMP_ADPCM_FILE} {TMP_DECODE_FILE}', check=True)
                output_wav = sf.read(TMP_DECODE_FILE)[0]
                mse = np.mean((input_wav - output_wav) ** 2)
                mse_map[x, y] = mse / adpcm_mse * 100
                time_map[x, y] = time / base_time * 100
        sns.heatmap(time_map, xticklabels=DEPTH_LIST, yticklabels=WIDTH_LIST,
            cmap='coolwarm_r', annot=True, fmt='01.1f', vmin=None, vmax=100)
        plt.xlabel('Search depth')
        plt.ylabel('Search width')
        plt.title(f'Encoding time ratio (%) for {input_file}')
        plt.savefig(f'{os.path.splitext(os.path.basename(input_file))[0]}_time_compare.png')
        plt.clf()
        sns.heatmap(mse_map, xticklabels=DEPTH_LIST, yticklabels=WIDTH_LIST,
                cmap='coolwarm_r', annot=True, fmt='01.1f', vmin=None, vmax=100)
        plt.xlabel('Search depth')
        plt.ylabel('Search width')
        plt.title(f'MSE ratio vs libsndfile (%) for {input_file}')
        plt.savefig(f'{os.path.splitext(os.path.basename(input_file))[0]}_mse_compare.png')
        plt.close()
