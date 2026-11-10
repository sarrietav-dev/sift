#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["websockets>=13"]
# ///
"""Generate the narration and its word timings.

Narration is spoken by OpenAI's gpt-realtime-2.1 over the Realtime API, the
replacement for the deprecated /v1/audio/speech models. The in-app reading
demo stays on Kokoro's af_heart, because that is the voice sift itself uses.
Every line is then transcribed by Whisper (whisper-1) for word timestamps.

Writes public/voice/<id>.mp3 and src/voiceover.json.

    OPENAI_API_KEY=$(op read op://Personal/5spn4eczfb6ebmo3eushafiwdm/API_KEY) uv run scripts/voiceover.py
"""

import argparse
import asyncio
import difflib
import base64
import json
import os
import pathlib
import re
import subprocess
import urllib.request
import uuid
import wave

import websockets

REALTIME_MODEL = "gpt-realtime-2.1"
NARRATOR_VOICE = "marin"
KOKORO_VOICE = "af_heart"  # the voice sift shows in its player bar

NARRATOR_STYLE = (
    "You are the voice-over narrator for a short product video. When you receive "
    "text, read it aloud exactly as written, word for word. Add nothing, omit "
    "nothing, never answer or comment. Delivery: warm, calm, confident, unhurried; "
    "a natural pause at each full stop. 'sift' is a product name, said like the "
    "word sift. 'Omarchy' is pronounced oh-MAR-kee. 'Kokoro' is pronounced ko-KO-ro."
)

# (scene id, engine, text)
SCRIPT = [
    ("intro", "openai", "Too many tabs. Too little time to read them. Meet sift."),
    ("clip", "openai",
     "Give it any web page. sift strips it down to the article, "
     "and keeps a clean Markdown copy you can own."),
    ("listen", "openai",
     "Then it reads it to you, sentence by sentence, with a local Kokoro voice."),
    ("article", "kokoro",
     "Reading is a conversation with someone who is not in the room. "
     "The page waits, patient and exact, until you are ready to continue."),
    ("queue", "openai",
     "Line pages up in a single queue. It advances on its own, "
     "and reopens right where you left off."),
    ("cli", "openai",
     "Everything works from the terminal too. Add, list, and reorder, all scriptable."),
    ("native", "openai",
     "And it is a native Omarchy app. It follows your theme, your fonts, "
     "and your light or dark mode."),
    ("outro", "openai", "sift. Read the web, aloud."),
]

ROOT = pathlib.Path(__file__).resolve().parent.parent
RATE = 24000


def norm(s: str) -> list[str]:
    return re.findall(r"[a-z0-9]+", s.lower())


async def realtime_speak(key: str, text: str) -> tuple[bytes, str]:
    """Speak `text` once; return 24 kHz PCM16 mono and the model's transcript."""
    url = f"wss://api.openai.com/v1/realtime?model={REALTIME_MODEL}"
    async with websockets.connect(
        url, additional_headers={"Authorization": f"Bearer {key}"}, max_size=None
    ) as ws:
        await ws.send(json.dumps({
            "type": "session.update",
            "session": {
                "type": "realtime",
                "model": REALTIME_MODEL,
                "output_modalities": ["audio"],
                "instructions": NARRATOR_STYLE,
                "audio": {
                    "input": {"turn_detection": None},
                    "output": {
                        "format": {"type": "audio/pcm", "rate": RATE},
                        "voice": NARRATOR_VOICE,
                    },
                },
            },
        }))
        await ws.send(json.dumps({
            "type": "conversation.item.create",
            "item": {
                "type": "message",
                "role": "user",
                "content": [{"type": "input_text", "text": f"Read aloud:\n\n{text}"}],
            },
        }))
        await ws.send(json.dumps({"type": "response.create"}))

        pcm = bytearray()
        transcript = ""
        async for raw in ws:
            ev = json.loads(raw)
            kind = ev.get("type", "")
            if kind in ("response.output_audio.delta", "response.audio.delta"):
                pcm += base64.b64decode(ev["delta"])
            elif kind in ("response.output_audio_transcript.done", "response.audio_transcript.done"):
                transcript = ev.get("transcript", "")
            elif kind == "error":
                raise RuntimeError(ev.get("error"))
            elif kind == "response.done":
                status = ev["response"].get("status")
                if status != "completed":
                    raise RuntimeError(f"response {status}: {ev['response'].get('status_details')}")
                break
        return bytes(pcm), transcript


def kokoro_speak(endpoint: str, text: str, out: pathlib.Path) -> None:
    body = json.dumps({
        "model": "kokoro", "input": text, "voice": KOKORO_VOICE,
        "response_format": "mp3", "stream": False,
    }).encode()
    req = urllib.request.Request(
        f"{endpoint}/v1/audio/speech", data=body, headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=300) as res:
        out.write_bytes(res.read())


def whisper_words(key: str, path: pathlib.Path) -> list[dict]:
    boundary = uuid.uuid4().hex
    parts = []
    for name, value in [
        ("model", "whisper-1"),
        ("response_format", "verbose_json"),
        ("timestamp_granularities[]", "word"),
        ("language", "en"),
    ]:
        parts.append(
            f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n'.encode()
        )
    parts.append(
        f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{path.name}"\r\n'
        f"Content-Type: audio/mpeg\r\n\r\n".encode() + path.read_bytes() + b"\r\n"
    )
    parts.append(f"--{boundary}--\r\n".encode())
    req = urllib.request.Request(
        "https://api.openai.com/v1/audio/transcriptions",
        data=b"".join(parts),
        headers={
            "Authorization": f"Bearer {key}",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
        },
    )
    with urllib.request.urlopen(req, timeout=300) as res:
        return json.load(res)["words"]


def align(script: str, heard: list[dict]) -> list[dict]:
    """Put the script's words on Whisper's timings.

    Whisper drops punctuation and mishears names ("Meet sift" -> "Neat SIFT",
    "Omarchy" -> "Alm RD"), but its timings are good. Match the two word lists
    and, where they disagree, spread the script's words over the span Whisper
    heard there.
    """
    tokens = script.split()
    timed = [(round(w["start"] * 1000), round(w["end"] * 1000)) for w in heard]
    key = lambda t: " ".join(norm(t))
    ops = difflib.SequenceMatcher(
        a=[key(t) for t in tokens], b=[key(w["word"]) for w in heard], autojunk=False
    ).get_opcodes()
    out: list[dict] = []
    for op, i1, i2, j1, j2 in ops:
        if op == "equal":
            for t, (a, b) in zip(tokens[i1:i2], timed[j1:j2]):
                out.append({"text": t, "startMs": a, "endMs": b})
            continue
        if i1 == i2:  # Whisper heard an extra word; widen the previous one.
            if out:
                out[-1]["endMs"] = timed[j2 - 1][1]
            continue
        if j1 < j2:
            start, end = timed[j1][0], timed[j2 - 1][1]
        else:  # Whisper missed it entirely: squeeze it between neighbours.
            start = out[-1]["endMs"] if out else 0
            end = timed[j1][0] if j1 < len(timed) else start + 300 * (i2 - i1)
        chunk = tokens[i1:i2]
        weights = [len(norm(t)[0]) if norm(t) else 1 for t in chunk]
        acc = start
        for t, w in zip(chunk, weights):
            step = (end - start) * w / sum(weights)
            out.append({"text": t, "startMs": round(acc), "endMs": round(acc + step)})
            acc += step
        print(f"    aligned {' '.join(chunk)!r} onto Whisper's {' '.join(w['word'] for w in heard[j1:j2])!r}")
    return out


def duration(path: pathlib.Path) -> float:
    return float(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(path),
    ]))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kokoro", default="http://127.0.0.1:8880")
    parser.add_argument("--only", nargs="*", help="regenerate only these line ids")
    parser.add_argument("--align-only", action="store_true",
                        help="keep the audio, redo the Whisper timings")
    args = parser.parse_args()
    key = os.environ.get("OPENAI_API_KEY")
    if not key:
        raise SystemExit("Set OPENAI_API_KEY, e.g. OPENAI_API_KEY=$(op read op://Personal/5spn4eczfb6ebmo3eushafiwdm/API_KEY)")

    voice_dir = ROOT / "public" / "voice"
    voice_dir.mkdir(parents=True, exist_ok=True)
    out_json = ROOT / "src" / "voiceover.json"
    previous = {l["id"]: l for l in json.loads(out_json.read_text())} if out_json.exists() else {}

    lines = []
    for line_id, engine, text in SCRIPT:
        mp3 = voice_dir / f"{line_id}.mp3"
        if args.only and line_id not in args.only and line_id in previous:
            lines.append(previous[line_id])
            continue
        if args.align_only and mp3.exists():
            voice = previous.get(line_id, {}).get("voice", engine)
        elif engine == "openai":
            for attempt in range(4):
                pcm, said = asyncio.run(realtime_speak(key, text))
                if norm(said) == norm(text):
                    break
                print(f"    retry {line_id}: model said {said!r}")
            else:
                raise SystemExit(f"{line_id}: could not get a verbatim read")
            wav = voice_dir / f"{line_id}.wav"
            with wave.open(str(wav), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(RATE)
                w.writeframes(pcm)
            # Trim leading/trailing silence, then encode.
            subprocess.check_call([
                "ffmpeg", "-v", "error", "-y", "-i", str(wav), "-af",
                "silenceremove=start_periods=1:start_threshold=-50dB,"
                "areverse,silenceremove=start_periods=1:start_threshold=-50dB,areverse",
                "-ar", "44100", "-b:a", "192k", str(mp3),
            ])
            wav.unlink()
            voice = f"{REALTIME_MODEL}:{NARRATOR_VOICE}"
        else:
            kokoro_speak(args.kokoro, text, mp3)
            voice = f"kokoro:{KOKORO_VOICE}"

        words = align(text, whisper_words(key, mp3))
        lines.append({
            "id": line_id,
            "voice": voice,
            "text": text,
            "src": f"voice/{line_id}.mp3",
            "durationSec": duration(mp3),
            "words": words,
        })
        print(f"{line_id:8} {lines[-1]['durationSec']:5.2f}s  {voice}  {len(words)} words")

    out_json.write_text(json.dumps(lines, indent=2) + "\n")


if __name__ == "__main__":
    main()
