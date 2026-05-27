import os
import struct
import sys
import wave
from datetime import datetime

MAX_VOLTAGE = 5.0
MIN_VOLTAGE = -5.0
INT16_MAX_VAL = 32767.0


def float32_file_to_pcm16(float_file_path, channel_count=1):
    with open(float_file_path, "rb") as float_file:
        float_data = float_file.read()

    if len(float_data) % 4 != 0:
        raise ValueError("Float cache file size is not aligned to 32-bit samples")

    if len(float_data) < 4:
        raise ValueError("Float cache file does not contain a sample-rate header")

    float_data = float_data[4:]
    sample_count = len(float_data) // 4
    if channel_count <= 0:
        raise ValueError("Channel count must be greater than zero")
    if sample_count % channel_count != 0:
        raise ValueError("Float cache sample count is not aligned to channel count")

    samples = [voltage for (voltage,) in struct.iter_unpack("<f", float_data)]
    peak_voltage = max((abs(voltage) for voltage in samples), default=0.0)
    scale = MAX_VOLTAGE / peak_voltage if peak_voltage > MAX_VOLTAGE else 1.0

    pcm_data = bytearray()
    for voltage in samples:
        scaled_voltage = voltage * scale
        clamped_voltage = max(MIN_VOLTAGE, min(MAX_VOLTAGE, scaled_voltage))
        scaled_value = int(round((clamped_voltage / MAX_VOLTAGE) * INT16_MAX_VAL))
        scaled_value = max(-32768, min(32767, scaled_value))
        pcm_data.extend(struct.pack("<h", scaled_value))

    return bytes(pcm_data)


def safe_output_stem(output_name):
    output_name = (output_name or "").strip()
    if not output_name:
        return datetime.now().strftime("%Y-%m-%d_%H-%M-%S")

    output_name = os.path.splitext(os.path.basename(output_name))[0].strip()
    return output_name or datetime.now().strftime("%Y-%m-%d_%H-%M-%S")


def write_wav(float_file_path, output_wav_path, sample_rate, channel_count=1, output_name=""):
    try:
        sample_rate = int(sample_rate)
        channel_count = int(channel_count)

        if not os.path.exists(output_wav_path):
            os.makedirs(output_wav_path)

        output_stem = safe_output_stem(output_name)
        output_wav_path = os.path.join(output_wav_path, f"{output_stem}.wav")

        pcm_data = float32_file_to_pcm16(float_file_path, channel_count)

        with wave.open(output_wav_path, "wb") as wav_file:
            wav_file.setnchannels(channel_count)
            wav_file.setsampwidth(2)
            wav_file.setframerate(sample_rate)
            wav_file.writeframes(pcm_data)

        print(f"Success: Saved to {output_wav_path}")
        return 0

    except Exception as error:
        print(f"Error: {str(error)}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5, 6):
        print(
            "Usage: python WAVExport.py <float_tmp> <out_wav_dir> <rate> [channels] [output_name]",
            file=sys.stderr,
        )
        sys.exit(1)

    raw_path = sys.argv[1]
    out_path = sys.argv[2]
    rate = sys.argv[3]
    channels = sys.argv[4] if len(sys.argv) >= 5 else 1
    output_name = sys.argv[5] if len(sys.argv) == 6 else ""

    sys.exit(write_wav(raw_path, out_path, rate, channels, output_name))
