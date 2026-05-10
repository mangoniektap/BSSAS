import sys
import tempfile
import wave

from shared_paths import resolve_temporary_dir

INT16_MAX = 32767


def _decode_pcm_sample(sample_bytes, sample_width):
    if sample_width == 1:
        # 8-bit PCM in WAV is unsigned.
        return (sample_bytes[0] - 128) / 128.0

    if sample_width == 2:
        return int.from_bytes(sample_bytes, byteorder="little", signed=True) / 32768.0

    if sample_width == 3:
        sample_value = int.from_bytes(sample_bytes, byteorder="little", signed=False)
        if sample_value & 0x800000:
            sample_value -= 1 << 24
        return sample_value / 8388608.0

    if sample_width == 4:
        return int.from_bytes(sample_bytes, byteorder="little", signed=True) / 2147483648.0

    raise ValueError(f"Unsupported PCM sample width: {sample_width} byte(s)")


def _float_to_pcm16(sample_value):
    clamped_value = max(-1.0, min(1.0, sample_value))
    scaled_value = int(round(clamped_value * INT16_MAX))
    scaled_value = max(-32768, min(32767, scaled_value))
    return scaled_value.to_bytes(2, byteorder="little", signed=True)


def _convert_frames_to_mono_pcm16(frames, n_channels, sample_width):
    if n_channels <= 0:
        raise ValueError("Invalid channel count in WAV file")

    if sample_width not in (1, 2, 3, 4):
        raise ValueError(f"Unsupported PCM sample width: {sample_width} byte(s)")

    frame_width = n_channels * sample_width
    if frame_width <= 0 or len(frames) % frame_width != 0:
        raise ValueError("WAV frame data size is not aligned to the declared format")

    mono_pcm16 = bytearray()
    for offset in range(0, len(frames), frame_width):
        frame = frames[offset:offset + frame_width]
        first_channel_bytes = frame[:sample_width]
        normalized_sample = _decode_pcm_sample(first_channel_bytes, sample_width)
        mono_pcm16.extend(_float_to_pcm16(normalized_sample))

    return bytes(mono_pcm16)


def parse_wav_to_pcm(wav_file_path):
    try:
        with wave.open(wav_file_path, "rb") as wav_file:
            n_channels = wav_file.getnchannels()
            sample_width = wav_file.getsampwidth()
            n_frames = wav_file.getnframes()
            framerate = wav_file.getframerate()
            frames = wav_file.readframes(n_frames)

        pcm16_frames = _convert_frames_to_mono_pcm16(frames, n_channels, sample_width)

        temp_file = tempfile.NamedTemporaryFile(
            delete=False,
            suffix=".pcm",
            prefix="wav_import_",
            dir=resolve_temporary_dir(),
        )
        temp_file.write(pcm16_frames)
        temp_file.flush()
        temp_file.close()

        return temp_file.name, framerate, 2

    except Exception as error:
        print(f"Error parsing WAV: {str(error)}", file=sys.stderr)
        return None, None, None


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python WAVParse.py <input_wav_file>", file=sys.stderr)
        sys.exit(1)

    wav_file = sys.argv[1]
    pcm_path, rate, width = parse_wav_to_pcm(wav_file)
    if pcm_path:
        print(f"{pcm_path},{rate},{width}")
        sys.exit(0)

    sys.exit(1)
