#!/bin/bash

cd `dirname $0`
cd ..
WORKSPACE_ROOT=$(pwd)
VISION_CONFIG_PATH="${WORKSPACE_ROOT}/src/vision/config"

echo "[STOP EXISTING NODES (IF ANY), TO AVOID CONFILICT]"
sudo killall -9 booster-video-stream
./scripts/stop.sh
sudo jetson_clocks
sudo systemctl mask apt-daily.timer apt-daily-upgrade.timer
sudo systemctl mask unattended-upgrades.service
sudo rm -f /var/lib/systemd/timers/stamp-apt-daily.timer
sudo pkill -9 update_manager
sudo pkill -9 python3
systemctl --user disable robocup_game_assist.service

echo "[START ROBOCUP NODES]"
source ./install/setup.bash
export FASTRTPS_DEFAULT_PROFILES_FILE=/opt/booster/BoosterRos2/fastdds_profile_udp_only.xml
# export RMW_FASTRTPS_USE_SHM=0

echo "[START VISION]"
nohup ros2 launch vision launch.py vision_config_path:=/opt/booster save_data:=true > vision.log 2>&1 &
echo "[START BRAIN]"
nohup ros2 launch brain launch.py vision_config_path:=/opt/booster tree:=test role:=striker disable_com:=true "$@" > brain.log 2>&1 &
#echo "[START SOUND]"
#nohup ros2 run sound_play sound_play_node > sound.log 2>&1 &
echo "[DONE]"
sudo jetson_clocks
