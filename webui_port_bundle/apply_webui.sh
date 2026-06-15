#!/usr/bin/env bash
set -euo pipefail

target=""
patch_brain=""
no_backup=0
server_url=""
has_ball_predictor=""

usage() {
  echo "Usage: ./apply_webui.sh [--target PATH] [--server-url URL_OR_IP] [--original-demo|--not-original-demo] [--patch-brain|--no-patch-brain] [--no-backup]"
}

normalize_server_url() {
  local value="$1"
  if [[ -z "$value" ]]; then
    echo ""
  elif [[ "$value" == http://* || "$value" == https://* ]]; then
    echo "$value"
  else
    echo "http://${value}:8000"
  fi
}

ask_yes_no() {
  local prompt="$1"
  local default="$2"
  local answer=""
  local hint="[y/N]"
  [[ "$default" == "y" ]] && hint="[Y/n]"

  while true; do
    read -r -p "$prompt $hint: " answer
    answer="${answer:-$default}"
    case "${answer,,}" in
      y|yes) return 0 ;;
      n|no) return 1 ;;
      *) echo "Please enter y or n." ;;
    esac
  done
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target|-t)
      target="$2"
      shift 2
      ;;
    --patch-brain)
      patch_brain=1
      shift
      ;;
    --no-patch-brain)
      patch_brain=0
      shift
      ;;
    --server-url|--windows-ip)
      server_url="$(normalize_server_url "$2")"
      shift 2
      ;;
    --original-demo|--no-ball-predictor)
      has_ball_predictor=0
      shift
      ;;
    --not-original-demo|--has-ball-predictor)
      has_ball_predictor=1
      shift
      ;;
    --no-backup)
      no_backup=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

echo "WebUI Linux robot patch"
echo "This only patches the Linux robot workspace. Windows frontend/backend are not copied."
echo

if [[ -z "$target" ]]; then
  read -r -p "Target demo directory [current directory]: " target
  target="${target:-.}"
fi

if [[ -z "$server_url" ]]; then
  read -r -p "Windows backend IP or URL, for example 192.168.15.21 or http://192.168.15.21:8000: " server_input
  server_url="$(normalize_server_url "$server_input")"
fi

if [[ -z "$has_ball_predictor" ]]; then
  echo
  echo "Target demo type?"
  echo "  Choose y for an original demo. This assumes there is NO k1_ball_predictor package."
  echo "  Choose n for a newer/custom demo. This assumes k1_ball_predictor exists."
  if ask_yes_no "Is this an original demo" "y"; then
    has_ball_predictor=0
  else
    has_ball_predictor=1
  fi
fi

if [[ -z "$patch_brain" ]]; then
  echo
  echo "Patch brain?"
  echo "  Choose y only if the target demo does not already publish /brain/status_json."
  echo "  For older demos, full brain overwrite may introduce version-only dependencies."
  if [[ "$has_ball_predictor" -eq 0 ]]; then
    echo "  Current selection: original demo / no k1_ball_predictor, so full brain patch will be skipped."
  fi
  if ask_yes_no "Patch brain now" "n"; then
    patch_brain=1
  else
    patch_brain=0
  fi
fi

if [[ "$patch_brain" -eq 1 && "$has_ball_predictor" -eq 0 ]]; then
  echo
  echo "skip brain patch: target is marked as original demo, so k1_ball_predictor is assumed missing."
  echo "The bundled brain patch depends on k1_ball_predictor and would fail to compile on that demo."
  patch_brain=0
fi

bundle_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
overlay_root="$bundle_root/overlay"
target_root="$(cd "$target" && pwd)"
stamp="$(date +%Y%m%d_%H%M%S)"

backup_or_remove() {
  local path="$1"
  [[ -e "$path" ]] || return 0

  if [[ "$no_backup" -eq 1 ]]; then
    rm -rf "$path"
    return 0
  fi

  local backup="${path}.backup.${stamp}"
  mv "$path" "$backup"
  echo "backup: $path -> $backup"
  if [[ -d "$backup" ]]; then
    touch "$backup/COLCON_IGNORE"
    echo "colcon ignore backup: $backup/COLCON_IGNORE"
  fi
}

ignore_existing_colcon_backups() {
  local backup=""
  shopt -s nullglob
  for backup in "$target_root"/src/*.backup.*; do
    if [[ -d "$backup" && ! -e "$backup/COLCON_IGNORE" ]]; then
      touch "$backup/COLCON_IGNORE"
      echo "colcon ignore existing backup: $backup/COLCON_IGNORE"
    fi
  done
  shopt -u nullglob
}

copy_directory_fresh() {
  local source="$1"
  local destination="$2"
  backup_or_remove "$destination"
  mkdir -p "$(dirname "$destination")"
  cp -a "$source" "$destination"
  echo "copy: $source -> $destination"
}

echo
echo "target: $target_root"
echo "server_base_url: ${server_url:-not set}"
echo "k1_ball_predictor: $([[ "$has_ball_predictor" -eq 1 ]] && echo present || echo missing/original-demo)"
echo "patch brain: $([[ "$patch_brain" -eq 1 ]] && echo yes || echo no)"
echo "mode: Linux robot patch only; Windows WebUI frontend/backend are not copied."

ignore_existing_colcon_backups
mkdir -p "$target_root/src"
copy_directory_fresh "$overlay_root/src/k1_robot_webui_client" "$target_root/src/k1_robot_webui_client"

if [[ -n "$server_url" ]]; then
  config_file="$target_root/src/k1_robot_webui_client/config/webui_client.yaml"
  python3 - "$config_file" "$server_url" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
url = sys.argv[2]
text = path.read_text()
lines = []
changed = False
for line in text.splitlines():
    stripped = line.lstrip()
    indent = line[: len(line) - len(stripped)]
    if stripped.startswith("server_base_url:"):
        lines.append(f'{indent}server_base_url: "{url}"')
        changed = True
    else:
        lines.append(line)
if not changed:
    lines.append(f'    server_base_url: "{url}"')
path.write_text("\n".join(lines) + "\n")
PY
  echo "set server_base_url: $server_url"
fi

if [[ "$patch_brain" -eq 1 ]]; then
  brain_cpp="$target_root/src/brain/src/brain.cpp"
  brain_h="$target_root/src/brain/include/brain.h"

  [[ -d "$(dirname "$brain_cpp")" ]] || { echo "Target does not look like a K1 demo: missing src/brain/src" >&2; exit 1; }
  [[ -d "$(dirname "$brain_h")" ]] || { echo "Target does not look like a K1 demo: missing src/brain/include" >&2; exit 1; }

  if ! ask_yes_no "Confirm overwriting target brain.cpp and brain.h from this bundle" "n"; then
    echo "skip brain patch."
  else
    backup_or_remove "$brain_cpp"
    cp -f "$overlay_root/src/brain/src/brain.cpp" "$brain_cpp"
    echo "copy patched brain.cpp"

    backup_or_remove "$brain_h"
    cp -f "$overlay_root/src/brain/include/brain.h" "$brain_h"
    echo "copy patched brain.h"
  fi
else
  echo "skip brain patch. Use --patch-brain only if the target demo lacks /brain/status_json."
fi

echo
echo "done. Next steps:"
echo "  1) Check $target_root/src/k1_robot_webui_client/config/webui_client.yaml"
echo "  2) Rebuild/source the ROS workspace on Linux."
echo "  3) Run/launch k1_robot_webui_client on Linux, while WebUI backend/frontend keep running on Windows."
echo
echo "If brain was overwritten and build fails with missing k1_ball_predictor, restore the backup brain.cpp/brain.h and rerun this script without brain patch."
