#pragma once

#include <string>

#include <opencv2/opencv.hpp>

#include "booster_vision/base/intrin.h"
#include "booster_vision/base/pose.h"

namespace booster_vision {

struct GroundPlaneConfig {
    bool enable = false;
    bool debug = false;
    int update_every_n_frames = 5;
    int sample_step = 8;
    float min_depth = 0.2f;
    float max_depth = 6.0f;
    float min_ground_height = -0.20f;
    float max_ground_height = 0.25f;
    float ransac_distance_threshold = 0.02f;
    float min_inlier_ratio = 0.35f;
    float max_normal_tilt_deg = 25.0f;
    float force_update_head_pitch_delta_deg = 3.0f;
    float force_update_head_yaw_delta_deg = 5.0f;
    bool use_cached_plane_when_fit_failed = true;
    bool write_to_position = true;
    bool write_to_position_projection = false;
};

struct PlaneModel {
    float a = 0.0f;
    float b = 0.0f;
    float c = 1.0f;
    float d = 0.0f;

    bool Normalize();
    void EnsureNormalUp();
};

struct GroundPlaneResult {
    bool valid = false;
    PlaneModel plane_base;
    PlaneModel plane_eye;
    double timestamp = 0.0;
    float inlier_ratio = 0.0f;
    int sampled_points = 0;
    int inlier_points = 0;
    std::string failure_reason;
};

class GroundPlaneEstimator {
public:
    explicit GroundPlaneEstimator(const GroundPlaneConfig &config);

    GroundPlaneResult UpdateFromDepth(const cv::Mat &depth_m,
                                      const Intrinsics &intr,
                                      const Pose &p_eye2base,
                                      double timestamp);

    bool IntersectRay(const cv::Point2f &target_uv,
                      const Intrinsics &intr,
                      const Pose &p_eye2base,
                      cv::Point3f &position_base) const;

    bool HasValidPlane() const;
    const GroundPlaneResult &GetCache() const;
    void Reset();

private:
    GroundPlaneResult FailOrCached(const std::string &reason,
                                   const Pose &p_eye2base,
                                   double timestamp);
    bool TransformPlaneBaseToEye(const PlaneModel &plane_base,
                                 const Pose &p_eye2base,
                                 PlaneModel &plane_eye) const;
    bool ShouldForceUpdate(const Pose &p_eye2base) const;
    void SmoothPlane(PlaneModel &plane_base) const;

    GroundPlaneConfig config_;
    GroundPlaneResult cache_;
    Pose last_p_eye2base_;
    int frame_count_ = 0;
};

} // namespace booster_vision

