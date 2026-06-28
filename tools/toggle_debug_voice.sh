#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

GAME_XML="${ROOT_DIR}/src/brain/behavior_trees/game.xml"
STRIKER_XML="${ROOT_DIR}/src/brain/behavior_trees/subtrees/subtree_striker_play.xml"
GOALIE_XML="${ROOT_DIR}/src/brain/behavior_trees/subtrees/subtree_goal_keeper_play.xml"
BACKUP_DIR="${ROOT_DIR}/tools/debug-voice-backups"

if command -v python3 >/dev/null 2>&1; then
  PYTHON_BIN="python3"
elif command -v python >/dev/null 2>&1; then
  PYTHON_BIN="python"
else
  echo "ERROR: 找不到 python3/python，无法安全修改 XML。" >&2
  exit 1
fi

backup_all() {
  mkdir -p "${BACKUP_DIR}"
  local stamp
  stamp="$(date +%Y%m%d_%H%M%S)"
  local dir="${BACKUP_DIR}/${stamp}"
  mkdir -p "${dir}"
  cp "${GAME_XML}" "${dir}/game.xml"
  cp "${STRIKER_XML}" "${dir}/subtree_striker_play.xml"
  cp "${GOALIE_XML}" "${dir}/subtree_goal_keeper_play.xml"
  echo "已备份比赛 XML 到: ${dir}"
}

status_voice() {
  "${PYTHON_BIN}" - "${GAME_XML}" "${STRIKER_XML}" "${GOALIE_XML}" <<'PY'
import sys
from pathlib import Path

for raw in sys.argv[1:]:
    path = Path(raw)
    text = path.read_text(encoding='utf-8')
    count = text.count('DEBUG_VOICE_START')
    print(f'{path.name}: 调试语音块 {count} 个')
PY
}

remove_voice() {
  backup_all
  "${PYTHON_BIN}" - "${GAME_XML}" "${STRIKER_XML}" "${GOALIE_XML}" <<'PY'
import re
import sys
from pathlib import Path

pattern = re.compile(r'\n?\s*<!-- DEBUG_VOICE_START -->\n\s*<Speak\b[^>]*\/>\n\s*<!-- DEBUG_VOICE_END -->', re.MULTILINE)

for raw in sys.argv[1:]:
    path = Path(raw)
    text = path.read_text(encoding='utf-8')
    new_text, count = pattern.subn('', text)
    path.write_text(new_text, encoding='utf-8')
    print(f'{path.name}: 已删除 {count} 个调试语音块')
PY
}

apply_voice() {
  backup_all
  "${PYTHON_BIN}" - "${GAME_XML}" "${STRIKER_XML}" "${GOALIE_XML}" <<'PY'
import sys
from pathlib import Path

game_xml = Path(sys.argv[1])
striker_xml = Path(sys.argv[2])
goalie_xml = Path(sys.argv[3])

def block(indent: str, text: str, attrs: str = '') -> str:
    extra = f' {attrs}' if attrs else ''
    return (
        f'{indent}<!-- DEBUG_VOICE_START -->\n'
        f'{indent}<Speak text="{text}"{extra} />\n'
        f'{indent}<!-- DEBUG_VOICE_END -->\n'
    )

def insert_before(text: str, needle: str, insert: str) -> str:
    if insert.strip() in text:
        return text
    if needle not in text:
        raise SystemExit(f'找不到插入位置: {needle}')
    return text.replace(needle, insert + needle, 1)

def apply_game(path: Path) -> None:
    text = path.read_text(encoding='utf-8')
    text = insert_before(text, '                    <SetVelocity />', block('                    ', '罚时停止'))
    text = insert_before(text, '                        <SetVelocity />', block('                        ', '比赛暂停'))
    text = insert_before(text, '                            <MoveHead pitch="0.35" yaw="0.0" />', block('                            ', '准备入场'))
    text = insert_before(text, '                            <SubTree ID="CamFindAndTrackBall" _autoremap="true" />', block('                            ', '站好等待'))
    text = insert_before(text, '                            <SubTree ID="StrikerPlay" _autoremap="true" _while="player_role == \'striker\'" />', block('                            ', 'lets fucking go'))
    path.write_text(text, encoding='utf-8')

def apply_striker(path: Path) -> None:
    text = path.read_text(encoding='utf-8')
    text = insert_before(text, '            <SubTree ID="CamFindAndTrackBall" _autoremap="true" />', block('            ', '等待对方开球'))
    text = insert_before(text, '               <SubTree ID="CamFindAndTrackBall" _autoremap="true" />', block('               ', '球出界回场'))
    text = insert_before(text, '                        <SubTree ID="FindBall" _while="decision==\'find\'" _autoremap="true" />', block('                        ', '找球', '_while="decision==\'find\'"'))
    text = insert_before(text, '                        <Kick _while="decision == \'kick\'" speed_limit="0.9" min_msec_kick="1000" msecs_stablize="1000"/>', block('                        ', 'lets gooooo', '_while="decision == \'kick\'"'))
    path.write_text(text, encoding='utf-8')

def apply_goalie(path: Path) -> None:
    text = path.read_text(encoding='utf-8')
    text = insert_before(text, '            <SubTree ID="CamFindAndTrackBall" _autoremap="true" />', block('            ', '等待对方开球'))
    text = insert_before(text, '               <SubTree ID="CamFindAndTrackBall" _autoremap="true" />', block('               ', '球出界回场'))
    text = insert_before(text, '                     <GoToGoalBlockingPosition _while="decision==\'find\'" theta_tolerance="0.2" />', block('                     ', '守门员找球', '_while="decision==\'find\'"'))
    text = insert_before(text, '                        <!-- <SetVelocity />', block('                        ', 'lets gooooo'))
    path.write_text(text, encoding='utf-8')

apply_game(game_xml)
apply_striker(striker_xml)
apply_goalie(goalie_xml)
print('已加入调试语音。')
PY
}

case "${1:-status}" in
  status)
    status_voice
    ;;
  apply|add|on)
    apply_voice
    ;;
  remove|delete|off)
    remove_voice
    ;;
  *)
    echo "用法: bash tools/toggle_debug_voice.sh [status|apply|remove]"
    exit 2
    ;;
esac
