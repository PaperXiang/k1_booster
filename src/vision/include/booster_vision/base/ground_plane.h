#pragma once

#include <array>
#include <string>

#include <opencv2/opencv.hpp>

#include "booster_vision/base/intrin.h"
#include "booster_vision/base/pose.h"

namespace booster_vision {

struct GroundPlaneConfig {
    bool enable = false;
    int update_every_n_frames = 5;
    int sample_step = 8;
    float min_depth = 0.2f;
    float max_depth = 6.0f;
    float ransac_distance_threshold = 0.02f;
    float min_inlier_ratio = 0.35f;
    float max_normal_tilt_deg = 25.0f;
    float force_update_head_pitch_delta_deg = 3.0f;
    float force_update_head_yaw_delta_deg = 5.0f;
    float min_ground_height = -0.20f;
    float max_ground_height = 0.25f;
};

struct GroundPlaneCache {
    bool valid = false;
    std::array<float, 4> plane_base = {0.0f, 0.0f, 1.0f, 0.0f};
    std::array<float, 4> plane_eye = {0.0f, 0.0f, 1.0f, 0.0f};
    double timestamp = 0.0;
    float inlier_ratio = 0.0f;
    Pose last_p_eye2base;
    std::string last_failure_reason;
};

bool FitGroundPlaneFromDepth(GroundPlaneCache &cache,
                             const cv::Mat &depth,
                             const cv::Mat &rgb,
                             const Intrinsics &intr,
                             const Pose &p_eye2base,
                             double timestamp,
                             const GroundPlaneConfig &config);

bool PrecomputePlaneTransform(GroundPlaneCache &cache,
                              const Pose &p_eye2base);

bool CalculatePositionWithCache(cv::Point3f &position_base,
                                const GroundPlaneCache &cache,
                                const Pose &p_eye2base,
                                const cv::Point2f &target_uv,
                                const Intrinsics &intr,
                                std::string *failure_reason = nullptr);

} // namespace booster_vision
