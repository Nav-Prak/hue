#!/bin/bash
# tools/fuzz_local.sh
#
# Builds the six sanitized fuzz harnesses in this checkout and starts or
# resumes the campaign. Run it again after a restart, a sleep or a `git pull`:
# finished targets stay finished, unfinished ones continue, and a target whose
# harness was rebuilt differently starts again. Extra arguments go to
# tools/fuzz_campaign.py (for example: --seconds-per-target 3600 --jobs 3).
#
# Output:  $HUE_FUZZ_OUTPUT (default ~/hue-fuzz-out)
# Console: <output>.log, appended
set -euo pipefail
cd "$(dirname "$0")/.."
output="${HUE_FUZZ_OUTPUT:-$HOME/hue-fuzz-out}"
log="$output.log"
{
    echo "=== $(date -u +%Y-%m-%dT%H:%M:%SZ) $(git rev-parse --short HEAD) ==="
    if [ ! -f build/linux-clang-sanitized/CMakeCache.txt ]; then
        cmake --preset linux-clang-sanitized -DHUE_FUZZ=ON -DHUE_TRACY_ENABLE=OFF > "$output.configure.log" 2>&1
    fi
    cmake --build --preset linux-sanitized --target fuzz_gltf fuzz_animation_events fuzz_scene \
        fuzz_tuning fuzz_shader_contract fuzz_wav > "$output.build.log" 2>&1
} >> "$log" 2>&1
exec python3 tools/fuzz_campaign.py --binary-dir build/linux-clang-sanitized/tools/fuzz \
    --output "$output" "$@" >> "$log" 2>&1
