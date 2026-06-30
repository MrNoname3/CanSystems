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

| # | File | Meaning |
|---|------|---------|
| 1 | `0001_beep.mp3`                    | short beep / acknowledgement |
| 2 | `0002_door_open.mp3`               | door opened |
| 3 | `0003_door_closed.mp3`            | door closed |
| 4 | `0004_water_leak_bathroom.mp3`     | water leak detected — bathroom |
| 5 | `0005_water_leak_bathroom_end.mp3` | bathroom leak cleared |
| 6 | `0006_water_leak_kitchen.mp3`      | water leak detected — kitchen |
| 7 | `0007_water_leak_kitchen_end.mp3`  | kitchen leak cleared |
| 8 | `0008_water_leak_toilet.mp3`       | water leak detected — toilet |
| 9 | `0009_water_leak_toilet_end.mp3`   | toilet leak cleared |

The meanings come from the file names; the firmware only deals in track numbers, so the set is
easy to repurpose — replace the files (keeping the numeric order) and drive them by `Sound`.
