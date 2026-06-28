#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CONFIG_FILE="${ROOT_DIR}/src/brain/config/config.yaml"
STRIKER_XML="${ROOT_DIR}/src/brain/behavior_trees/subtrees/subtree_striker_play.xml"
GOALIE_XML="${ROOT_DIR}/src/brain/behavior_trees/subtrees/subtree_goal_keeper_play.xml"
BACKUP_DIR="${ROOT_DIR}/tools/config-backups"

if command -v python3 >/dev/null 2>&1; then
  PYTHON_BIN="python3"
elif command -v python >/dev/null 2>&1; then
  PYTHON_BIN="python"
else
  echo "ERROR: python3/python not found. This tool needs Python for safe text edits." >&2
  exit 1
fi

backup_all() {
  mkdir -p "${BACKUP_DIR}"
  local stamp
  stamp="$(date +%Y%m%d_%H%M%S)"
  local dir="${BACKUP_DIR}/${stamp}"
  mkdir -p "${dir}"
  cp "${CONFIG_FILE}" "${dir}/config.yaml"
  cp "${STRIKER_XML}" "${dir}/subtree_striker_play.xml"
  cp "${GOALIE_XML}" "${dir}/subtree_goal_keeper_play.xml"
  echo "已备份当前配置到: ${dir}"
}

run_py() {
  "${PYTHON_BIN}" - "$@"
}

show_status() {
  run_py "${CONFIG_FILE}" "${STRIKER_XML}" "${GOALIE_XML}" <<'PY'
import re
import sys
from pathlib import Path

config, striker_xml, goalie_xml = map(Path, sys.argv[1:4])

def yaml_get(path, section, key):
    current = None
    pattern_section = re.compile(r"^    ([A-Za-z0-9_]+):\s*(?:#.*)?$")
    pattern_key = re.compile(rf"^      {re.escape(key)}:\s*([^#\n]*?)(?:\s*#.*)?$")
    for line in path.read_text(encoding="utf-8").splitlines():
        m = pattern_section.match(line)
        if m:
            current = m.group(1)
            continue
        if current == section:
            m = pattern_key.match(line)
            if m:
                return m.group(1).strip()
    return "<not found>"

def xml_arc(path):
    text = path.read_text(encoding="utf-8")
    m = re.search(r'<Chase\b[^>]*\barc_walk="(true|false)"', text)
    return m.group(1) if m else "<not found>"

rows = [
    ("前锋 Chase 弧线追球 arc_walk", xml_arc(striker_xml)),
    ("守门员 Chase 弧线追球 arc_walk", xml_arc(goalie_xml)),
    ("Adjust 绕球超时秒数，0=关闭", yaml_get(config, "strategy", "adjust_timeout_secs")),
    ("球路预测总开关 enable", yaml_get(config, "ball_prediction", "enable")),
    ("球路预测是否接管追球 use_for_chase", yaml_get(config, "ball_prediction", "use_for_chase")),
    ("球路预测提前量 predict_time", yaml_get(config, "ball_prediction", "predict_time")),
    ("丢球预测保持时间 lost_timeout", yaml_get(config, "ball_prediction", "lost_prediction_timeout")),
    ("定位球阶段禁用预测追球", yaml_get(config, "ball_prediction", "disable_for_set_play_chase")),
]

print("\n当前测试开关状态")
print("-" * 64)
for name, value in rows:
    print(f"{name:<42} {value}")
print("-" * 64)
PY
}

set_yaml_value() {
  local section="$1"
  local key="$2"
  local value="$3"
  backup_all
  run_py "${CONFIG_FILE}" "${section}" "${key}" "${value}" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
section, key, value = sys.argv[2:5]
lines = path.read_text(encoding="utf-8").splitlines(keepends=True)

current = None
changed = False
section_re = re.compile(r"^    ([A-Za-z0-9_]+):\s*(?:#.*)?(?:\r?\n)?$")
key_re = re.compile(rf"^(      {re.escape(key)}:\s*)([^#\r\n]*?)(\s*(?:#.*)?)?(\r?\n)?$")

for i, line in enumerate(lines):
    m = section_re.match(line)
    if m:
        current = m.group(1)
        continue
    if current == section:
        m = key_re.match(line)
        if m:
            prefix = m.group(1)
            suffix = m.group(3) or ""
            if suffix.startswith("#"):
                suffix = " " + suffix
            newline = m.group(4) or "\n"
            lines[i] = f"{prefix}{value}{suffix}{newline}"
            changed = True
            break

if not changed:
    raise SystemExit(f"ERROR: 找不到配置项 {section}.{key}, 文件: {path}")

path.write_text("".join(lines), encoding="utf-8")
print(f"已修改 {section}.{key} = {value}")
PY
}

set_arc_walk_file() {
  local file="$1"
  local value="$2"
  backup_all
  run_py "${file}" "${value}" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
value = sys.argv[2]
text = path.read_text(encoding="utf-8")

def rewrite_chase_line(line):
    if 'arc_walk="' in line:
        return re.sub(r'arc_walk="(?:true|false)"', f'arc_walk="{value}"', line, count=1)
    return line.rstrip(" />") + f' arc_walk="{value}" />'

lines = text.splitlines(keepends=True)
count = 0
for i, line in enumerate(lines):
    if '<Chase' in line and '<!--' not in line and '-->' not in line:
        newline = ''
        body = line
        if line.endswith('\r\n'):
            body, newline = line[:-2], '\r\n'
        elif line.endswith('\n'):
            body, newline = line[:-1], '\n'
        lines[i] = rewrite_chase_line(body) + newline
        count += 1
        break

if count != 1:
    raise SystemExit(f"ERROR: 没有找到唯一可修改的 <Chase ...>, 文件: {path}, 修改数量: {count}")

path.write_text("".join(lines), encoding="utf-8")
print(f"已修改 {path.name}: arc_walk={value}")
PY
}

set_arc_walk() {
  local target="$1"
  local value="$2"
  case "${target}" in
    striker)
      set_arc_walk_file "${STRIKER_XML}" "${value}"
      ;;
    goalie)
      set_arc_walk_file "${GOALIE_XML}" "${value}"
      ;;
    all)
      backup_all
      run_py "${STRIKER_XML}" "${GOALIE_XML}" "${value}" <<'PY'
import re
import sys
from pathlib import Path

paths = [Path(sys.argv[1]), Path(sys.argv[2])]
value = sys.argv[3]

for path in paths:
    text = path.read_text(encoding="utf-8")
    def rewrite_chase_line(line):
        if 'arc_walk="' in line:
            return re.sub(r'arc_walk="(?:true|false)"', f'arc_walk="{value}"', line, count=1)
        return line.rstrip(" />") + f' arc_walk="{value}" />'

    lines = text.splitlines(keepends=True)
    count = 0
    for i, line in enumerate(lines):
        if '<Chase' in line and '<!--' not in line and '-->' not in line:
            newline = ''
            body = line
            if line.endswith('\r\n'):
                body, newline = line[:-2], '\r\n'
            elif line.endswith('\n'):
                body, newline = line[:-1], '\n'
            lines[i] = rewrite_chase_line(body) + newline
            count += 1
            break

    if count != 1:
        raise SystemExit(f"ERROR: 没有找到唯一可修改的 <Chase ...>, 文件: {path}, 修改数量: {count}")
    path.write_text("".join(lines), encoding="utf-8")
    print(f"已修改 {path.name}: arc_walk={value}")
PY
      ;;
    *)
      echo "ERROR: 未知 arc_walk 目标: ${target}" >&2
      return 1
      ;;
  esac
}

restore_backup() {
  if [ ! -d "${BACKUP_DIR}" ]; then
    echo "没有备份目录: ${BACKUP_DIR}"
    return 0
  fi

  mapfile -t dirs < <(find "${BACKUP_DIR}" -maxdepth 1 -mindepth 1 -type d | sort -r)
  if [ "${#dirs[@]}" -eq 0 ]; then
    echo "没有找到备份。"
    return 0
  fi

  echo "可恢复的备份:"
  local i=1
  local dir
  for dir in "${dirs[@]}"; do
    echo "  ${i}) $(basename "${dir}")"
    i=$((i + 1))
  done
  read -r -p "输入要恢复的备份编号，直接回车取消: " choice
  [ -z "${choice}" ] && return 0
  if ! [[ "${choice}" =~ ^[0-9]+$ ]] || [ "${choice}" -lt 1 ] || [ "${choice}" -gt "${#dirs[@]}" ]; then
    echo "选择无效。"
    return 1
  fi

  local selected="${dirs[$((choice - 1))]}"
  cp "${selected}/config.yaml" "${CONFIG_FILE}"
  cp "${selected}/subtree_striker_play.xml" "${STRIKER_XML}"
  cp "${selected}/subtree_goal_keeper_play.xml" "${GOALIE_XML}"
  echo "已恢复备份: ${selected}"
}

ask_bool() {
  local prompt="$1"
  local value
  while true; do
    read -r -p "${prompt} [true/false]: " value
    case "${value}" in
      true|false) echo "${value}"; return 0 ;;
      t|T|yes|YES|y|Y|1|开|开启) echo "true"; return 0 ;;
      f|F|no|NO|n|N|0|关|关闭) echo "false"; return 0 ;;
      *) echo "请输入 true/false，或输入 开/关。" >&2 ;;
    esac
  done
}

ask_number() {
  local prompt="$1"
  local value
  while true; do
    read -r -p "${prompt}: " value
    if [[ "${value}" =~ ^-?[0-9]+([.][0-9]+)?$ ]]; then
      echo "${value}"
      return 0
    fi
    echo "请输入数字，例如 0、0.25、3.5。" >&2
  done
}

menu() {
  while true; do
    show_status
    cat <<'MENU'

测试开关菜单
1) 设置前锋 Chase 弧线追球 arc_walk
2) 设置守门员 Chase 弧线追球 arc_walk
3) 同时设置前锋和守门员 arc_walk
4) 设置 Adjust 绕球超时秒数，0 表示关闭
5) 设置球路预测总开关 enable
6) 设置球路预测是否接管追球 use_for_chase
7) 设置球路预测提前量 predict_time
8) 设置丢球预测保持时间 lost_prediction_timeout
9) 设置定位球/开球阶段是否禁用预测追球
b) 恢复备份
q) 退出
MENU
    read -r -p "请选择: " choice
    case "${choice}" in
      1)
        set_arc_walk striker "$(ask_bool "前锋 arc_walk，开=true，关=false")"
        ;;
      2)
        set_arc_walk goalie "$(ask_bool "守门员 arc_walk，开=true，关=false")"
        ;;
      3)
        set_arc_walk all "$(ask_bool "前锋和守门员 arc_walk，开=true，关=false")"
        ;;
      4)
        set_yaml_value strategy adjust_timeout_secs "$(ask_number "Adjust 绕球超时秒数，0 表示关闭")"
        ;;
      5)
        set_yaml_value ball_prediction enable "$(ask_bool "球路预测总开关 enable")"
        ;;
      6)
        set_yaml_value ball_prediction use_for_chase "$(ask_bool "球路预测是否接管追球 use_for_chase")"
        ;;
      7)
        set_yaml_value ball_prediction predict_time "$(ask_number "球路预测提前量 predict_time，单位秒")"
        ;;
      8)
        set_yaml_value ball_prediction lost_prediction_timeout "$(ask_number "丢球预测保持时间 lost_prediction_timeout，单位秒")"
        ;;
      9)
        set_yaml_value ball_prediction disable_for_set_play_chase "$(ask_bool "定位球/开球阶段禁用预测追球")"
        ;;
      b|B)
        restore_backup
        ;;
      q|Q)
        exit 0
        ;;
      *)
        echo "未知选项: ${choice}"
        ;;
    esac
  done
}

case "${1:-}" in
  --status)
    show_status
    ;;
  --backup)
    backup_all
    ;;
  --restore)
    restore_backup
    ;;
  "")
    menu
    ;;
  *)
    echo "用法: $0 [--status|--backup|--restore]"
    exit 2
    ;;
esac
