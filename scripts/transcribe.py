#!/usr/bin/env python3
import argparse
import json
import os
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description="Transcribe audio to JSON sidecar.")
    parser.add_argument("audio_path")
    parser.add_argument("output_json_path")
    parser.add_argument("--model", default="small")
    parser.add_argument("--device", default="auto")
    parser.add_argument("--compute_type", default="auto")
    args = parser.parse_args()

    try:
        from faster_whisper import WhisperModel
    except Exception as exc:
        sys.stderr.write(
            "faster-whisper is not installed. Run: pip install faster-whisper\n"
        )
        sys.stderr.write(str(exc) + "\n")
        return 2

    device = args.device
    if device == "auto":
        device = "cuda"
    compute_type = args.compute_type
    if compute_type == "auto":
        compute_type = "float16" if device == "cuda" else "int8"

    try:
        model = WhisperModel(args.model, device=device, compute_type=compute_type)
    except Exception:
        # Fallback to CPU if CUDA is not available.
        model = WhisperModel(args.model, device="cpu", compute_type="int8")

    segments, info = model.transcribe(
        args.audio_path,
        word_timestamps=True,
        vad_filter=True,
        beam_size=5,
    )

    all_words = []
    all_segments = []
    full_text_parts = []
    total_duration = 0.0

    for seg in segments:
        seg_text = (seg.text or "").strip()
        if seg_text:
            full_text_parts.append(seg_text)
        total_duration = max(total_duration, float(seg.end))
        all_segments.append(
            {
                "start": float(seg.start),
                "end": float(seg.end),
                "text": seg_text,
            }
        )
        for w in (seg.words or []):
            word_text = (w.word or "").strip()
            if not word_text:
                continue
            all_words.append(
                {
                    "word": word_text,
                    "start": float(w.start),
                    "end": float(w.end),
                }
            )

    output = {
        "audio_path": os.path.abspath(args.audio_path),
        "language": getattr(info, "language", "unknown"),
        "duration_sec": total_duration,
        "text": " ".join(full_text_parts).strip(),
        "segments": all_segments,
        "words": all_words,
        "model": args.model,
    }

    os.makedirs(os.path.dirname(os.path.abspath(args.output_json_path)), exist_ok=True)
    with open(args.output_json_path, "w", encoding="utf-8") as f:
        json.dump(output, f, ensure_ascii=False, indent=2)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
