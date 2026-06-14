#include "booster_vision/base/ground_plane.h"

#include <cmath>
#include <vector>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include "booster_vision/base/pointcloud_process.h"

namespace booster_vision {

bool PlaneModel::Normalize() {
    float norm = std::sqrt(a * a + b * b + c * c);
    if (!std::isfinite(norm) || norm < 1e-6f) {
        return false;
    }
    a /= norm;
    b /= norm;
    c /= norm;
    d /= norm;
    return true;
}

void PlaneModel::EnsureNormalUp() {
    if (c < 0.0f) {
        a = -a;
        b = -b;
        c = -c;
        d = -d;
    }
}

GroundPlaneEstimator::GroundPlaneEstimator(const GroundPlaneConfig &config) :
    config_(config) {
}

bool GroundPlaneEstimator::HasValidPlane() const {
    return cache_.valid;
}

const GroundPlaneResult &GroundPlaneEstimator::GetCache() const {
    return cache_;
}

void GroundPlaneEstimator::Reset() {
    cache_ = GroundPlaneResult();
    frame_count_ = 0;
}

GroundPlaneResult GroundPlaneEstimator::FailOrCached(const std::string &reason,
                                                     const Pose &p_eye2base,
                                                     double timestamp) {
    if (config_.use_cached_plane_when_fit_failed && cache_.valid) {
        GroundPlaneResult result = cache_;
        result.timestamp = timestamp;
        result.failure_reason = reason + "; using cached plane";
        TransformPlaneBaseToEye(result.plane_base, p_eye2base, result.plane_eye);
        return result;
    }

    GroundPlaneResult result;
    result.timestamp = timestamp;
    result.failure_reason = reason;
    return result;
}

bool GroundPlaneEstimator::TransformPlaneBaseToEye(const PlaneModel &plane_base,
                                                  const Pose &p_eye2base,
                                                  PlaneModel &plane_eye) const {
    cv::Mat rot = p_eye2base.getRotationMatrix();
    cv::Mat trans = p_eye2base.getTranslationVecMatrix();
    cv::Mat normal_base = (cv::Mat_<float>(3, 1) << plane_base.a, plane_base.b, plane_base.c);
    cv::Mat normal_eye = rot.t() * normal_base;

    float d_eye = plane_base.d +
                  plane_base.a * trans.at<float>(0, 0) +
                  plane_base.b * trans.at<float>(1, 0) +
                  plane_base.c * trans.at<float>(2, 0);

    plane_eye.a = normal_eye.at<float>(0, 0);
    plane_eye.b = normal_eye.at<float>(1, 0);
    plane_eye.c = normal_eye.at<float>(2, 0);
    plane_eye.d = d_eye;
    return plane_eye.Normalize();
}

bool GroundPlaneEstimator::ShouldForceUpdate(const Pose &p_eye2base) const {
    if (!cache_.valid) {
        return true;
    }

    auto last_rpy = last_p_eye2base_.getEulerAnglesVec();
    auto current_rpy = p_eye2base.getEulerAnglesVec();
    float pitch_delta_deg = std::fabs(current_rpy[1] - last_rpy[1]) * 180.0f / static_cast<float>(CV_PI);
    float yaw_delta_deg = std::fabs(current_rpy[2] - last_rpy[2]) * 180.0f / static_cast<float>(CV_PI);
    return pitch_delta_deg > config_.force_update_head_pitch_delta_deg ||
           yaw_delta_deg > config_.force_update_head_yaw_delta_deg;
}

void GroundPlaneEstimator::SmoothPlane(PlaneModel &plane_base) const {
    (void)plane_base;
}

GroundPlaneResult GroundPlaneEstimator::UpdateFromDepth(const cv::Mat &depth_m,
                                                        const Intrinsics &intr,
                                                        const Pose &p_eye2base,
                                                        double timestamp) {
    if (!config_.enable) {
        return FailOrCached("ground plane disabled", p_eye2base, timestamp);
    }
    if (depth_m.empty()) {
        return FailOrCached("empty depth image", p_eye2base, timestamp);
    }
    if (depth_m.type() != CV_32FC1) {
        return FailOrCached("depth image must be CV_32FC1 meters", p_eye2base, timestamp);
    }

    frame_count_++;
    if (cache_.valid && config_.update_every_n_frames > 0 &&
        frame_count_ % config_.update_every_n_frames != 0 && !ShouldForceUpdate(p_eye2base)) {
        GroundPlaneResult result = cache_;
        result.timestamp = timestamp;
        TransformPlaneBaseToEye(result.plane_base, p_eye2base, result.plane_eye);
        return result;
    }

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr ground_cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    int y_begin = depth_m.rows / 2;
    int step = std::max(1, config_.sample_step);

    for (int v = y_begin; v < depth_m.rows; v += step) {
        for (int u = 0; u < depth_m.cols; u += step) {
            float depth = depth_m.at<float>(v, u);
            if (!std::isfinite(depth) || depth < config_.min_depth || depth > config_.max_depth) {
                continue;
            }

            cv::Point3f point_eye = intr.BackProject(cv::Point2f(u, v), depth);
            cv::Point3f point_base = p_eye2base * point_eye;
            if (point_base.z < config_.min_ground_height || point_base.z > config_.max_ground_height) {
                continue;
            }

            pcl::PointXYZRGB point;
            point.x = point_base.x;
            point.y = point_base.y;
            point.z = point_base.z;
            ground_cloud->points.push_back(point);
        }
    }

    if (ground_cloud->points.size() < 100) {
        return FailOrCached("not enough ground candidate points", p_eye2base, timestamp);
    }

    std::vector<float> plane_coeffs;
    float confidence = 0.0f;
    PlaneFitting(plane_coeffs, confidence, ground_cloud, config_.ransac_distance_threshold);
    if (plane_coeffs.size() < 4 || confidence <= 0.0f) {
        return FailOrCached("plane fitting failed", p_eye2base, timestamp);
    }

    PlaneModel plane_base{plane_coeffs[0], plane_coeffs[1], plane_coeffs[2], plane_coeffs[3]};
    if (!plane_base.Normalize()) {
        return FailOrCached("invalid plane coefficients", p_eye2base, timestamp);
    }
    plane_base.EnsureNormalUp();

    float cos_limit = std::cos(config_.max_normal_tilt_deg * static_cast<float>(CV_PI) / 180.0f);
    if (std::fabs(plane_base.c) < cos_limit) {
        return FailOrCached("ground normal tilt too large", p_eye2base, timestamp);
    }
    if (confidence < config_.min_inlier_ratio) {
        return FailOrCached("ground inlier ratio too low", p_eye2base, timestamp);
    }

    SmoothPlane(plane_base);

    PlaneModel plane_eye;
    if (!TransformPlaneBaseToEye(plane_base, p_eye2base, plane_eye)) {
        return FailOrCached("plane transform to eye failed", p_eye2base, timestamp);
    }

    GroundPlaneResult result;
    result.valid = true;
    result.plane_base = plane_base;
    result.plane_eye = plane_eye;
    result.timestamp = timestamp;
    result.inlier_ratio = confidence;
    result.sampled_points = static_cast<int>(ground_cloud->points.size());
    result.inlier_points = static_cast<int>(confidence * ground_cloud->points.size());

    cache_ = result;
    last_p_eye2base_ = p_eye2base;
    return result;
}

bool GroundPlaneEstimator::IntersectRay(const cv::Point2f &target_uv,
                                        const Intrinsics &intr,
                                        const Pose &p_eye2base,
                                        cv::Point3f &position_base) const {
    if (!cache_.valid) {
        return false;
    }

    PlaneModel plane_eye;
    if (!TransformPlaneBaseToEye(cache_.plane_base, p_eye2base, plane_eye)) {
        return false;
    }

    cv::Point3f ray = intr.BackProject(target_uv);
    float denom = plane_eye.a * ray.x + plane_eye.b * ray.y + plane_eye.c * ray.z;
    if (!std::isfinite(denom) || std::fabs(denom) < 1e-6f) {
        return false;
    }

    float scale = -plane_eye.d / denom;
    if (!std::isfinite(scale) || scale <= 0.0f) {
        return false;
    }

    cv::Point3f point_eye(ray.x * scale, ray.y * scale, ray.z * scale);
    position_base = p_eye2base * point_eye;
    return std::isfinite(position_base.x) &&
           std::isfinite(position_base.y) &&
           std::isfinite(position_base.z);
}

} // namespace booster_vision

