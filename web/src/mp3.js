// Kokoro clips are complete MP3 files with an ID3v2 tag in front. Streaming
// the sentences of an article one after another means joining those files, so
// drop the tag from each clip (a decoder would otherwise find metadata in the
// middle of the stream) and count frames to know how long each sentence lasts.
// The frame duration is what lets the player map audio time back to a sentence.

// Bitrate tables by MPEG version, then by the four-bit index in the frame
// header. Layer III only; Kokoro never emits anything else.
const BITRATES = {
    mpeg1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],
    mpeg2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160],
};
const SAMPLE_RATES = {
    mpeg1: [44100, 48000, 32000],
    mpeg2: [22050, 24000, 16000],
    mpeg25: [11025, 12000, 8000],
};

function syncsafe(bytes, at) {
    return ((bytes[at] & 0x7f) << 21) | ((bytes[at + 1] & 0x7f) << 14) | ((bytes[at + 2] & 0x7f) << 7) | (bytes[at + 3] & 0x7f);
}

function clipDuration(audio) {
    let offset = 0;
    let samples = 0;
    let rate = 0;
    while (offset + 4 <= audio.length) {
        // Resynchronise after anything that is not a frame header.
        if (audio[offset] !== 0xff || (audio[offset + 1] & 0xe0) !== 0xe0) {
            offset++;
            continue;
        }
        const version = audio[offset + 1] >> 3 & 0x03;
        const layer = audio[offset + 1] >> 1 & 0x03;
        const bitrateIndex = audio[offset + 2] >> 4 & 0x0f;
        const rateIndex = audio[offset + 2] >> 2 & 0x03;
        const padding = audio[offset + 2] >> 1 & 0x01;
        const mpeg1 = version === 3;
        // Version 1 is reserved; Layer III is 01; 0 is "free" and 15 is bad.
        if (version === 1 || layer !== 1 || !bitrateIndex || bitrateIndex === 15 || rateIndex === 3) {
            offset++;
            continue;
        }
        const bitrate = (mpeg1 ? BITRATES.mpeg1 : BITRATES.mpeg2)[bitrateIndex] * 1000;
        const sampleRate = (mpeg1 ? SAMPLE_RATES.mpeg1 : version === 2 ? SAMPLE_RATES.mpeg2 : SAMPLE_RATES.mpeg25)[rateIndex];
        const length = Math.floor((mpeg1 ? 144 : 72) * bitrate / sampleRate) + padding;
        if (length < 4) {
            offset++;
            continue;
        }
        samples += mpeg1 ? 1152 : 576;
        rate = sampleRate;
        offset += length;
    }
    return rate ? samples / rate : null;
}

export function splitMp3(bytes) {
    let start = 0;
    let end = bytes.length;
    if (end >= 10 && bytes[0] === 0x49 && bytes[1] === 0x44 && bytes[2] === 0x33) {
        // ID3v2 header (10 bytes) + syncsafe size, plus an optional footer.
        start = 10 + syncsafe(bytes, 6) + (bytes[5] & 0x10 ? 10 : 0);
    }
    if (end - start >= 128 && bytes[end - 128] === 0x54 && bytes[end - 127] === 0x41 && bytes[end - 126] === 0x47) {
        // ID3v1 is a fixed 128-byte trailer.
        end -= 128;
    }
    const audio = bytes.subarray(start, Math.max(start, end));
    return { audio, duration: clipDuration(audio) };
}
