#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Interactive shortcut for common K1 demo network/player settings."""

from __future__ import annotations

import re
import shutil
import sys
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BRAIN_CONFIG = ROOT / "src" / "brain" / "config" / "config.yaml"
WEBUI_CONFIG = ROOT / "src" / "k1_robot_webui_client" / "config" / "webui_client.yaml"
GC_LAUNCH = ROOT / "src" / "game_controller" / "launch" / "launch.py"


def print_usage() -> None:
    print("Usage: python configure_demo.py")
    print()
    print("Interactive shortcut for editing:")
    print("  src/brain/config/config.yaml: team_id, player_id, number_of_players, game_control_ip")
    print("  src/k1_robot_webui_client/config/webui_client.yaml: server_base_url")
    print("  src/game_controller/launch/launch.py: ip_white_list")


def strip_quotes(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {'"', "'"}:
        return value[1:-1]
    return value


def normalize_server_url(value: str) -> str:
    value = value.strip()
    if not value:
        return value
    if value.startswith("http://") or value.startswith("https://"):
        return value.rstrip("/")
    return f"http://{value}:8000"


def read_file(path: Path) -> str:
    if not path.exists():
        raise FileNotFoundError(f"Missing file: {path}")
    return path.read_text(encoding="utf-8")


def find_yaml_value(text: str, key: str, default: str = "") -> str:
    match = re.search(rf"^\s*{re.escape(key)}:\s*([^#\n]+)", text, re.MULTILINE)
    return strip_quotes(match.group(1)) if match else default


def find_server_url(text: str) -> str:
    return find_yaml_value(text, "server_base_url", "")


def find_launch_ip(text: str, default: str = "") -> str:
    match = re.search(r'"ip_white_list"\s*:\s*\[\s*\n\s*"([^"]+)"', text)
    return match.group(1) if match else default


def ask_value(prompt: str, default: str) -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"{prompt}{suffix}: ").strip()
    return value or default


def ask_int(prompt: str, default: str, minimum: int | None = None, maximum: int | None = None) -> str:
    while True:
        value = ask_value(prompt, default)
        try:
            parsed = int(value)
        except ValueError:
            print("Please enter an integer.")
            continue
        if minimum is not None and parsed < minimum:
            print(f"Value must be >= {minimum}.")
            continue
        if maximum is not None and parsed > maximum:
            print(f"Value must be <= {maximum}.")
            continue
        return str(parsed)


def replace_yaml_scalar(text: str, key: str, value: str, quoted: bool = False) -> str:
    formatted = f'"{value}"' if quoted else value
    pattern = re.compile(rf"^(\s*{re.escape(key)}:\s*)([^#\n]*)(\s*#.*)?$", re.MULTILINE)

    def repl(match: re.Match[str]) -> str:
        comment = match.group(3) or ""
        return f"{match.group(1)}{formatted}{comment}"

    new_text, count = pattern.subn(repl, text, count=1)
    if count != 1:
        raise ValueError(f"Could not find YAML key: {key}")
    return new_text


def replace_launch_whitelist_ip(text: str, ip: str) -> str:
    pattern = re.compile(r'("ip_white_list"\s*:\s*\[\s*\n)(\s*)"[^"]*"(,?\s*\n\s*\])')

    def repl(match: re.Match[str]) -> str:
        return f'{match.group(1)}{match.group(2)}"{ip}"{match.group(3)}'

    new_text, count = pattern.subn(repl, text, count=1)
    if count != 1:
        raise ValueError("Could not find launch.py ip_white_list entry")
    return new_text


def backup(path: Path, stamp: str) -> None:
    backup_path = path.with_name(f"{path.name}.bak.{stamp}")
    shutil.copy2(path, backup_path)
    print(f"backup: {path.relative_to(ROOT)} -> {backup_path.relative_to(ROOT)}")


def write_if_changed(path: Path, old_text: str, new_text: str, stamp: str) -> None:
    if old_text == new_text:
        print(f"unchanged: {path.relative_to(ROOT)}")
        return
    backup(path, stamp)
    path.write_text(new_text, encoding="utf-8", newline="")
    print(f"updated: {path.relative_to(ROOT)}")


def main() -> int:
    if any(arg in {"-h", "--help"} for arg in sys.argv[1:]):
        print_usage()
        return 0

    brain_text = read_file(BRAIN_CONFIG)
    webui_text = read_file(WEBUI_CONFIG)
    launch_text = read_file(GC_LAUNCH)

    current_team_id = find_yaml_value(brain_text, "team_id", "66")
    current_player_id = find_yaml_value(brain_text, "player_id", "1")
    current_players = find_yaml_value(brain_text, "number_of_players", "5")
    current_gc_ip = find_yaml_value(brain_text, "game_control_ip", find_launch_ip(launch_text, "192.168.74.2"))
    current_server = find_server_url(webui_text)

    print("K1 demo quick config")
    print(f"workspace: {ROOT}")
    print()

    team_id = ask_int("team_id", current_team_id, minimum=0)
    player_id = ask_int("player_id", current_player_id, minimum=1, maximum=5)
    number_of_players = ask_int("number_of_players", current_players, minimum=1, maximum=5)
    game_control_ip = ask_value("GameController IP", current_gc_ip)
    server_base_url = normalize_server_url(ask_value("Windows WebUI backend IP or URL", current_server))

    print()
    print("Will apply:")
    print(f"  team_id: {team_id}")
    print(f"  player_id: {player_id}")
    print(f"  number_of_players: {number_of_players}")
    print(f"  game_control_ip / ip_white_list: {game_control_ip}")
    print(f"  server_base_url: {server_base_url}")
    confirm = input("Continue [Y/n]: ").strip().lower()
    if confirm in {"n", "no"}:
        print("cancelled")
        return 0

    new_brain = brain_text
    new_brain = replace_yaml_scalar(new_brain, "team_id", team_id)
    new_brain = replace_yaml_scalar(new_brain, "player_id", player_id)
    new_brain = replace_yaml_scalar(new_brain, "number_of_players", number_of_players)
    new_brain = replace_yaml_scalar(new_brain, "game_control_ip", game_control_ip, quoted=True)

    new_webui = replace_yaml_scalar(webui_text, "server_base_url", server_base_url, quoted=True)
    new_launch = replace_launch_whitelist_ip(launch_text, game_control_ip)

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    write_if_changed(BRAIN_CONFIG, brain_text, new_brain, stamp)
    write_if_changed(WEBUI_CONFIG, webui_text, new_webui, stamp)
    write_if_changed(GC_LAUNCH, launch_text, new_launch, stamp)

    print()
    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
