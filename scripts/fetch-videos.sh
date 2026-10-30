#!/usr/bin/env bash
# Downloads MM-Fit (Strömbäck, Huang and Radu, IMWUT 2020; CC BY 4.0) into data/mmfit:
#   - the poses and labels of all 21 workouts (from the dataset's archive, 1.7 GB);
#   - with --videos, the RGB videos of the unseen-subject test workouts (w00 w05 w12 w13 w20,
#     about 10 GB), on which Spotter's own pose model is evaluated end to end.
#   scripts/fetch-videos.sh [--videos] [workout ids...]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/data/mmfit"
mkdir -p "$out"
videos=0
ids=()
for a in "$@"; do
  case "$a" in
    --videos) videos=1 ;;
    w[0-9][0-9]) ids+=("$a") ;;
    *) echo "unknown option $a" >&2; exit 2 ;;
  esac
done
[ ${#ids[@]} -gt 0 ] || ids=(w00 w05 w12 w13 w20)

if [ ! -d "$out/mm-fit/w20" ]; then
  sha=365bf1e546feb9350e69ba026c060fa14c1bccc989d316d062815b4981247b60
  if [ ! -f "$out/mm-fit.zip" ] || ! echo "$sha  $out/mm-fit.zip" | sha256sum -c - >/dev/null 2>&1; then
    curl -fL --retry 3 -o "$out/mm-fit.zip.part" https://s3.eu-west-2.amazonaws.com/vradu.uk/mm-fit.zip
    echo "$sha  $out/mm-fit.zip.part" | sha256sum -c - >/dev/null || { echo "checksum mismatch: mm-fit.zip" >&2; exit 1; }
    mv "$out/mm-fit.zip.part" "$out/mm-fit.zip"
  fi
  (cd "$out" && unzip -qo mm-fit.zip 'mm-fit/*/*pose_2d.npy' 'mm-fit/*/*labels.csv')
fi

declare -A md5=(
  [w00]=faa0eadb0766a982d6c58f9a900dba48 [w05]=d1dc24a6de76c7242c88f30a9974b353 [w09]=a6342991353b6dea5f2d92928dc0bbd7
  [w10]=59177e4f4d5aa23f8b97f1440a4167e4 [w11]=b72f32ae0e03c160a79cf5d03b2a379d [w12]=e36092104fc685b87e4a53f5e35f151e
  [w13]=7b4b668bfee34adb9eef6002c0ef3957 [w20]=83ca04239fb6d6d6db90092f43776bb4
)
if [ $videos = 1 ]; then
  mkdir -p "$out/videos"
  for w in "${ids[@]}"; do
    f="$out/videos/${w}_rgb.mp4"
    [ -n "${md5[$w]:-}" ] || { echo "no checksum for $w" >&2; exit 1; }
    if [ ! -f "$f" ] || ! echo "${md5[$w]}  $f" | md5sum -c - >/dev/null 2>&1; then
      echo "downloading $w (about 2 GB)"
      curl -fL --retry 3 -o "$f.part" "https://zenodo.org/api/records/7672767/files/${w}_rgb.mp4/content"
      echo "${md5[$w]}  $f.part" | md5sum -c - >/dev/null || { echo "checksum mismatch: $w" >&2; exit 1; }
      mv "$f.part" "$f"
    fi
  done
fi
echo "MM-Fit is in $out"
