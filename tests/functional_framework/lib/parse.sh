# Sequence-file parsing: flattens IMPORT directives into one ordered list of
# "origin\tinstruction" lines. Sourced by run_sequence.sh.
#
# Portability note: this targets bash 3.2 (macOS system bash) - no
# associative arrays, no `mapfile`. Cycle detection uses a colon-separated
# string of already-visited realpaths instead of an assoc array.

# flatten_sequence <seq_file> <chain>
# Prints "origin_file:lineno<TAB>instruction" for every non-comment,
# non-blank, non-IMPORT line, recursively inlining IMPORTs in place.
# <chain> is a colon-separated list of realpaths already being imported,
# used to detect cycles; pass "" at the top level.
flatten_sequence() {
  local file="$1"
  local chain="$2"
  local dir realf lineno raw trimmed kw rest target

  if [ ! -f "$file" ]; then
    log_error "sequence file not found: $file"
    return 1
  fi

  dir=$(cd "$(dirname "$file")" && pwd)
  realf="$dir/$(basename "$file")"

  case ":$chain:" in
    *":$realf:"*)
      log_error "import cycle detected: $realf (chain: $chain)"
      return 1
      ;;
  esac
  chain="$chain:$realf"

  lineno=0
  while IFS= read -r raw || [ -n "$raw" ]; do
    lineno=$((lineno + 1))

    # trim leading/trailing whitespace
    trimmed="${raw#"${raw%%[![:space:]]*}"}"
    trimmed="${trimmed%"${trimmed##*[![:space:]]}"}"

    [ -z "$trimmed" ] && continue
    case "$trimmed" in
      \#*) continue ;;
    esac

    kw="${trimmed%% *}"
    if [ "$kw" = "$trimmed" ]; then
      rest=""
    else
      rest="${trimmed#* }"
    fi

    if [ "$kw" = "IMPORT" ]; then
      target="$rest"
      case "$target" in
        /*) : ;;
        *) target="$dir/$target" ;;
      esac
      if ! flatten_sequence "$target" "$chain"; then
        return 1
      fi
    else
      printf '%s:%s\t%s\n' "$file" "$lineno" "$trimmed"
    fi
  done < "$file"
}
