#!/usr/bin/env bash
# Downloads what Spotter uses but does not keep in git, checks each file's SHA-256 and unpacks it
# into third_party/:
#   - ONNX Runtime 1.24.4: on Windows the DirectML build (any DirectX 12 GPU) with DirectML 1.15.4,
#     on Linux the CPU build;
#   - OpenCV 4.12.0 (Windows; on Linux install libopencv-dev);
#   - the pose models: YOLOX-tiny (person detector) and RTMPose-m (17 COCO keypoints), from
#     OpenMMLab's ONNX exports (Apache-2.0).
#   scripts/fetch-deps.sh [--llm]
#     --llm   also Qwen2.5-7B-Instruct Q4_K_M and llama.cpp's server (b11429), for the planner
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
tp="$root/third_party"
dl="$tp/downloads"
mkdir -p "$dl" "$tp/models/pose"
llm=0
for a in "$@"; do
  case "$a" in
    --llm) llm=1 ;;
    *) echo "unknown option $a" >&2; exit 2 ;;
  esac
done

fetch() { # url sha256 file
  local url="$1" sha="$2" file="$dl/$3"
  if [ ! -f "$file" ] || ! echo "$sha  $file" | sha256sum -c - >/dev/null 2>&1; then
    echo "downloading $url"
    curl -fsSL --retry 3 -o "$file.part" "$url" || { echo "download failed: $url" >&2; exit 1; }
    echo "$sha  $file.part" | sha256sum -c - >/dev/null || { echo "checksum mismatch: $url" >&2; rm -f "$file.part"; exit 1; }
    mv "$file.part" "$file"
  fi
}

mm=https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/onnx_sdk
fetch $mm/yolox_tiny_8xb8-300e_humanart-6f3252f9.zip 36e09ca555916253fa1b5d51bec01e48e98e1f0af966c51ce3e844d8c8fc4cfc yolox-tiny.zip
fetch $mm/rtmpose-m_simcc-body7_pt-body7_420e-256x192-e48f03d0_20230504.zip f7fbb6c5c11a1bb70f3d445e4ddec5d144ea89ad5649c081ef976f9b24a0b741 rtmpose-m.zip
for m in yolox-tiny rtmpose-m; do
  if [ ! -f "$tp/models/pose/$m.onnx" ]; then
    unzip -qo "$dl/$m.zip" '*end2end.onnx' -d "$dl/$m"
    find "$dl/$m" -name end2end.onnx -exec mv {} "$tp/models/pose/$m.onnx" \;
    rm -rf "$dl/$m"
  fi
done

ort="$tp/onnxruntime"
case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*)
    fetch https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/1.24.4 \
      57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681 onnxruntime-directml-1.24.4.nupkg
    fetch https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.15.4 \
      4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9 directml-1.15.4.nupkg
    if [ ! -f "$ort/lib/onnxruntime.dll" ]; then
      rm -rf "$dl/x" && mkdir -p "$ort/include" "$ort/lib"
      unzip -qo "$dl/onnxruntime-directml-1.24.4.nupkg" 'build/native/include/*' 'runtimes/win-x64/native/*' -d "$dl/x"
      unzip -qo "$dl/directml-1.15.4.nupkg" 'bin/x64-win/DirectML.dll' -d "$dl/x"
      mv "$dl"/x/build/native/include/* "$ort/include/"
      mv "$dl"/x/runtimes/win-x64/native/* "$dl/x/bin/x64-win/DirectML.dll" "$ort/lib/"
      rm -rf "$dl/x"
    fi
    fetch https://github.com/opencv/opencv/releases/download/4.12.0/opencv-4.12.0-windows.exe \
      b753b14d880b9bc8d89d6acd3b665c040baec0211078435432fcae117db707af opencv-4.12.0-windows.exe
    # A self-extracting 7-Zip archive: it unpacks to third_party/opencv/build.
    [ -d "$tp/opencv/build" ] || "$dl/opencv-4.12.0-windows.exe" -o"$(cygpath -w "$tp")" -y >/dev/null ;;
  Linux)
    fetch https://github.com/microsoft/onnxruntime/releases/download/v1.24.4/onnxruntime-linux-x64-1.24.4.tgz \
      3a211fbea252c1e66290658f1b735b772056149f28321e71c308942cdb54b747 onnxruntime-linux-x64-1.24.4.tgz
    if [ ! -d "$ort/lib" ]; then
      mkdir -p "$ort"
      tar -xzf "$dl/onnxruntime-linux-x64-1.24.4.tgz" -C "$ort" --strip-components=1
    fi ;;
esac

if [ $llm = 1 ]; then
  fetch https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/main/Qwen2.5-7B-Instruct-Q4_K_M.gguf \
    65b8fcd92af6b4fefa935c625d1ac27ea29dcb6ee14589c55a8f115ceaaa1423 Qwen2.5-7B-Instruct-Q4_K_M.gguf
  mv -f "$dl/Qwen2.5-7B-Instruct-Q4_K_M.gguf" "$tp/models/"
  llama="https://github.com/ggml-org/llama.cpp/releases/download/b11429"
  case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
      fetch "$llama/llama-b11429-bin-win-cuda-13.4-x64.zip" 76ddc6eff2389570789ed608881efc6977751a722015d8e8c94f302224ff1a3a llama-b11429-bin-win-cuda-13.4-x64.zip
      fetch "$llama/cudart-llama-bin-win-cuda-13.4-x64.zip" 738f8c251ac22b70c3ae6f83a10cf222725df0395246a2cf58f32bdb85fbe668 cudart-llama-bin-win-cuda-13.4-x64.zip
      if [ ! -f "$tp/llama/llama-server.exe" ]; then
        mkdir -p "$tp/llama"
        for z in llama-b11429-bin-win-cuda-13.4-x64.zip cudart-llama-bin-win-cuda-13.4-x64.zip; do unzip -qo "$dl/$z" -d "$tp/llama"; done
      fi ;;
    Linux)
      fetch "$llama/llama-b11429-bin-ubuntu-x64.tar.gz" f6d25dde8f51133143d1453da4fd5f73b145127177612a283bf7995957af3392 llama-b11429-bin-ubuntu-x64.tar.gz
      if [ ! -x "$tp/llama/llama-server" ]; then
        mkdir -p "$tp/llama"
        tar -xzf "$dl/llama-b11429-bin-ubuntu-x64.tar.gz" -C "$tp/llama" --strip-components=1
      fi ;;
  esac
fi
echo "dependencies are in $tp"
