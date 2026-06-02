#include "booster_vision/vision_node.h"

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <fstream>

#include <yaml-cpp/yaml.h>
#include "ament_index_cpp/get_package_share_directory.hpp"

#include "vision_interface/msg/detected_object.hpp"
#include "vision_interface/msg/detections.hpp"
#include "vision_interface/msg/cal_param.hpp"

#include "booster_vision/base/data_syncer.hpp"
#include "booster_vision/base/data_logger.hpp"
#include "booster_vision/base/misc_utils.hpp"
#include "booster_vision/model//detector.h"
#include "booster_vision/model//segmentor.h"
#include "booster_vision/pose_estimator/pose_estimator.h"
#include "booster_vision/img_bridge.h"

namespace booster_vision {

VisionNode::VisionNode(const std::string &node_name) :
    rclcpp::Node(node_name) {
    this->declare_parameter<bool>("offline_mode", false);
    this->declare_parameter<bool>("show_det", false);
    this->declare_parameter<bool>("show_seg", false);
    this->declare_parameter<bool>("save_data", true);
    this->declare_parameter<bool>("save_depth", true);
    this->declare_parameter<int>("save_fps", 3);
    this->declare_parameter<std::string>("detection_model_path", "");
    this->declare_parameter<std::string>("segmentation_model_path", "");
    this->declare_parameter<std::string>("camera_type", "");
}

// TODO(GW): oneline offline
void VisionNode::Init(const std::string &cfg_template_path, const std::string &cfg_path) {
    if (!std::filesystem::exists(cfg_template_path)) {
        // TODO(SS): throw exception here
        std::cerr << "错误：配置模板文件 '" << cfg_template_path << "' 不存在。" << std::endl;
        return;
    }

    YAML::Node node = YAML::LoadFile(cfg_template_path);
    if (!std::filesystem::exists(cfg_path)) {
        std::cout << "警告：配置文件为空或不存在，将使用模板默认配置。" << std::endl;
    } else {
        YAML::Node cfg_node = YAML::LoadFile(cfg_path);
        // merge input cfg to template cfg
        MergeYAML(node, cfg_node);
    }

    std::cout << "已加载配置：" << std::endl
              << node << std::endl;

    this->get_parameter<bool>("show_det", show_det_);
    this->get_parameter<bool>("show_seg", show_seg_);
    this->get_parameter<bool>("save_data", save_data_);
    this->get_parameter<bool>("save_depth", save_depth_);
    this->get_parameter<bool>("offline_mode", offline_mode_);
    this->get_parameter<std::string>("camera_type", camera_type_);
    this->get_parameter<std::string>("detection_model_path", detection_model_path);
    std::cout << "检测模型路径原始值：" << detection_model_path << std::endl;

    if(!detection_model_path.empty()){
        if(detection_model_path[0] == '/') {
            // absolute path, do nothing
        } else {
            std::string package_path = ament_index_cpp::get_package_share_directory("vision");
            detection_model_path = std::filesystem::path(package_path) / detection_model_path;
        }
    }


    this->get_parameter<std::string>("segmentation_model_path", segmentation_model_path);
    std::cout << "分割模型路径原始值：" << segmentation_model_path << std::endl;

    if(!segmentation_model_path.empty()){
        if(segmentation_model_path[0] == '/') {
            // absolute path, do nothing
        } else {
            std::string package_path = ament_index_cpp::get_package_share_directory("vision");
            segmentation_model_path = std::filesystem::path(package_path) / segmentation_model_path;
        }
    }
    
    int save_fps = 0;
    this->get_parameter<int>("save_fps", save_fps);
    save_depth_ = save_depth_ && save_data_;
    std::cout << "离线模式：" << offline_mode_ << std::endl;
    std::cout << "显示检测结果：" << show_det_ << std::endl;
    std::cout << "显示分割结果：" << show_seg_ << std::endl;
    std::cout << "保存数据：" << save_data_ << std::endl;
    std::cout << "保存深度图：" << save_depth_ << std::endl;
    std::cout << "数据保存帧率：" << save_fps << std::endl;
    std::cout << "相机类型：" << camera_type_ << std::endl;
    std::cout << "检测模型路径：" << detection_model_path << std::endl;
    std::cout << "分割模型路径：" << segmentation_model_path << std::endl;
    save_every_n_frame_ = std::max(1, save_fps > 0 ? 30 / save_fps : 1);
    std::cout << "每隔多少帧保存一次：" << save_every_n_frame_ << std::endl;

    // read camera param
    if (!node["camera"]) {
        // TODO(SS): throw exception here
        std::cerr << "未找到相机参数配置。" << std::endl;
        return;
    } else {
        if(camera_type_.empty())
        {
            std::cout << "launch 文件未覆盖相机类型，使用默认配置：" << node["camera"]["type"].as<std::string>() << std::endl;
            camera_type_ = node["camera"]["type"].as<std::string>();
        }
        intr_ = Intrinsics(node["camera"]["intrin"]);
        p_eye2head_ = as_or<Pose>(node["camera"]["extrin"], Pose());

        float pitch_comp = as_or<float>(node["camera"]["pitch_compensation"], 0.0);
        float yaw_comp = as_or<float>(node["camera"]["yaw_compensation"], 0.0);
        float z_comp = as_or<float>(node["camera"]["z_compensation"], 0.0);

        p_headprime2head_ = Pose(0, 0, z_comp, 0, pitch_comp * M_PI / 180, yaw_comp * M_PI / 180);
    }

    // init detector
    if (!node["detection_model"]) {
        std::cerr << "未找到检测模型参数配置。" << std::endl;
        return;
    } else {
        detector_ = YoloV8Detector::CreateYoloV8Detector(node["detection_model"], detection_model_path);
        classnames_ = node["detection_model"]["classnames"].as<std::vector<std::string>>();
        // detector post processing
        float default_threshold = as_or<float>(node["detection_model"]["confidence_threshold"], 0.2);
        if (node["detection_model"]["post_process"]) {
            enable_post_process_ = true;
            single_ball_assumption_ = as_or<bool>(node["detection_model"]["post_process"]["single_ball_assumption"], false);
            if (node["detection_model"]["post_process"]["confidence_thresholds"]) {
                for (const auto &item : node["detection_model"]["post_process"]["confidence_thresholds"]) {
                    confidence_map_[item.first.as<std::string>()] = item.second.as<float>();
                }
                // set default confidence for other classes
                for (const auto &classname : classnames_) {
                    if (confidence_map_.find(classname) == confidence_map_.end()) {
                        confidence_map_[classname] = default_threshold;
                    }
                }
            } else {
                std::cout << "所有类别使用相同的默认置信度阈值：" << default_threshold << std::endl;
            }
        }
    }

    if (!node["segmentation_model"]) {
        std::cerr << "未找到分割模型参数配置。" << std::endl;
    } else {
        segmentor_ = YoloV8Segmentor::CreateYoloV8Segmentor(node["segmentation_model"], segmentation_model_path);
    }

    // add detector_ warmup

    // init data_syncer
    use_depth_ = as_or<bool>(node["use_depth"], false);
    if (node["ground_plane"]) {
        const auto &ground_node = node["ground_plane"];
        ground_plane_config_.enable = as_or<bool>(ground_node["enable"], false);
        ground_plane_config_.update_every_n_frames = std::max(1, as_or<int>(ground_node["update_every_n_frames"], 5));
        ground_plane_config_.sample_step = std::max(1, as_or<int>(ground_node["sample_step"], 8));
        ground_plane_config_.min_depth = as_or<float>(ground_node["min_depth"], 0.2f);
        ground_plane_config_.max_depth = as_or<float>(ground_node["max_depth"], 6.0f);
        ground_plane_config_.ransac_distance_threshold = as_or<float>(ground_node["ransac_distance_threshold"], 0.02f);
        ground_plane_config_.min_inlier_ratio = as_or<float>(ground_node["min_inlier_ratio"], 0.35f);
        ground_plane_config_.max_normal_tilt_deg = as_or<float>(ground_node["max_normal_tilt_deg"], 25.0f);
        ground_plane_config_.force_update_head_pitch_delta_deg = as_or<float>(ground_node["force_update_head_pitch_delta_deg"], 3.0f);
        ground_plane_config_.force_update_head_yaw_delta_deg = as_or<float>(ground_node["force_update_head_yaw_delta_deg"], 5.0f);
        ground_plane_config_.min_ground_height = as_or<float>(ground_node["min_ground_height"], -0.20f);
        ground_plane_config_.max_ground_height = as_or<float>(ground_node["max_ground_height"], 0.25f);
    }
    ground_plane_config_.enable = ground_plane_config_.enable && use_depth_;
    std::cout << "地面平面估计开关：" << ground_plane_config_.enable << std::endl;
    std::cout << "地面平面每隔多少帧更新一次：" << ground_plane_config_.update_every_n_frames << std::endl;

    auto ball_motion_config = ball_motion_predictor_.config();
    if (node["ball_motion_prediction"]) {
        const auto &motion_node = node["ball_motion_prediction"];
        ball_motion_config.enable = as_or<bool>(motion_node["enable"], false);
        ball_motion_config.predict_time = std::max(0.0, as_or<double>(motion_node["predict_time"], 0.25));
        ball_motion_config.min_dt = std::max(1e-3, as_or<double>(motion_node["min_dt"], 0.02));
        ball_motion_config.max_dt = std::max(ball_motion_config.min_dt, as_or<double>(motion_node["max_dt"], 0.20));
        ball_motion_config.max_history_gap = std::max(ball_motion_config.max_dt, as_or<double>(motion_node["max_history_gap"], 0.50));
        ball_motion_config.max_speed = std::max(0.0, as_or<double>(motion_node["max_speed"], 4.0));
        ball_motion_config.max_acceleration = std::max(0.0, as_or<double>(motion_node["max_acceleration"], 0.0));
        ball_motion_config.allow_projection = as_or<bool>(motion_node["allow_projection"], false);
    }
    ball_motion_predictor_.setConfig(ball_motion_config);
    std::cout << "球运动预测开关：" << ball_motion_config.enable
              << "，预测时间：" << ball_motion_config.predict_time << "s"
              << "，允许投影预测：" << ball_motion_config.allow_projection << std::endl;

    data_syncer_ = std::make_shared<DataSyncer>(use_depth_);
    bool save_data_nonstationary = as_or<bool>(node["misc"]["save_data_nonstationary"], true);
    std::string log_root = std::string(std::getenv("HOME")) + "/Workspace/vision_log/" + getTimeString();
    data_logger_ = save_data_ ? std::make_shared<DataLogger>(log_root, save_data_nonstationary) : nullptr;
    if (data_logger_) {
        data_logger_->LogYAML(node, "vision_local.yaml");
    }
    seg_data_syncer_ = std::make_shared<DataSyncer>(false);

    // init robot color classifier
    if (node["robot_color_classifier"]) {
        color_classifier_ = std::make_shared<ColorClassifier>();
        color_classifier_->Init(node["robot_color_classifier"]);
    }

    // init pose estimator
    pose_estimator_ = std::make_shared<PoseEstimator>(intr_);
    pose_estimator_->Init(YAML::Node());
    pose_estimator_map_["default"] = pose_estimator_;

    if (node["ball_pose_estimator"]) {
        pose_estimator_map_["ball"] = std::make_shared<BallPoseEstimator>(intr_);
        pose_estimator_map_["ball"]->Init(node["ball_pose_estimator"]);
    }

    if (node["human_like_pose_estimator"]) {
        pose_estimator_map_["human_like"] = std::make_shared<HumanLikePoseEstimator>(intr_);
        pose_estimator_map_["human_like"]->Init(node["human_like_pose_estimator"]);
    }

    if (node["field_marker_pose_estimator"]) {
        pose_estimator_map_["field_marker"] = std::make_shared<FieldMarkerPoseEstimator>(intr_);
        pose_estimator_map_["field_marker"]->Init(node["field_marker_pose_estimator"]);

        line_segment_area_threshold_ = as_or<int>(node["field_marker_pose_estimator"]["line_segment_area_threshold"], 75);
    }

    // init ros related

    std::cout << "当前相机类型：" << camera_type_ << std::endl;
    std::string color_topic;
    std::string depth_topic;
    if (camera_type_.find("zed") != std::string::npos) {
        color_topic = "/zed/zed_node/left/image_rect_color";
        depth_topic = "/zed/zed_node/depth/depth_registered";
    } else if (camera_type_ == "d-robotics") {
        color_topic = "/image_left_raw";
        depth_topic = "/image_left_raw/camera_info";
    } else if (camera_type_ == "orbbec") {
        color_topic = "/camera/color/image_raw";
        depth_topic = "/camera/depth/image_raw";
    } else {
        // realsense
        color_topic = "/camera/camera/color/image_raw";
        depth_topic = "/camera/camera/aligned_depth_to_color/image_raw";
    }

    callback_group_sub_1_ = this->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    callback_group_sub_2_ = this->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    callback_group_sub_3_ = this->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    callback_group_sub_4_ = this->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    auto sub_opt_1 = rclcpp::SubscriptionOptions();
    sub_opt_1.callback_group = callback_group_sub_1_;
    auto sub_opt_2 = rclcpp::SubscriptionOptions();
    sub_opt_2.callback_group = callback_group_sub_2_;
    auto sub_opt_3 = rclcpp::SubscriptionOptions();
    sub_opt_3.callback_group = callback_group_sub_3_;
    auto sub_opt_4 = rclcpp::SubscriptionOptions();
    sub_opt_4.callback_group = callback_group_sub_4_;

    it_ = std::make_shared<image_transport::ImageTransport>(shared_from_this());
    image_transport::TransportHints hints(this, "compressed");
    // Subscribe to both raw and compressed image topics for color
    if (camera_type_.find("compressed") != std::string::npos) {
        color_sub_ = it_->subscribe(color_topic, 1, &VisionNode::ColorCallback, this, &hints, sub_opt_1);
    } else {
        color_sub_ = it_->subscribe(color_topic, 1, &VisionNode::ColorCallback, this, nullptr, sub_opt_1);
    } 
    if (use_depth_) {
        depth_sub_ = it_->subscribe(depth_topic, 1, &VisionNode::DepthCallback, this, nullptr, sub_opt_3);
    }
    if (camera_type_.find("compressed") != std::string::npos) {
        color_sub_ = it_->subscribe(color_topic, 2, &VisionNode::ColorCallback, this, &hints, sub_opt_1);
    } else {
        color_sub_ = it_->subscribe(color_topic, 2, &VisionNode::ColorCallback, this, nullptr, sub_opt_1);
    } 
    if (use_depth_) {
        depth_sub_ = it_->subscribe(depth_topic, 2, &VisionNode::DepthCallback, this, nullptr, sub_opt_3);
    }

    // auto qos_profile = rclcpp::QoS(rclcpp::KeepLast(1))
    // auto qos_profile = rclcpp::QoS(rclcpp::KeepLast(1))
    //     .reliability(rclcpp::ReliabilityPolicy::BestEffort)  // Use best effort for real-time performance
    //     .durability(rclcpp::DurabilityPolicy::Volatile);

    // color_sub_ = this->create_subscription<sensor_msgs::msg::Image>(
    //     color_topic,
    //     qos_profile,
    //     std::bind(&VisionNode::ColorCallback, this, std::placeholders::_1),
    //     sub_opt_1);

    // depth_sub_ = this->create_subscription<sensor_msgs::msg::Image>(
    //     depth_topic,
    //     qos_profile,
    //     std::bind(&VisionNode::DepthCallback, this, std::placeholders::_1),
    //     sub_opt_3);

    detection_pub_ = this->create_publisher<vision_interface::msg::Detections>("/booster_vision/detection", rclcpp::QoS(1));

    if (node["segmentation_model"]) {
        std::cout << "为分割模型创建图像订阅。" << std::endl;
        if (camera_type_.find("compressed") != std::string::npos) {
            color_seg_sub_ = it_->subscribe(color_topic, 1, &VisionNode::SegmentationCallback, this, &hints, sub_opt_2);
        } else {
            color_seg_sub_ = it_->subscribe(color_topic, 1, &VisionNode::SegmentationCallback, this, nullptr, sub_opt_2);
        } 
        field_line_pub_ = this->create_publisher<vision_interface::msg::LineSegments>("/booster_vision/line_segments", rclcpp::QoS(1));
    }
    ball_pub_ = this->create_publisher<vision_interface::msg::Ball>("/booster_vision/ball", rclcpp::QoS(1));

    if (offline_mode_) {
        pose_tf_sub_ = this->create_subscription<geometry_msgs::msg::TransformStamped>("/booster_vision/t_head2base", 10, std::bind(&VisionNode::PoseTFCallBack, this, std::placeholders::_1));
    } else {
        pose_sub_ = this->create_subscription<geometry_msgs::msg::Pose>("/head_pose", 10, std::bind(&VisionNode::PoseCallBack, this, std::placeholders::_1), sub_opt_4);
        calParam_sub_ = this->create_subscription<vision_interface::msg::CalParam>("/booster_vision/cal_param", 10, std::bind(&VisionNode::CalParamCallback, this, std::placeholders::_1));
        pose_tf_pub_ = this->create_publisher<geometry_msgs::msg::TransformStamped>("/booster_vision/t_head2base", rclcpp::QoS(10));
    }
}

void VisionNode::ProcessData(SyncedDataBlock &synced_data, vision_interface::msg::Detections &detection_msg) {
    double timestamp = synced_data.color_data.timestamp;
    double depth_time_diff = (timestamp - synced_data.depth_data.timestamp) * 1000;
    double pose_time_diff = (timestamp - synced_data.pose_data.timestamp) * 1000;
    if (use_depth_ && depth_time_diff > 40) {
        std::cerr << "彩色图与深度图时间差：" << depth_time_diff << "ms" << std::endl;
    }
    if (pose_time_diff > 40) {
        std::cerr << "彩色图与头部姿态时间差：" << pose_time_diff << " ms" << std::endl;
    }
    cv::Mat color = synced_data.color_data.data;
    cv::Mat depth = synced_data.depth_data.data;

    cv::Mat depth_float;
    if (!depth.empty() && depth.depth() == CV_16U) {
        depth.convertTo(depth_float, CV_32F, 0.001, 0);
    } else {
        depth_float = depth;
    }

    Pose p_head2base = synced_data.pose_data.data;
    Pose p_eye2base = p_head2base * p_headprime2head_ * p_eye2head_;
    std::cout << "检测用相机到机器人基座位姿 p_eye2base：\n"
              << p_eye2base.toCVMat() << std::endl;

    bool ground_plane_available = false;
    if (ground_plane_config_.enable) {
        ground_plane_frame_count_++;
        bool should_fit_plane = !ground_plane_cache_.valid ||
                                (ground_plane_frame_count_ % ground_plane_config_.update_every_n_frames == 0);

        if (ground_plane_cache_.valid) {
            auto cached_rpy = ground_plane_cache_.last_p_eye2base.getEulerAnglesVec();
            auto current_rpy = p_eye2base.getEulerAnglesVec();
            auto angle_delta_deg = [](float lhs, float rhs) {
                return std::fabs(std::atan2(std::sin(lhs - rhs), std::cos(lhs - rhs))) * 180.0f / CV_PI;
            };
            float pitch_delta = angle_delta_deg(current_rpy[1], cached_rpy[1]);
            float yaw_delta = angle_delta_deg(current_rpy[2], cached_rpy[2]);
            if (pitch_delta > ground_plane_config_.force_update_head_pitch_delta_deg ||
                yaw_delta > ground_plane_config_.force_update_head_yaw_delta_deg) {
                should_fit_plane = true;
                std::cout << "[地面平面] 头部运动变化较大，强制更新。pitch_delta=" << pitch_delta
                          << "，yaw_delta=" << yaw_delta << std::endl;
            }
        }

        if (should_fit_plane) {
            ground_plane_available = FitGroundPlaneFromDepth(ground_plane_cache_, depth_float, color, intr_,
                                                             p_eye2base, timestamp, ground_plane_config_);
            if (ground_plane_available) {
                std::cout << "[地面平面] 已更新 plane_base=("
                          << ground_plane_cache_.plane_base[0] << ", "
                          << ground_plane_cache_.plane_base[1] << ", "
                          << ground_plane_cache_.plane_base[2] << ", "
                          << ground_plane_cache_.plane_base[3] << ")，内点比例="
                          << ground_plane_cache_.inlier_ratio << std::endl;
            } else {
                std::cout << "[地面平面] 拟合失败：" << ground_plane_cache_.last_failure_reason << std::endl;
                if (ground_plane_cache_.valid) {
                    ground_plane_available = PrecomputePlaneTransform(ground_plane_cache_, p_eye2base);
                    if (ground_plane_available) {
                        std::cout << "[地面平面] 拟合失败后复用上一帧有效平面。" << std::endl;
                    }
                }
            }
        } else {
            ground_plane_available = PrecomputePlaneTransform(ground_plane_cache_, p_eye2base);
            if (!ground_plane_available) {
                std::cout << "[地面平面] 缓存预计算失败："
                          << ground_plane_cache_.last_failure_reason << std::endl;
            }
        }
    }

    // inference
    auto detections = detector_->Inference(color);
    std::cout << "检测到 " << detections.size() << " 个目标。" << std::endl;

    auto get_estimator = [&](const std::string &class_name) {
        if (class_name == "Ball") {
            return pose_estimator_map_.find("ball") != pose_estimator_map_.end() ? pose_estimator_map_["ball"] : pose_estimator_map_["default"];
        } else if (class_name == "Person" || class_name == "Opponent" || class_name == "Goalpost") {
            return pose_estimator_map_.find("human_like") != pose_estimator_map_.end() ? pose_estimator_map_["human_like"] : pose_estimator_map_["default"];
        } else if (class_name.find("Cross") != std::string::npos || class_name == "PenaltyPoint") {
            return pose_estimator_map_.find("field_marker") != pose_estimator_map_.end() ? pose_estimator_map_["field_marker"] : pose_estimator_map_["default"];
        } else {
            return pose_estimator_map_["default"];
        }
    };

    std::vector<booster_vision::DetectionRes> filtered_detections;
    if (enable_post_process_ && !detections.empty()) {
        // filter detections with different confidence
        if (!confidence_map_.empty()) {
            for (auto &detection : detections) {
                auto classname = classnames_[detection.class_id];
                if (detection.confidence < confidence_map_[classname]) {
                    continue;
                }
                filtered_detections.push_back(detection);
            }
        } else {
            filtered_detections = detections;
        }

        // keep the highest ball detections
        if (single_ball_assumption_) {
            std::vector<booster_vision::DetectionRes> ball_detections;
            std::vector<booster_vision::DetectionRes> filtered_detections_bk = filtered_detections;
            filtered_detections.clear();

            for (const auto &detection : filtered_detections_bk) {
                if (classnames_[detection.class_id] == "Ball") {
                    ball_detections.push_back(detection);
                } else {
                    filtered_detections.push_back(detection);
                }
            }

            if (ball_detections.size() > 1) {
                std::cout << "检测到多个球，仅保留置信度最高的一个。" << std::endl;
                auto max_ball_detection = *std::max_element(ball_detections.begin(), ball_detections.end(),
                                                            [](const booster_vision::DetectionRes &a, const booster_vision::DetectionRes &b) {
                                                                return a.confidence < b.confidence;
                                                            });
                filtered_detections.push_back(max_ball_detection);
            } else {
                filtered_detections.insert(filtered_detections.end(), ball_detections.begin(), ball_detections.end());
            }
        }
    } else {
        filtered_detections = detections;
    }

    auto get_ground_target_uv = [](const booster_vision::DetectionRes &detection) {
        const auto &bbox = detection.bbox;
        if (detection.class_name == "Ball" ||
            detection.class_name == "Person" ||
            detection.class_name == "Opponent" ||
            detection.class_name == "Goalpost") {
            return cv::Point2f(bbox.x + bbox.width / 2.0f, bbox.y + bbox.height);
        }
        return cv::Point2f(bbox.x + bbox.width / 2.0f, bbox.y + bbox.height / 2.0f);
    };

    std::vector<booster_vision::DetectionRes> detections_for_display;
    vision_interface::msg::Ball ball_msg;
    ball_msg.header = detection_msg.header;
    ball_msg.confidence = 0;

    int ball_motion_target_index = -1;
    float best_ball_confidence = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < static_cast<int>(filtered_detections.size()); ++i) {
        const auto &candidate = filtered_detections[i];
        if (detector_->kClassLabels[candidate.class_id] == "Ball" &&
            candidate.confidence > best_ball_confidence) {
            best_ball_confidence = candidate.confidence;
            ball_motion_target_index = i;
        }
    }

    for (int detection_index = 0; detection_index < static_cast<int>(filtered_detections.size()); ++detection_index) {
        auto &detection = filtered_detections[detection_index];
        vision_interface::msg::DetectedObject detection_obj;

        detection.class_name = detector_->kClassLabels[detection.class_id];

        auto pose_estimator = get_estimator(detection.class_name);
        Pose pose_obj_by_color = pose_estimator->EstimateByColor(p_eye2base, detection, color);
        Pose pose_obj_by_depth;
        std::string position_source = "projection_fallback";
        std::string fallback_reason;

        if (ground_plane_available) {
            cv::Point3f plane_position;
            cv::Point2f target_uv = get_ground_target_uv(detection);
            if (CalculatePositionWithCache(plane_position, ground_plane_cache_, p_eye2base, target_uv, intr_, &fallback_reason)) {
                pose_obj_by_depth = Pose(plane_position.x, plane_position.y, plane_position.z, 0, 0, 0);
                position_source = "ground_plane";
            }
        } else if (ground_plane_config_.enable) {
            fallback_reason = ground_plane_cache_.last_failure_reason.empty() ? "ground_plane_unavailable" : ground_plane_cache_.last_failure_reason;
        }

        if (pose_obj_by_depth == Pose() && pose_estimator->use_depth_) {
            Pose object_depth_pose = pose_estimator->EstimateByDepth(p_eye2base, detection, color, depth_float);
            if (object_depth_pose != Pose()) {
                pose_obj_by_depth = object_depth_pose;
                position_source = "object_depth";
            }
        }

        if (pose_obj_by_depth == Pose()) {
            pose_obj_by_depth = pose_obj_by_color;
        }

        auto measured_translation = pose_obj_by_depth.getTranslationVec();
        cv::Point3f measured_position(measured_translation[0], measured_translation[1], measured_translation[2]);
        cv::Point3f predicted_position = measured_position;
        ball_motion_predictor::Result ball_motion_result;
        bool dynamic_measurement = position_source == "ground_plane" || position_source == "object_depth";
        const auto &ball_motion_config = ball_motion_predictor_.config();
        bool can_predict_ball = detection.class_name == "Ball" &&
                                detection_index == ball_motion_target_index &&
                                (dynamic_measurement || ball_motion_config.allow_projection);
        if (can_predict_ball) {
            ball_motion_result = ball_motion_predictor_.update(
                ball_motion_predictor::Point3D{measured_position.x, measured_position.y, measured_position.z},
                timestamp,
                dynamic_measurement || ball_motion_config.allow_projection);
            predicted_position = cv::Point3f(
                static_cast<float>(ball_motion_result.predicted_position.x),
                static_cast<float>(ball_motion_result.predicted_position.y),
                static_cast<float>(ball_motion_result.predicted_position.z));
        }

        detection_obj.position_projection = pose_obj_by_color.getTranslationVec();
        // Keep the published position as the measured vision position. Prediction is logged only so
        // brain does not chase an extrapolated ball as if it were a direct observation.
        detection_obj.position = {measured_position.x, measured_position.y, measured_position.z};
        detection_obj.position_confidence = dynamic_measurement ? 2 : 1;

        if (ground_plane_config_.enable) {
            auto projection = pose_obj_by_color.getTranslationVec();
            auto measured = pose_obj_by_depth.getTranslationVec();
            float delta_xy = std::hypot(measured[0] - projection[0], measured[1] - projection[1]);
            std::cout << "[GroundPlane] object=" << detection.class_name
                      << ", source=" << position_source
                      << ", projection=(" << projection[0] << ", " << projection[1] << ")"
                      << ", position=(" << measured[0] << ", " << measured[1] << ")"
                      << ", delta_xy=" << delta_xy;
            if (!fallback_reason.empty() && position_source == "projection_fallback") {
                std::cout << ", fallback_reason=" << fallback_reason;
            }
            std::cout << std::endl;
        }

        if (detection.class_name == "Ball" && detection_index == ball_motion_target_index) {
            std::cout << "[BallMotion] predicted=" << ball_motion_result.prediction_applied
                      << ", measured=(" << measured_position.x << ", " << measured_position.y << ")"
                      << ", predicted_position=(" << predicted_position.x << ", " << predicted_position.y << ")"
                      << ", velocity=(" << ball_motion_result.velocity.x << ", " << ball_motion_result.velocity.y << ")"
                      << ", acceleration=(" << ball_motion_result.acceleration.x << ", " << ball_motion_result.acceleration.y << ")"
                      << ", position_confidence=" << detection_obj.position_confidence << std::endl;
        }

        auto xyz = p_head2base.getTranslationVec();
        auto rpy = p_head2base.getEulerAnglesVec();
        detection_obj.received_pos = {xyz[0], xyz[1], xyz[2],
                                      static_cast<float>(rpy[0] / CV_PI * 180), static_cast<float>(rpy[1] / CV_PI * 180), static_cast<float>(rpy[2] / CV_PI * 180)};

        detection_obj.confidence = detection.confidence * 100;
        detection_obj.xmin = detection.bbox.x;
        detection_obj.ymin = detection.bbox.y;
        detection_obj.xmax = detection.bbox.x + detection.bbox.width;
        detection_obj.ymax = detection.bbox.y + detection.bbox.height;
        detection_obj.label = detection.class_name;

        if ((color_classifier_ != nullptr) && (detection.class_name == "Opponent")) {
            // get a crop of the image given detection.bbox
            cv::Mat crop = color(detection.bbox);
            std::string robot_color_str = color_classifier_->Classify(crop);
            // add robot color to detection_obj
            detection_obj.color = robot_color_str;
        }

        // publish detection
        detection_msg.detected_objects.push_back(detection_obj);
        detections_for_display.push_back(detection);

        if (detection.class_name == "Ball" && detection.confidence > ball_msg.confidence) {
            const auto &value = detection_obj.position;
            if (value.size() >= 2 && value[0] >= -2 && value[0] <= 10 && value[1] >= -5 && value[1] <= 5) {
                ball_msg.x = value[0];
                ball_msg.y = value[1];
                ball_msg.confidence = detection.confidence;
            }
        }
    }

    // compute corner points positision
    std::vector<cv::Point2f> corner_uvs = {cv::Point2f(0, 0), cv::Point2f(color.cols - 1, 0),
                                           cv::Point2f(color.cols - 1, color.rows - 1), cv::Point2f(0, color.rows - 1),
                                           cv::Point2f(color.cols / 2.0, color.rows / 2.0)};
    for (auto &uv : corner_uvs) {
        cv::Point3f corner_pos;
        std::string corner_fallback_reason;
        if (!ground_plane_available ||
            !CalculatePositionWithCache(corner_pos, ground_plane_cache_, p_eye2base, uv, intr_, &corner_fallback_reason)) {
            corner_pos = CalculatePositionByIntersection(p_eye2base, uv, intr_);
        }
        detection_msg.corner_pos.push_back(corner_pos.x);
        detection_msg.corner_pos.push_back(corner_pos.y);
    }

    // sync-radar measurements

    // publish msg
    detection_pub_->publish(detection_msg);
    std::cout << std::endl;

    // 新增: 打印两次检测发布时间间隔
    {
        static double last_pub_time = -1.0;
        static uint64_t count = 0;
        double pub_ts = detection_msg.header.stamp.sec +
                        static_cast<double>(detection_msg.header.stamp.nanosec) * 1e-9;
        if (last_pub_time >= 0.0) {
            double diff_ms = (pub_ts - last_pub_time) * 1000.0;
            std::cout << "[Detections Pub Interval] #" << (count) 
                      << " -> #" << (count + 1) << ": " << diff_ms << " ms" << std::endl;
        }
        last_pub_time = pub_ts;
        count++;
    }

    ball_pub_->publish(ball_msg);

    // show vision results
    if (show_det_) {
        cv::Mat color_rgb;
        cv::cvtColor(color, color_rgb, cv::COLOR_BGR2RGB);
        cv::Mat img_out = YoloV8Detector::DrawDetection(color_rgb, detections_for_display);
        cv::imshow("Detection", img_out);

        // color jet depth_float and show
        // if (!depth_float.empty()) {
        //     cv::Mat depth_colormap;
        //     cv::normalize(depth_float, depth_float, 0, 255, cv::NORM_MINMAX);
        //     depth_float.convertTo(depth_float, CV_8U);
        //     cv::applyColorMap(depth_float, depth_colormap, cv::COLORMAP_JET);
        //     cv::imshow("Depth", depth_colormap);
        // }

        cv::waitKey(1);
    }

    if (save_data_) {
        save_cnt_++;
        if (save_cnt_ % save_every_n_frame_ != 0) {
            return;
        } else {
            save_cnt_ = 0;
        }
        data_logger_->LogDataBlock(synced_data);
    }
}

void VisionNode::ColorCallback(const sensor_msgs::msg::Image::ConstSharedPtr &msg) {
    std::cout << "new color for det received" << std::endl;
    auto start = std::chrono::system_clock::now();
    if (!msg) {
        std::cerr << "empty image message." << std::endl;
        return;
    }

    // cv_bridge::CvImagePtr cv_ptr;
    cv::Mat img;
    try {
        // cv_ptr = cv_bridge::toCvCopy(msg, msg->encoding);
        img = toCVMat(*msg);
    } catch (std::exception &e) {
        std::cerr << "cv_bridge exception: " << e.what() << std::endl;
        return;
    }

    if (camera_type_ == "realsense") {
        cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
    }

    vision_interface::msg::Detections detection_msg;
    detection_msg.header = msg->header;
    detection_msg.header.frame_id = "base_link";
    double timestamp = msg->header.stamp.sec + static_cast<double>(msg->header.stamp.nanosec) * 1e-9;

    // get synced data
    SyncedDataBlock synced_data = data_syncer_->getSyncedDataBlock(ColorDataBlock(img, timestamp));
    
    ProcessData(synced_data, detection_msg);
    auto end = std::chrono::system_clock::now();
    std::cout << "color callback takes: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << "ms" << std::endl;
}

void VisionNode::ProcessSegmentationData(SyncedDataBlock &synced_data, vision_interface::msg::LineSegments &field_line_segs_msg) {
    double timestamp = synced_data.color_data.timestamp;
    cv::Mat color = synced_data.color_data.data;
    Pose p_head2base = synced_data.pose_data.data;
    Pose p_eye2base = p_head2base * p_headprime2head_ * p_eye2head_;

    double time_diff = (timestamp - synced_data.pose_data.timestamp) * 1000;
    if (time_diff > 40) {
        std::cerr << "seg: color pose time diff: " << time_diff << " ms" << std::endl;
    }
    std::cout << "seg: p_eye2base: \n"
              << p_eye2base.toCVMat() << std::endl;

    // inference
    auto segmentations = segmentor_->Inference(color);
    std::vector<FieldLineSegment> field_line_segs;
    for (auto &seg : segmentations) {
        // TODO: fit circle line
        if (seg.class_id == 0) continue;
        auto line_segs = FitFieldLineSegments(p_eye2base, intr_, seg.contour, line_segment_area_threshold_);
        for (auto line_seg : line_segs) {
            float inlier_precentage = static_cast<float>(line_seg.inlier_count) / line_seg.contour_2d_points.size();
            if (inlier_precentage < 0.25) {
                continue;
            }
            field_line_segs_msg.coordinates.push_back(line_seg.end_points_3d[0].x);
            field_line_segs_msg.coordinates.push_back(line_seg.end_points_3d[0].y);
            field_line_segs_msg.coordinates.push_back(line_seg.end_points_3d[1].x);
            field_line_segs_msg.coordinates.push_back(line_seg.end_points_3d[1].y);

            field_line_segs_msg.coordinates_uv.push_back(line_seg.end_points_2d[0].x);
            field_line_segs_msg.coordinates_uv.push_back(line_seg.end_points_2d[0].y);
            field_line_segs_msg.coordinates_uv.push_back(line_seg.end_points_2d[1].x);
            field_line_segs_msg.coordinates_uv.push_back(line_seg.end_points_2d[1].y);

            field_line_segs.push_back(line_seg);
        }
    }
    std::cout << segmentations.size() << " objects segmented." << std::endl;

    field_line_pub_->publish(field_line_segs_msg);
    if (show_seg_) {
        cv::Mat img_out = YoloV8Segmentor::DrawSegmentation(color, segmentations);
        img_out = DrawFieldLineSegments(img_out, field_line_segs);
        cv::imshow("Segmentation", img_out);
        cv::waitKey(1);
    }
}

void VisionNode::SegmentationCallback(const sensor_msgs::msg::Image::ConstSharedPtr &msg) {
    if (!segmentor_) {
        std::cerr << "no segmentor loaded." << std::endl;
        return;
    }
    std::cout << "new color for seg received" << std::endl;
    if (!msg) {
        std::cerr << "empty image message." << std::endl;
        return;
    }

    // cv_bridge::CvImagePtr cv_ptr; // 使用cv_bridge将ROS图像消息转换为OpenCV cv::Mat格式
    cv::Mat img;
    try {
        // cv_ptr = cv_bridge::toCvCopy(msg, msg->encoding);
        img = toCVMat(*msg).clone();
    } catch (std::exception &e) {
        std::cerr << "cv_bridge exception: " << e.what() << std::endl;
        return;
    }

    vision_interface::msg::LineSegments field_line_segs_msg;
    field_line_segs_msg.header = msg->header;
    field_line_segs_msg.header.frame_id = "base_link";
    double timestamp = msg->header.stamp.sec + static_cast<double>(msg->header.stamp.nanosec) * 1e-9;

    // get synced data
    SyncedDataBlock synced_data = seg_data_syncer_->getSyncedDataBlock(ColorDataBlock(img, timestamp));
    ProcessSegmentationData(synced_data, field_line_segs_msg);
}

void VisionNode::DepthCallback(const sensor_msgs::msg::Image::ConstSharedPtr &msg) {
    std::cout << "new depth received" << std::endl;
    // cv_bridge::CvImagePtr cv_ptr;
    cv::Mat img;
    try {
        // TODO(SS): check if the image is 16-bit for zed camera
        // cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::TYPE_16UC1);
        img = toCVMat(*msg).clone();
    } catch (std::exception &e) {
        std::cerr << "cv_bridge exception " << e.what() << std::endl;
        return;
    }

    if (img.empty()) {
        std::cerr << "empty image recevied." << std::endl;
        return;
    }

    // Check if the image is indeed 16-bit
    if (img.depth() != CV_16U && img.depth() != CV_32F) {
        std::cerr << "image is either 16u or 32f." << std::endl;
        return;
    }

    double timestamp = msg->header.stamp.sec + static_cast<double>(msg->header.stamp.nanosec) * 1e-9;
    data_syncer_->AddDepth(DepthDataBlock(img, timestamp));
    // seg_data_syncer_->AddDepth(DepthDataBlock(img, timestamp));
}

void VisionNode::PoseTFCallBack(const geometry_msgs::msg::TransformStamped::SharedPtr msg) {
    double timestamp = msg->header.stamp.sec + static_cast<double>(msg->header.stamp.nanosec) * 1e-9;
    data_syncer_->AddPose(PoseDataBlock(Pose(*msg), timestamp));
    seg_data_syncer_->AddPose(PoseDataBlock(Pose(*msg), timestamp));
}

void VisionNode::PoseCallBack(const geometry_msgs::msg::Pose::SharedPtr msg) {
    auto current_time = this->get_clock()->now();
    double timestamp = static_cast<double>(current_time.nanoseconds()) * 1e-9;

    float x = msg->position.x;
    float y = msg->position.y;
    float z = msg->position.z;
    float qx = msg->orientation.x;
    float qy = msg->orientation.y;
    float qz = msg->orientation.z;
    float qw = msg->orientation.w;
    auto pose = Pose(x, y, z, qx, qy, qz, qw);
    data_syncer_->AddPose(PoseDataBlock(pose, timestamp));
    seg_data_syncer_->AddPose(PoseDataBlock(pose, timestamp));

    if (!offline_mode_) {
        auto tf_msg = pose.toRosTFMsg();
        tf_msg.header.stamp = builtin_interfaces::msg::Time(current_time);

        pose_tf_pub_->publish(tf_msg);
    }
}

void VisionNode::CalParamCallback(const vision_interface::msg::CalParam::SharedPtr msg) {
    float pitch_comp = msg->pitch_compensation;
    float yaw_comp = msg->yaw_compensation;
    float z_comp = msg->z_compensation;
    std::cout << "calParams: " << pitch_comp << " " << yaw_comp << " " << z_comp << std::endl;
    p_headprime2head_ = Pose(0, 0, z_comp, 0, pitch_comp * M_PI / 180, yaw_comp * M_PI / 180);
}

} // namespace booster_vision
