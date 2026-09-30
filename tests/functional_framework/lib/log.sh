# Logging helpers, sourced by run_sequence.sh. Requires $LOG_FILE to be set
# before any of these are called.

_log_ts() {
  date '+%H:%M:%S'
}

log_info() {
  local msg
  msg="[$(_log_ts)] $*"
  echo "$msg" | tee -a "$LOG_FILE"
}

log_error() {
  local msg
  msg="[$(_log_ts)] ERROR: $*"
  echo "$msg" | tee -a "$LOG_FILE" >&2
}

log_cmd() {
  echo "[$(_log_ts)] + $*" | tee -a "$LOG_FILE"
}
