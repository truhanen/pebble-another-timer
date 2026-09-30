# Approval-based golden-file comparison for screenshots + the run log,
# sourced by run_sequence.sh. App-agnostic, like the rest of this
# framework - nothing here is specific to any one app.
#
# Requires ImageMagick (`magick compare` - ImageMagick 7's subcommand
# form - or a standalone `compare` binary, ImageMagick 6's form; either
# is fine) on PATH for image comparisons. Log comparison only needs plain
# `diff`, always available.
#
# A golden baseline is a directory per sequence (see run_sequence.sh's
# GOLDEN_SEQ_DIR), holding the approved screenshots (same NN_label.png
# names a run itself produces) plus a scrubbed copy of run.log. Meant to
# be committed to git: since it's just files, "what did an earlier commit
# consider correct" is simply `git show <commit>:path/to/golden/seq/file`
# - no separate framework needed for that part, this only handles the
# comparison step itself.

# Sets the global array IM_COMPARE_CMD to ("magick" "compare") or
# ("compare"), whichever is actually on PATH - ImageMagick 7 folds the old
# standalone `compare` binary into the `magick` subcommand and some
# packagings don't also install a standalone `compare` shim, so `magick
# compare` is tried first. An array (not a plain string handed to callers
# for them to word-split) so this doesn't depend on the caller's shell
# word-splitting settings.
_im_compare_cmd() {
  if command -v magick >/dev/null 2>&1; then
    IM_COMPARE_CMD=(magick compare)
  elif command -v compare >/dev/null 2>&1; then
    IM_COMPARE_CMD=(compare)
  else
    return 1
  fi
}

# Same idea as _im_compare_cmd but for drawing (used by _mask_rect_apply
# below) - sets IM_CONVERT_CMD to ("magick") or ("convert").
_im_convert_cmd() {
  if command -v magick >/dev/null 2>&1; then
    IM_CONVERT_CMD=(magick)
  elif command -v convert >/dev/null 2>&1; then
    IM_CONVERT_CMD=(convert)
  else
    return 1
  fi
}

# _mask_rect_apply <src_png> <geometry WxH+X+Y> <dst_png>
# Writes a copy of src_png to dst_png with a solid black rectangle drawn
# over the given region - a fallback for excluding some live-value area
# from golden screenshot comparison when nothing better is available (an
# app-side freeze hook - see the main README's "Golden-file regression
# testing" section - is strongly preferred where one exists: masking hides
# real regressions in that region too, not just noise). Masking both sides
# of a comparison identically makes that region a no-op for AE/diff
# purposes, rather than either freezing real device
# time or accepting golden failures on every run.
_mask_rect_apply() {
  local src="$1" geometry="$2" dst="$3"
  if [[ ! "$geometry" =~ ^([0-9]+)x([0-9]+)\+([0-9]+)\+([0-9]+)$ ]]; then
    log_error "invalid --mask-rect '$geometry' (expected WxH+X+Y, e.g. 200x30+0+198)"
    return 1
  fi
  local w="${BASH_REMATCH[1]}" h="${BASH_REMATCH[2]}" x="${BASH_REMATCH[3]}" y="${BASH_REMATCH[4]}"
  local x2=$((x + w)) y2=$((y + h))
  "${IM_CONVERT_CMD[@]}" "$src" -fill black -draw "rectangle $x,$y $x2,$y2" "$dst"
}

# Strips this framework's own "[HH:MM:SS] " log-line prefix (the HOST
# machine's real wall-clock time when each step ran, via lib/log.sh's
# _log_ts - NOT the emulator's own clock) before comparing two run logs,
# so two runs of the identical sequence produce byte-identical scrubbed
# output regardless of when either actually ran.
#
# Also normalizes the raw Unix-epoch argument on any auto re-pin
# `emu-set-time` line - see run_screenshot's own comment in lib/steps.sh:
# it re-pins the emulator's clock immediately before every screenshot by
# computing CLOCK_PIN_TS + real elapsed host seconds, so that epoch is
# itself host-wall-clock-derived and differs between any two runs even
# when nothing regressed. An explicit `CMD emu-set-time HH:MM:SS` step
# written directly in a .seq file (e.g. wipe_and_prep.seq's initial pin)
# is unaffected - it's a fixed string, not a computed epoch, so it's left
# alone and still compares byte-for-byte as before.
#
# Also normalizes/drops several other run_sequence.sh header lines that
# are invocation-dependent rather than behavior-dependent, confirmed by
# actually round-tripping promote_golden.sh's output through a live
# compare_golden run (the very first end-to-end exercise of golden
# comparison this framework has had) and finding all of these mismatch
# for reasons that have nothing to do with an actual regression:
# - `output dir: ...` always contains that run's own RUN_ID timestamp -
#   guaranteed to differ between any two runs, ever. Dropped entirely.
# - `golden mode: ...` only gets logged on an invocation that actually
#   passed --golden-dir - a baseline captured via a plain run (e.g. one
#   later promoted with promote_golden.sh, which doesn't invoke
#   run_sequence.sh's golden machinery at all) never has this line.
#   Dropped entirely.
# - `sequence: <path>` and every per-step `[<path>:<line>]` origin
#   prefix embed the .seq path exactly as given on the command line
#   (via IMPORT chains too) - absolute vs. relative, or just a different
#   invocation CWD, changes this without changing anything about what
#   actually ran. Reduced to the path's basename.
# - every `pebble screenshot ... --no-open <path>` command-echo line's own
#   destination path is `$RUN_OUT_DIR/<NN_label.png>` - RUN_OUT_DIR always
#   contains that run's own RUN_ID, guaranteed to differ between any two
#   runs same as `output dir:` above. Reduced to just the filename.
# - the closing `sequence PASSED/FAILED - log: <path>` line has the exact
#   same RUN_OUT_DIR/RUN_ID problem - reduced to just `run.log`.
_scrub_log() {
  sed -E \
    -e 's/^\[[0-9]{2}:[0-9]{2}:[0-9]{2}\] //' \
    -e 's/(emu-set-time[^0-9]*)[0-9]{9,}/\1<EPOCH>/' \
    -e '/^output dir: /d' \
    -e '/^golden mode: /d' \
    -e 's#^sequence: .*/([^/]+)$#sequence: \1#' \
    -e 's#^\[[^]]*/([^]/]+:[0-9]+)\]#[\1]#' \
    -e 's#(pebble screenshot.*--no-open )[^[:space:]]*/([^/[:space:]]+\.png)$#\1\2#' \
    -e 's#^(sequence PASSED - log: |ERROR: sequence FAILED - log: ).*/(run\.log)$#\1\2#' \
    "$1"
}

# update_golden <run_out_dir> <golden_seq_dir>
# Approves the fresh run's screenshots + scrubbed log as the new baseline,
# overwriting whatever was there before.
update_golden() {
  local run_dir="$1" golden_dir="$2"
  # Refuse to approve a baseline from a native run: the emulator's
  # displayed clock/elapsed counters are only reproducible run-to-run
  # under the containerized harness's pinned libfaketime clock (see
  # tests/functional/docker/README.md's "Why this exists at all") - a
  # native run's screenshots can differ on every single invocation for
  # reasons that have nothing to do with an actual regression, making them
  # actively harmful as a golden baseline (every future native comparison
  # against them would be spuriously noisy, and even a future
  # containerized comparison could subtly mismatch if the native baseline
  # happened to capture a clock value the masking wasn't tuned for). The
  # marker this checks for (run_sequence.sh's own `.containerized` file,
  # written only when PEBBLE_TEST_CONTAINERIZED=1) is set unconditionally
  # by run-sequence-in-container.sh, so any run produced via
  # tests/functional/docker/ already satisfies this - nothing extra to do
  # for the normal case. PEBBLE_TEST_ALLOW_NATIVE_GOLDEN=1 is a loud,
  # deliberately inconvenient (env var, not a flag) escape hatch for a
  # genuine one-off exception; there should be no routine reason to use it.
  if [ ! -e "$run_dir/.containerized" ] && [ "${PEBBLE_TEST_ALLOW_NATIVE_GOLDEN:-}" != "1" ]; then
    log_error "refusing to approve golden baseline from a native (non-containerized) run: $run_dir"
    log_error "golden baselines must come from tests/functional/docker/ (run_container.sh / run_all_parallel.sh) - see that directory's README for why."
    log_error "genuine one-off exception: set PEBBLE_TEST_ALLOW_NATIVE_GOLDEN=1 (not recommended)."
    return 1
  fi
  mkdir -p "$golden_dir"
  rm -f "$golden_dir"/*.png
  # A sequence with zero screenshots is unusual but not an error - only
  # copy if there's anything to copy, so this doesn't fail on the glob
  # not matching.
  local any_png=0
  local f
  for f in "$run_dir"/*.png; do
    [ -e "$f" ] || continue
    any_png=1
    cp "$f" "$golden_dir/"
  done
  [ "$any_png" = "0" ] && log_info "note: no screenshots produced by this run"
  _scrub_log "$run_dir/run.log" > "$golden_dir/run.log"
  log_info "golden updated: $golden_dir"
}

# compare_golden <run_out_dir> <golden_seq_dir> <fuzz_percent> [mask_rect]
# Returns 0 if the run matches its golden baseline (screenshots pixel-
# identical within `fuzz_percent`, same set of files, scrubbed run.log
# identical), 1 otherwise. Writes per-file diff images/text for any
# mismatch into <run_out_dir>/diffs/ and leaves that directory absent if
# everything matched. `mask_rect`, if given (WxH+X+Y), is blacked out on
# BOTH images before comparing (see _mask_rect_apply) - use it to exclude
# a screen region that's expected to vary run-to-run regardless of
# behavior, e.g. this app's clock/status bar (its own README/app.conf
# usage passes one for exactly that).
compare_golden() {
  local run_dir="$1" golden_dir="$2" fuzz="${3:-0}" mask_rect="${4:-}"
  local mismatch=0

  if [ ! -d "$golden_dir" ]; then
    log_error "no golden baseline at $golden_dir - run with --update-golden first"
    return 1
  fi

  local IM_COMPARE_CMD=()
  if ! _im_compare_cmd; then
    log_error "ImageMagick not found (need 'magick' or 'compare' on PATH) - cannot compare screenshots"
    return 1
  fi
  local IM_CONVERT_CMD=()
  if [ -n "$mask_rect" ] && ! _im_convert_cmd; then
    log_error "ImageMagick not found (need 'magick' or 'convert' on PATH) - cannot apply --mask-rect"
    return 1
  fi

  # Snapshot the scrubbed run.log BEFORE anything below calls log_error -
  # log_error (lib/log.sh) tees to the SAME run.log this function is about
  # to compare, so reading it again at the end (after this function's own
  # screenshot-mismatch errors have already been appended to it) would be
  # comparing a file that's still being written to by this very
  # comparison, self-referentially - live-verified: this produced a
  # phantom run.log mismatch showing this function's OWN earlier ERROR
  # lines as unexpected "new" content on every run that had any screenshot
  # mismatch at all.
  local run_log_snapshot=""
  [ -f "$run_dir/run.log" ] && run_log_snapshot="$(_scrub_log "$run_dir/run.log")"

  local diff_dir="$run_dir/diffs"
  mkdir -p "$diff_dir"
  local mask_tmp=""
  [ -n "$mask_rect" ] && mask_tmp="$diff_dir/.masked" && mkdir -p "$mask_tmp"

  # Every golden PNG must exist in this run, pixel-identical within fuzz.
  local golden_png
  for golden_png in "$golden_dir"/*.png; do
    [ -e "$golden_png" ] || continue
    local fname
    fname="$(basename "$golden_png")"
    local run_png="$run_dir/$fname"
    if [ ! -f "$run_png" ]; then
      log_error "golden mismatch: $fname was produced before but is MISSING from this run"
      mismatch=1
      continue
    fi
    # Compare masked COPIES when a mask is given - the stored golden PNG
    # and this run's own screenshot both stay untouched on disk (so a
    # human reviewing either still sees the real clock), only these
    # throwaway temp copies feed into `compare`.
    local golden_cmp="$golden_png" run_cmp="$run_png"
    if [ -n "$mask_rect" ]; then
      golden_cmp="$mask_tmp/golden_$fname"
      run_cmp="$mask_tmp/run_$fname"
      _mask_rect_apply "$golden_png" "$mask_rect" "$golden_cmp" || return 1
      _mask_rect_apply "$run_png" "$mask_rect" "$run_cmp" || return 1
    fi
    # ImageMagick's compare prints the AE (Absolute Error - count of
    # differing pixels, as "<count> (<normalized 0-1 fraction>)", e.g.
    # "0 (0)" or "100 (1)") to stderr regardless of exit status, and also
    # writes a visual diff image to the given output path. Its own exit
    # status is 0 only when the images match within the given fuzz - more
    # robust to key on than parsing the text (confirmed empirically: exit
    # 0 for an identical pair, exit 1 for a fully-different pair), so that
    # drives the pass/fail decision; the leading number is only used for
    # the human-readable message.
    local ae_output cmp_status
    ae_output="$("${IM_COMPARE_CMD[@]}" -metric AE -fuzz "${fuzz}%" "$golden_cmp" "$run_cmp" \
        "$diff_dir/$fname" 2>&1 1>/dev/null)"
    cmp_status=$?
    if [ "$cmp_status" -ne 0 ]; then
      log_error "golden mismatch: $fname differs (${ae_output%% *} pixel(s), fuzz=${fuzz}%) - see $diff_dir/$fname"
      mismatch=1
    else
      rm -f "$diff_dir/$fname"   # matched - no diff image worth keeping
    fi
  done
  rm -rf "$mask_tmp"

  # A screenshot this run produced that golden doesn't have at all is new
  # or renamed - flag it too rather than silently ignoring it.
  local run_png
  for run_png in "$run_dir"/*.png; do
    [ -e "$run_png" ] || continue
    local fname
    fname="$(basename "$run_png")"
    if [ ! -f "$golden_dir/$fname" ]; then
      log_error "golden mismatch: $fname is new (not in golden baseline) - run --update-golden if intentional"
      mismatch=1
    fi
  done

  if [ -f "$golden_dir/run.log" ]; then
    if ! diff -u "$golden_dir/run.log" <(printf '%s\n' "$run_log_snapshot") \
        > "$diff_dir/run.log.diff"; then
      log_error "golden mismatch: run.log differs - see $diff_dir/run.log.diff"
      mismatch=1
    else
      rm -f "$diff_dir/run.log.diff"
    fi
  fi

  rmdir "$diff_dir" 2>/dev/null   # tidy up if nothing needed keeping

  if [ "$mismatch" = "1" ]; then
    log_error "golden comparison FAILED against $golden_dir"
    return 1
  fi
  log_info "golden comparison PASSED against $golden_dir"
  return 0
}
