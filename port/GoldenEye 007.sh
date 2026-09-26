#!/bin/bash
# GoldenEye 007 R36S / PortMaster launcher.
# User supplies the retail ROM; required sidecars are generated locally.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

if [ ! -f "$controlfolder/control.txt" ]; then
  echo "GoldenEye 007: PortMaster control.txt not found at $controlfolder"
  exit 1
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/ge007"
CONFDIR="$GAMEDIR/conf"
ROM="$GAMEDIR/data/ge007.ntsc-final.z64"
CONVERTER="$GAMEDIR/prepare-assets/ge007-convert"
GAME="$GAMEDIR/ge007.aarch64"

mkdir -p "$CONFDIR" "$GAMEDIR/data"
cd "$GAMEDIR" || exit 1

> "$GAMEDIR/log.txt"
exec > >(tee "$GAMEDIR/log.txt") 2>&1

export XDG_DATA_HOME="$CONFDIR"

# ---------------------------------------------------------------------------
# Optional per-game performance profile.
#
# Port Control writes only small numeric enums to data/ge007.ini. The launcher
# translates those enums into fixed known-safe governor/swappiness values,
# applies them through PortMaster's existing privilege helper when necessary,
# and restores every original kernel value when the game exits. The game
# binary itself never runs as root and never writes privileged sysfs/procfs.
# ---------------------------------------------------------------------------
PERF_INI="$GAMEDIR/data/ge007.ini"
PERF_STATE="$GAMEDIR/.perf-state.$"

perf_ini_int() {
  section="$1"
  key="$2"
  [ -f "$PERF_INI" ] || { echo 0; return; }
  awk -v want_section="$section" -v want_key="$key" '
    function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
    /^[ \t]*\[/ {
      sec=$0; gsub(/[\[\]\r]/, "", sec); sec=trim(sec); next
    }
    sec == want_section && index($0, "=") {
      k=$0; sub(/=.*/, "", k); k=trim(k)
      if (k == want_key) {
        v=$0; sub(/^[^=]*=/, "", v); sub(/[;#].*$/, "", v); v=trim(v)
        print v; exit
      }
    }' "$PERF_INI"
}

perf_write() {
  path="$1"
  value="$2"
  if [ -w "$path" ]; then
    printf '%s\n' "$value" > "$path"
    return $?
  fi
  if [ -n "${ESUDO:-}" ]; then
    printf '%s\n' "$value" | $ESUDO tee "$path" >/dev/null 2>&1
    return $?
  fi
  return 1
}

perf_backup_write() {
  path="$1"
  value="$2"
  label="$3"
  [ -r "$path" ] || return 1
  old="$(cat "$path" 2>/dev/null | tr -d '\r\n')"
  [ -n "$old" ] || return 1
  printf '%s|%s\n' "$path" "$old" >> "$PERF_STATE"
  if perf_write "$path" "$value"; then
    echo "[Performance] $label: $old -> $value"
    return 0
  fi
  echo "[Performance] $label: unable to write $path"
  return 1
}

perf_governor_supported() {
  path="$1"
  value="$2"
  dir="${path%/*}"
  avail="$dir/scaling_available_governors"
  [ -r "$avail" ] || avail="$dir/available_governors"
  if [ -r "$avail" ]; then
    grep -qw "$value" "$avail"
  else
    return 0
  fi
}

perf_restore() {
  [ -f "$PERF_STATE" ] || return 0
  echo "[Performance] restoring system settings"
  while IFS='|' read -r path old; do
    [ -n "$path" ] || continue
    if perf_write "$path" "$old"; then
      echo "[Performance] restored $path -> $old"
    else
      echo "[Performance] WARNING: failed to restore $path"
    fi
  done < "$PERF_STATE"
  rm -f "$PERF_STATE"
}

perf_apply() {
  # Recover from an interrupted previous launcher (power loss / shell kill)
  # before establishing this run's baseline.
  if [ -f "$PERF_STATE" ]; then
    echo "[Performance] stale restore state found; restoring previous baseline"
    perf_restore
  fi
  rm -f "$PERF_STATE"

  cpu_sel="$(perf_ini_int System CpuGovernor)"
  gpu_sel="$(perf_ini_int System GpuGovernor)"
  ram_sel="$(perf_ini_int System RamProfile)"

  case "$cpu_sel" in
    1) cpu_gov="schedutil" ;;
    2) cpu_gov="performance" ;;
    3) cpu_gov="powersave" ;;
    *) cpu_gov="" ;;
  esac

  case "$gpu_sel" in
    1) gpu_gov="simple_ondemand" ;;
    2) gpu_gov="performance" ;;
    3) gpu_gov="powersave" ;;
    *) gpu_gov="" ;;
  esac

  vfs_cache_pressure=""
  case "$ram_sel" in
    1) swappiness="10" ;;
    2) swappiness="40" ;;
    3)
      # Game profile: reduce swap pressure and retain file/dentry cache without
      # resizing zram, dropping caches, or making persistent kernel changes.
      swappiness="5"
      vfs_cache_pressure="50"
      ;;
    *) swappiness="" ;;
  esac

  if [ -n "$cpu_gov" ]; then
    found=0
    for p in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
      [ -f "$p" ] || continue
      found=1
      if perf_governor_supported "$p" "$cpu_gov"; then
        perf_backup_write "$p" "$cpu_gov" "CPU governor" || true
      else
        echo "[Performance] CPU governor '$cpu_gov' unavailable for $p"
      fi
    done
    [ "$found" -eq 1 ] || echo "[Performance] CPU cpufreq governor interface unavailable"
  fi

  if [ -n "$gpu_gov" ]; then
    found=0
    seen=""
    for p in /sys/class/devfreq/*gpu*/governor /sys/class/devfreq/*mali*/governor; do
      [ -f "$p" ] || continue
      case "|$seen|" in *"|$p|"*) continue ;; esac
      seen="${seen:+$seen|}$p"
      found=1
      if perf_governor_supported "$p" "$gpu_gov"; then
        perf_backup_write "$p" "$gpu_gov" "GPU governor" || true
      else
        echo "[Performance] GPU governor '$gpu_gov' unavailable for $p"
      fi
    done
    [ "$found" -eq 1 ] || echo "[Performance] GPU devfreq governor interface unavailable"
  fi

  if [ -n "$swappiness" ] && [ -r /proc/sys/vm/swappiness ]; then
    perf_backup_write /proc/sys/vm/swappiness "$swappiness" "VM swappiness" || true
  fi

  if [ -n "$vfs_cache_pressure" ] && [ -r /proc/sys/vm/vfs_cache_pressure ]; then
    perf_backup_write /proc/sys/vm/vfs_cache_pressure "$vfs_cache_pressure" "VM cache pressure" || true
  fi

  if [ -s "$PERF_STATE" ]; then
    echo "[Performance] profile active; originals will be restored on exit"
  else
    rm -f "$PERF_STATE"
    echo "[Performance] using system defaults / no writable tuning endpoints"
  fi
}

echo "=== GoldenEye 007 / R36S PortMaster alpha ==="
date
echo "directory=${directory:-unset}"
echo "GAMEDIR=$GAMEDIR"
echo "CFW=${CFW_NAME:-unknown}"
echo "DEVICE=${DEVICE:-unknown}"
echo "param_device=${param_device:-unknown}"

chmod +x "$GAME" "$CONVERTER" 2>/dev/null || true

if [ ! -x "$GAME" ]; then
  echo "[Binary] missing or not executable: $GAME"
  type pm_finish >/dev/null 2>&1 && pm_finish
  exit 4
fi

if [ ! -f "$ROM" ]; then
  echo "[ROM] MISSING: $ROM"
  echo "[ROM] Required SHA1: abe01e4aeb033b6c0836819f549c791b26cfde83"
  type pm_message >/dev/null 2>&1 && pm_message "GoldenEye ROM missing: ge007/data/ge007.ntsc-final.z64"
  type pm_finish >/dev/null 2>&1 && pm_finish
  exit 2
fi

if command -v sha1sum >/dev/null 2>&1; then
  ROM_SHA1="$(sha1sum "$ROM" | awk '{print $1}')"
  echo "[ROM] sha1=$ROM_SHA1"
  if [ "$ROM_SHA1" != "abe01e4aeb033b6c0836819f549c791b26cfde83" ]; then
    echo "[ROM] WRONG ROM: expected US NTSC big-endian retail image"
    type pm_message >/dev/null 2>&1 && pm_message "GoldenEye ROM has the wrong SHA-1."
    type pm_finish >/dev/null 2>&1 && pm_finish
    exit 5
  fi
fi

if [ ! -f "$GAMEDIR/data/pcmodels-ntsc-final/pcmodels.bin" ] || \
   [ ! -f "$GAMEDIR/data/pccg-ntsc-final/pccg.bin" ]; then
  echo "[Extract] sidecars missing; generating from user ROM"
  if ! command -v python3 >/dev/null 2>&1; then
    echo "[Extract] python3 not available on this firmware"
    type pm_message >/dev/null 2>&1 && pm_message "GoldenEye asset conversion needs python3."
    type pm_finish >/dev/null 2>&1 && pm_finish
    exit 6
  fi
  "$CONVERTER" --rom "$ROM" --out "$GAMEDIR"
  CONVERT_RC=$?
  echo "[Extract] converter rc=$CONVERT_RC"
  if [ "$CONVERT_RC" -ne 0 ]; then
    type pm_finish >/dev/null 2>&1 && pm_finish
    exit "$CONVERT_RC"
  fi
fi

if [ ! -f "$GAMEDIR/data/pcmodels-ntsc-final/pcmodels.bin" ] || \
   [ ! -f "$GAMEDIR/data/pccg-ntsc-final/pccg.bin" ]; then
  echo "[Extract] FAILED: required sidecars are still missing"
  type pm_finish >/dev/null 2>&1 && pm_finish
  exit 3
fi

echo "[Extract] PASS"

perf_apply
trap 'perf_restore' EXIT

echo "[Launch] starting ge007.aarch64"
"$GAME"
GAME_RC=$?
echo "[Exit] rc=$GAME_RC"

perf_restore
trap - EXIT

type pm_finish >/dev/null 2>&1 && pm_finish
exit "$GAME_RC"
