# Alert node audio

Sound set for the **alert node** (`nanoatmega328_alert`), played by its DFPlayer Mini MP3
module from a microSD card.

## How a track is selected

A track is chosen by its **number**, end to end: an MQTT message carries a `Sound` field, the
ESP32 gateway forwards it to the alert node as a `PLAY_MP3` CAN command, and the node calls
`mp3Player.play(<number>)`. The DFPlayer addresses tracks by their **index in the card's FAT
table** — i.e. the order the files were copied — so the leading 4-digit prefix only matches the
played track if the files are written in order. Copy them sorted onto a freshly formatted
**FAT32** card (root directory), and track `N` is the file beginning with that number.

## Tracks

| # | File | Spoken |
|---|------|--------|
| 1 | `0001_beep.mp3`                    | *(a tone, not speech — acknowledgement)* |
| 2 | `0002_door_open.mp3`               | The door is open. |
| 3 | `0003_door_closed.mp3`             | The door is closed. |
| 4 | `0004_water_leak_bathroom.mp3`     | Warning. Water leak detected in the bathroom. |
| 5 | `0005_water_leak_bathroom_end.mp3` | Bathroom leak cleared. |
| 6 | `0006_water_leak_kitchen.mp3`      | Warning. Water leak detected in the kitchen. |
| 7 | `0007_water_leak_kitchen_end.mp3`  | Kitchen leak cleared. |
| 8 | `0008_water_leak_toilet.mp3`       | Warning. Water leak detected in the toilet. |
| 9 | `0009_water_leak_toilet_end.mp3`   | Toilet leak cleared. |
| 10 | `0010_water_leak_fridge.mp3`      | Warning. Water leak detected at the fridge. |
| 11 | `0011_water_leak_fridge_end.mp3`  | Fridge leak cleared. |

The firmware only deals in track numbers, so the set is easy to repurpose — replace the files
(keeping the numeric order) and drive them by `Sound`.

## What a track has to be

Mono **MP3** in the card's root directory. The speech here is 24 kHz / 64 kbps and the tone
32 kHz / 192 kbps; both play on the module, so a new track built like either needs no thought.

Worth knowing before changing any synthesis setting: the node never reads the module's serial
replies, although the protocol has queries for exactly this. Its one piece of feedback is the
BUSY line, watched for the rising edge that means playback ended, and it stops waiting for that
edge after `playTimeoutTime`. A file the DFPlayer will not play therefore costs that whole wait
and is otherwise indistinguishable from one that played — nothing goes back over CAN either way.
Try a new track on the hardware before relying on it.

## Regenerating the speech

`sentences.tsv` is the source for everything spoken: one line per track, the file's base name
and the sentence separated by a tab. `generate.sh` walks that file and writes each `.mp3` beside
it. Track 1 is a tone rather than speech, which is why it has no line there.

No speech model lives in this repo — `generate.sh` shells out to one. `TTS` names the command;
the current set was made with the `tts.sh` wrapper from the **ai-stack** repo:

```bash
export TTS=/path/to/ai-stack/scripts/tts.sh

./generate.sh                         # every track in sentences.tsv
./generate.sh 0010_water_leak_fridge  # only the tracks named
VOICE=bm_george ./generate.sh         # the whole set in a different voice
```

Any command taking the same three arguments can stand in for that wrapper:

```text
<command> -o <output file> -v <voice> <text...>
```

`tts.sh` is itself a thin client for an OpenAI-compatible `POST /v1/audio/speech`, posting
`{"model": "kokoros", "voice": …, "input": …, "response_format": "mp3"}` and saving the response
body. That is where voice names like `af_heart` and `bm_george` come from — they are the model's,
not the wrapper's. Against any service exposing that endpoint, the whole thing is a dozen lines
of `curl`.

The output depends on the model and the server version as well as on the text and the voice, so
a re-run after the speech stack has moved on rewrites every file it touches. Name the tracks you
mean to change rather than rebuilding the set out of habit.

Add a track by appending a line to `sentences.tsv` and a row to the table above.
