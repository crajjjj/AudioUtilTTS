#!/usr/bin/env bash
# Voice training for AudioUtilTTS: fine-tune Kokoro on recordings of one speaker
# and export a model folder the plugin loads. Runs on Linux with an NVIDIA GPU
# (WSL2 on Windows); voice.ps1 is the Windows front end to this script.
#
#   voice.sh setup                                   once: tools, the recipe, base weights
#   voice.sh prepare <name> <clips folder>           clips + their text -> a training set
#   voice.sh train   <name>                          stage one, then stage two
#   voice.sh export  <name> <out folder> [types]     -> <out>/<name>, a model folder
#   voice.sh make    <name> <clips> <out> [types]    prepare, train and export in one go
#   voice.sh status  [name]
#
# [types] is a comma separated list of voice type editor IDs the voice speaks
# for (MaleNord,MaleGuard); it is written into the folder's model.toml.
#
# Settings, as environment variables:
#   VOICETRAIN_HOME   work folder                       (~/audioutiltts-voicetrain)
#   STEPS1, STEPS2    training steps of the two stages  (4400, 2200)
#   BATCH             batch size                        (2)
#   GPU_FRACTION      share of the card PyTorch may use (0.90)
#   DEVICE            cuda or cpu, for the voice extraction (cuda)
#   FRESH=1           train stage one again even when it is done
#   FORCE=1           start although another training seems to be running
#   DRY_RUN=1         train: write the configs and show the plan, train nothing
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
W="${VOICETRAIN_HOME:-$HOME/audioutiltts-voicetrain}"
RECIPE="$W/kikiri-tts"
TRAIN="$W/train-venv"
STOCK="$W/stock"
VOICES="$W/voices"
STEPS1="${STEPS1:-4400}"
STEPS2="${STEPS2:-2200}"
BATCH="${BATCH:-2}"
GPU_FRACTION="${GPU_FRACTION:-0.90}"
DEVICE="${DEVICE:-cuda}"
export PATH="$HOME/.local/bin:$PATH"

# The training recipe and the alignment extension, at the commits the voices so
# far were trained with. The recipe is patched below, so it is not left to float.
RECIPE_URL="https://github.com/semidark/kikiri-tts"
RECIPE_COMMIT="a12d0410e89841e6f3c09958ae5c072f90ae1d49"
ALIGN_URL="https://github.com/resemble-ai/monotonic_align.git"
ALIGN_COMMIT="c6e5e6cb19882164027eb6e35118e841eed9298e"
# tokens.txt and espeak-ng-data of an exported model come from this package
STOCK_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_0.tar.bz2"

say() { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

usage() {
    sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

valid_name() {
    case "$1" in
        ''|*[!A-Za-z0-9_-]*) die "a voice name may hold letters, digits, - and _ only: '$1'" ;;
    esac
}

# The command line for <command> [name], in the front end this was started from:
# voice.ps1 sets VOICE_FRONT=ps, so that a Windows user is shown what to type.
how() {
    local name="${2:-<name>}"
    if [ "${VOICE_FRONT:-}" = "ps" ]; then
        case "$1" in
            setup) printf '.\\voice.ps1 setup' ;;
            prepare) printf '.\\voice.ps1 prepare -Name %s -Clips <clips folder>' "$name" ;;
            train) printf '.\\voice.ps1 train -Name %s' "$name" ;;
            export) printf '.\\voice.ps1 export -Name %s -Out <folder>' "$name" ;;
        esac
    else
        case "$1" in
            setup) printf 'voice.sh setup' ;;
            prepare) printf 'voice.sh prepare %s <clips folder>' "$name" ;;
            train) printf 'voice.sh train %s' "$name" ;;
            export) printf 'voice.sh export %s <out folder>' "$name" ;;
        esac
    fi
}

need_setup() {
    [ -f "$W/.setup-done" ] || die "not set up yet. Run: $(how setup)"
}

training_running() {
    pgrep -f "train_first.py|train_second.py" >/dev/null 2>&1
}

# ---------------------------------------------------------------------- setup

cmd_setup() {
    mkdir -p "$W" "$VOICES"
    say "work folder: $W"

    step "1/8 system tools"
    local missing=""
    for tool in git curl ffmpeg ffprobe espeak-ng bzip2 gcc; do
        command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
    done
    if [ -n "$missing" ]; then
        say "installing (missing:$missing)"
        local sudo=""
        [ "$(id -u)" -eq 0 ] || sudo="sudo"
        $sudo apt-get update -qq || die "apt-get update failed"
        $sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
            git build-essential curl ca-certificates espeak-ng libsndfile1 ffmpeg bzip2 >/dev/null \
            || die "apt-get install failed"
    fi
    say "ok"

    step "2/8 uv (Python package manager)"
    if ! command -v uv >/dev/null 2>&1; then
        curl -LsSf https://astral.sh/uv/install.sh | sh >/dev/null 2>&1 || die "could not install uv"
    fi
    uv --version || die "uv is not on the PATH"

    step "3/8 the training recipe"
    if [ ! -d "$RECIPE/.git" ]; then
        git clone --quiet "$RECIPE_URL" "$RECIPE" || die "could not clone $RECIPE_URL"
    fi
    git -C "$RECIPE" checkout --quiet "$RECIPE_COMMIT" 2>/dev/null \
        || { git -C "$RECIPE" fetch --quiet origin && git -C "$RECIPE" checkout --quiet "$RECIPE_COMMIT"; } \
        || die "could not check out the recipe at $RECIPE_COMMIT"
    git -C "$RECIPE" submodule update --init --recursive --quiet || die "could not fetch the recipe's submodules"
    # Stage two ships with PyTorch's anomaly detection switched on, which records
    # a stack trace for every operation: about eight times slower.
    sed -i 's/^torch\.autograd\.set_detect_anomaly(True)/torch.autograd.set_detect_anomaly(False)/' \
        "$RECIPE/StyleTTS2/train_second.py"
    grep -q '^torch\.autograd\.set_detect_anomaly(False)' "$RECIPE/StyleTTS2/train_second.py" \
        || say "note: the anomaly detection line of train_second.py was not found; stage two may run slowly"
    say "recipe at $(git -C "$RECIPE" rev-parse --short HEAD)"

    step "4/8 the recipe's own Python environment (phonemes, export)"
    (cd "$RECIPE" && uv sync --quiet) || die "uv sync failed in $RECIPE"
    uv pip install --quiet --python "$RECIPE/.venv/bin/python" onnx || die "could not install onnx"
    # the English front end fetches its language model the first time it is used
    "$RECIPE/.venv/bin/python" - >/dev/null 2>&1 <<'PY' || die "the English front end (misaki) does not start"
from misaki import en, espeak

en.G2P(trf=False, british=False, fallback=espeak.EspeakFallback(british=False))("The road is long.")
PY
    say "ok"

    step "5/8 the training environment (a large download the first time)"
    [ -x "$TRAIN/bin/python" ] || uv venv --quiet "$TRAIN" --python 3.12 || die "could not create $TRAIN"
    uv pip install --quiet --python "$TRAIN/bin/python" -r "$HERE/requirements-train.txt" \
        || die "could not install the training packages"
    if ! "$TRAIN/bin/python" -c "import monotonic_align" >/dev/null 2>&1; then
        # kept outside the recipe: a checkout folder of the same name inside
        # StyleTTS2 hides the package from the training scripts
        if [ ! -d "$W/monotonic_align/.git" ]; then
            git clone --quiet "$ALIGN_URL" "$W/monotonic_align" || die "could not clone $ALIGN_URL"
        fi
        git -C "$W/monotonic_align" checkout --quiet "$ALIGN_COMMIT" || die "could not check out monotonic_align"
        uv pip install --quiet --python "$TRAIN/bin/python" "$W/monotonic_align" \
            || die "could not build monotonic_align"
    fi
    say "ok"

    step "6/8 Kokoro base weights"
    if [ ! -f "$RECIPE/training/kokoro_base.pth" ]; then
        (cd "$RECIPE" && "$TRAIN/bin/python" - <<'PY'
import torch
from huggingface_hub import hf_hub_download

path = hf_hub_download("hexgrad/Kokoro-82M", "kokoro-v1_0.pth")
raw = torch.load(path, map_location="cpu", weights_only=False)
strip = lambda sd: {k.replace("module.", ""): v for k, v in sd.items()}
net = {k: strip(raw[k]) for k in ("bert", "bert_encoder", "predictor", "text_encoder", "decoder")}
torch.save({"net": net}, "training/kokoro_base.pth")
PY
        ) || die "could not fetch the Kokoro base weights"
    fi
    say "ok"

    step "7/8 tokens and espeak-ng data for exported models"
    if [ ! -f "$STOCK/tokens.txt" ] || [ ! -d "$STOCK/espeak-ng-data" ]; then
        rm -rf "$W/_stock" && mkdir -p "$W/_stock" "$STOCK"
        say "downloading the stock Kokoro package (333 MB)"
        curl -fsSL -o "$W/_stock/base.tar.bz2" "$STOCK_URL" || die "download failed: $STOCK_URL"
        tar -xjf "$W/_stock/base.tar.bz2" -C "$W/_stock" || die "could not unpack the stock package"
        cp "$W/_stock/kokoro-multi-lang-v1_0/tokens.txt" "$STOCK/" || die "tokens.txt missing in the stock package"
        rm -rf "$STOCK/espeak-ng-data"
        cp -r "$W/_stock/kokoro-multi-lang-v1_0/espeak-ng-data" "$STOCK/" || die "espeak-ng-data missing"
        rm -rf "$W/_stock"
    fi
    say "ok"

    step "8/8 check"
    (cd "$RECIPE/StyleTTS2" && "$TRAIN/bin/python" - <<'PY'
import importlib
import sys

import torch

sys.path.insert(0, ".")
from monotonic_align import maximum_path  # noqa: F401

for name in ("meldataset", "models", "losses", "utils", "optimizers"):
    importlib.import_module(name)
print("training code imports: ok")
if torch.cuda.is_available():
    total = torch.cuda.get_device_properties(0).total_memory / 2**30
    print(f"GPU: {torch.cuda.get_device_name(0)}, {total:.0f} GB")
    if total < 11:
        print("note: stage one peaked at 9.2 GB at batch size 2; this card may be too small")
else:
    print("NO GPU visible to PyTorch. Training needs an NVIDIA card; under WSL, a current")
    print("Windows NVIDIA driver and WSL 2 (check with: nvidia-smi).")
    sys.exit(3)
PY
    ) || die "the check failed (see above)"
    date -Is > "$W/.setup-done"
    say ""
    say "Setup done. Next: $(how prepare)"
}

# -------------------------------------------------------------------- prepare

cmd_prepare() {
    need_setup
    [ $# -ge 2 ] || die "usage: voice.sh prepare <name> <clips folder>"
    local name="$1" clips="$2"
    valid_name "$name"
    [ -d "$clips" ] || die "clips folder not found: $clips"
    local v="$VOICES/$name"
    mkdir -p "$v"
    step "preparing '$name' from $clips"
    if ! "$RECIPE/.venv/bin/python" "$HERE/prepare.py" "$clips" "$v" "$RECIPE"; then
        rmdir "$v" 2>/dev/null  # nothing was written: leave no empty voice behind
        die "prepare failed (see above)"
    fi
    say ""
    say "Next: $(how train "$name")"
}

# ---------------------------------------------------------------------- train

# Shows a training run's progress: one step line in ten, and what matters.
progress() {
    awk '
        /^Epoch \[/ {
            s = $0; sub(/^.*Step \[/, "", s); sub(/\/.*$/, "", s)
            if (s % 100 == 0 || s == 10) { line = $0; sub(/, Gen Loss.*$/, "", line); print "    " line; fflush() }
            next
        }
        /Validation loss|Saving|Traceback|Error|out of memory|GPU memory cap/ { print "    " $0; fflush() }
    '
}

run_stage() {  # <script> <config> <log file>
    (cd "$RECIPE/StyleTTS2" && PYTHONUNBUFFERED=1 PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True \
        "$TRAIN/bin/python" "$HERE/run_capped.py" "$GPU_FRACTION" "$1" --config_path "$2" 2>&1) \
        | tee "$3" | progress
}

cmd_train() {
    need_setup
    [ $# -ge 1 ] || die "usage: voice.sh train <name>"
    local name="$1"
    valid_name "$name"
    local v="$VOICES/$name"
    [ -s "$v/train_list.txt" ] || die "'$name' is not prepared. Run: $(how prepare "$name")"

    local lines per_epoch e1 e2 save2
    lines=$(wc -l < "$v/train_list.txt")
    per_epoch=$(( lines / BATCH ))
    [ "$per_epoch" -ge 1 ] || die "too few lines to train on: $lines"
    e1=$(( (STEPS1 + per_epoch - 1) / per_epoch ))
    e2=$(( (STEPS2 + per_epoch - 1) / per_epoch ))
    # Stage two saves when the epoch number divides by save_freq and never at
    # its end, so save_freq is chosen to hit the last epoch (number e2 - 1).
    save2=$(( e2 - 1 ))
    [ "$save2" -lt 1 ] && save2=1
    local workers
    workers=$(nproc 2>/dev/null || echo 4)
    [ "$workers" -gt 8 ] && workers=8

    # the recipe's own config with only the run-specific keys changed
    sed -e "s/^batch_size: .*/batch_size: ${BATCH}/" \
        -e "s/^epochs_1st: .*/epochs_1st: ${e1}/" \
        -e "s/^epochs_2nd: .*/epochs_2nd: ${e2}/" \
        -e "s/^save_freq: .*/save_freq: 1000/" \
        -e "s#^log_dir: .*#log_dir: \"${v}/run\"#" \
        -e "s#train_data: .*#train_data: \"${v}/train_list.txt\"#" \
        -e "s#val_data: .*#val_data: \"${v}/val_list.txt\"#" \
        -e "s#root_path: .*#root_path: \"${v}/audio\"#" \
        -e "s#OOD_data: .*#OOD_data: \"${v}/OOD_texts.txt\"#" \
        -e "s/num_workers: .*/num_workers: ${workers}/" \
        -e 's/^\(  joint_epoch: \)[0-9]*/\1999/' \
        "$RECIPE/configs/config_german_ft.yml" > "$v/config_1.yml" || die "could not write the config"
    sed -e "s/^save_freq: .*/save_freq: ${save2}/" "$v/config_1.yml" > "$v/config_2.yml"

    step "training '$name': $lines lines, $per_epoch steps an epoch"
    say "stage one $e1 epochs (about $STEPS1 steps), stage two $e2 epochs (about $STEPS2 steps)"
    say "full output: $v/stage1.log and stage2.log"
    if [ "${DRY_RUN:-0}" = "1" ]; then
        say "DRY_RUN: the configs are written ($v/config_1.yml, config_2.yml), nothing is trained"
        return 0
    fi
    if training_running && [ "${FORCE:-0}" != "1" ]; then
        die "another training is running on this machine (the setting FORCE=1 starts anyway)"
    fi

    if [ -f "$v/run/first_stage.pth" ] && [ "${FRESH:-0}" != "1" ]; then
        say ""
        say "stage one is done already (the setting FRESH=1 trains it again)"
    else
        step "stage one, started $(date +%H:%M)"
        rm -rf "$v/run"
        run_stage train_first.py "$v/config_1.yml" "$v/stage1.log"
        if [ ! -f "$v/run/first_stage.pth" ]; then
            say ""
            tail -n 12 "$v/stage1.log" | cut -c1-200
            die "stage one did not finish (the lines above are the end of $v/stage1.log)"
        fi
        rm -f "$v/run"/epoch_1st_*.pth
    fi

    # Stage two always starts from first_stage.pth: it cannot be continued from
    # one of its own checkpoints (the loader would take nothing from it).
    step "stage two, started $(date +%H:%M)"
    rm -f "$v/run"/epoch_2nd_*.pth
    run_stage train_second.py "$v/config_2.yml" "$v/stage2.log"
    local last
    last=$(ls "$v/run"/epoch_2nd_*.pth 2>/dev/null | sort | tail -n 1)
    if [ -z "$last" ]; then
        say ""
        tail -n 12 "$v/stage2.log" | cut -c1-200
        die "stage two left no checkpoint (the lines above are the end of $v/stage2.log)"
    fi
    local f
    for f in "$v/run"/epoch_2nd_*.pth; do
        [ "$f" != "$last" ] && rm -f "$f"
    done
    say ""
    say "Trained, finished $(date +%H:%M). Next: $(how export "$name")"
}

# --------------------------------------------------------------------- export

cmd_export() {
    need_setup
    [ $# -ge 2 ] || die "usage: voice.sh export <name> <out folder> [voice types]"
    local name="$1" out="$2" types="${3:-}"
    valid_name "$name"
    local v="$VOICES/$name"
    local s1="$v/run/first_stage.pth" s2
    s2=$(ls "$v/run"/epoch_2nd_*.pth 2>/dev/null | sort | tail -n 1)
    [ -f "$s1" ] && [ -n "$s2" ] || die "'$name' is not trained. Run: $(how train "$name")"
    mkdir -p "$out" || die "cannot create $out"

    step "1/4 the voice"
    local device="$DEVICE"
    if ! (cd "$RECIPE" && "$TRAIN/bin/python" scripts/extract_voicepack.py --model "$s2" \
            --style-encoder-model "$s1" --audio-dir "$v/audio" --output "$v/voice.pt" \
            --device "$device") > "$v/voicepack.log" 2>&1; then
        say "on the GPU it failed, trying the CPU"
        (cd "$RECIPE" && "$TRAIN/bin/python" scripts/extract_voicepack.py --model "$s2" \
            --style-encoder-model "$s1" --audio-dir "$v/audio" --output "$v/voice.pt" \
            --device cpu) > "$v/voicepack.log" 2>&1
    fi
    [ -f "$v/voice.pt" ] || { tail -n 8 "$v/voicepack.log"; die "the voice could not be extracted"; }
    say "ok"

    step "2/4 the model"
    local model="$v/export/$name"
    rm -rf "$v/export" && mkdir -p "$model"
    (cd "$RECIPE" && "$RECIPE/.venv/bin/python" "$HERE/export.py" "$s2" "$v/voice.pt" "$model" "$name") \
        > "$v/export.log" 2>&1 || { tail -n 12 "$v/export.log"; die "the export failed"; }
    tail -n 1 "$v/export.log"
    cp "$STOCK/tokens.txt" "$model/" || die "tokens.txt missing, run setup again"
    cp -r "$STOCK/espeak-ng-data" "$model/" || die "espeak-ng-data missing, run setup again"
    if [ -n "$types" ]; then
        {
            printf '# Voice types this model speaks for: editor ID = voice number in this model.\n'
            printf '[voicetype]\n'
            local t
            for t in ${types//,/ }; do
                case "$t" in *[!A-Za-z0-9_]*) die "not a voice type editor ID: '$t'" ;; esac
                printf '%s = 0\n' "$t"
            done
        } > "$model/model.toml"
        say "model.toml: ${types//,/, }"
    fi

    step "3/4 load it the way the plugin does, and speak"
    rm -rf "$v/samples"
    "$TRAIN/bin/python" "$HERE/speak.py" "$model" "$v/samples" 2>&1 | grep -v "espeak-ng-data" \
        || true
    if ! ls "$v/samples"/*.wav >/dev/null 2>&1; then
        die "the exported model did not load or speak. Do NOT install it: a model the runtime cannot read closes the game."
    fi

    step "4/4 copy"
    rm -rf "${out:?}/$name" "${out:?}/$name-samples"
    cp -r "$model" "$out/$name" || die "could not copy the model to $out"
    cp -r "$v/samples" "$out/$name-samples" || die "could not copy the samples to $out"
    say "model:   $out/$name  ($(du -sh "$out/$name" | cut -f1))"
    say "samples: $out/$name-samples  (listen to these first)"
    say ""
    say "To install: copy the folder '$name' into Data\\SKSE\\Plugins\\AudioUtilTTS\\models"
}

# --------------------------------------------------------------------- status

cmd_status() {
    say "work folder: $W"
    [ -f "$W/.setup-done" ] && say "setup: done $(cat "$W/.setup-done")" || say "setup: not done"
    training_running && say "a training is running now"
    local v name state
    for v in "$VOICES"/${1:-*}/; do
        [ -d "$v" ] || continue
        v="${v%/}"
        name="$(basename "$v")"
        state="not prepared"
        [ -s "$v/train_list.txt" ] && state="prepared, $(wc -l < "$v/train_list.txt") lines"
        [ -f "$v/run/first_stage.pth" ] && state="$state; stage one done"
        ls "$v/run"/epoch_2nd_*.pth >/dev/null 2>&1 && state="$state; stage two done"
        [ -d "$v/export/$name" ] && state="$state; exported"
        say "  $name: $state"
        if [ -n "${1:-}" ]; then
            local log
            for log in "$v/stage2.log" "$v/stage1.log"; do
                if [ -f "$log" ]; then
                    say "  last step in $(basename "$log"): $(grep -a '^Epoch \[' "$log" | tail -n 1 | sed 's/, Gen Loss.*$//')"
                    break
                fi
            done
        fi
    done
}

# ----------------------------------------------------------------------- main

cmd="${1:-help}"
[ $# -gt 0 ] && shift
case "$cmd" in
    setup) cmd_setup "$@" ;;
    prepare) cmd_prepare "$@" ;;
    train) cmd_train "$@" ;;
    export) cmd_export "$@" ;;
    make)
        [ $# -ge 3 ] || die "usage: voice.sh make <name> <clips folder> <out folder> [voice types]"
        cmd_prepare "$1" "$2" && cmd_train "$1" && cmd_export "$1" "$3" "${4:-}"
        ;;
    status) cmd_status "$@" ;;
    help|-h|--help) usage ;;
    *) usage; die "unknown command: $cmd" ;;
esac
