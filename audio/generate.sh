#!/usr/bin/env bash
#
# Rebuild the spoken tracks in this directory from sentences.tsv - all of them,
# or only the ones named on the command line. Track 1 is a tone, not speech, and
# has no line in the file.
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
SENTENCES="$HERE/sentences.tsv"
VOICE="${VOICE:-af_heart}"
TTS="${TTS:-$(command -v tts.sh || true)}"

if [ -z "$TTS" ] || [ ! -x "$TTS" ]; then
  cat >&2 <<EOF
No tts.sh found. Point TTS at the wrapper from the ai-stack repo, or put it on
PATH:

  TTS=/path/to/ai-stack/scripts/tts.sh $0

Anything that takes '-o <file> -v <voice> <text>' will do just as well; the
service call behind it is written down in README.md.
EOF
  exit 1
fi

selected=("$@")

# A name matching no line is a typo, and rebuilding nothing would look like success.
for want in "${selected[@]}"; do
  awk -F'\t' -v name="$want" '$1 == name { found = 1 } END { exit !found }' "$SENTENCES" ||
    { printf 'No track named %s in sentences.tsv\n' "$want" >&2; exit 1; }
done

# Nothing named means every line.
wanted() {
  [ "${#selected[@]}" -eq 0 ] && return 0
  local candidate
  for candidate in "${selected[@]}"; do
    [ "$candidate" = "$1" ] && return 0
  done
  return 1
}

while IFS=$'\t' read -r name text; do
  [ -n "${name:-}" ] || continue
  wanted "$name" || continue
  "$TTS" -o "$HERE/$name.mp3" -v "$VOICE" "$text"
done < "$SENTENCES"
