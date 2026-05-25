#include "booster_vision/base/ground_plane.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include "booster_vision/base/pointcloud_process.h"

namespace booster_vision {
namespace {

bool IsFinitePoint(const cv::Point3f &point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool NormalizePlane(std::array<float, 4> &plane) {
    float norm = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
    if (!std::isfinite(norm) || norm < 1e-6f) {
        return false;
    }
    for (float &value : plane) {
        value /= norm;
    }
    return true;
}

} // namespace

bool FitGroundPlaneFromDepth(GroundPlaneCache &cache,
                             const cv::Mat &depth,
                             const cv::Mat &rgb,
                             const Intrinsics &intr,
                             const Pose &p_eye2base,
                             double timestamp,
                             const GroundPlaneConfig &config) {
    cache.last_failure_reason.clear();

    if (depth.empty() || depth.depth() != CV_32F || depth.channels() != 1) {
        cache.last_failure_reason = "invalid_depth_image";
        return false;
    }

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    int step = std::max(1, config.sample_step);
    int y_begin = depth.rows / 2;
    for (int v = y_begin; v < depth.rows; v += step) {
        for (int u = 0; u < depth.cols; u += step) {
            float d = depth.at<float>(v, u);
            if (!std::isfinite(d) || d < config.min_depth || d > config.max_depth) {
                continue;
            }

            cv::Point3f point_eye = intr.BackProject(cv::Point2f(u, v), d);
            cv::Point3f point_base = p_eye2base * point_eye;
            if (!IsFinitePoint(point_base)) {
                continue;
            }

            if (point_base.z < config.min_ground_height || point_base.z > config.max_ground_height) {
                continue;
            }

            pcl::PointXYZRGB point;
            point.x = point_base.x;
            point.y = point_base.y;
            point.z = point_base.z;
            if (!rgb.empty() && rgb.channels() >= 3 && v < rgb.rows && u < rgb.cols) {
                const auto color = rgb.at<cv::Vec3b>(v, u);
                point.b = color[0];
                point.g = color[1];
                point.r = color[2];
            }
            cloud->points.push_back(point);
        }
    }

    if (cloud->points.size() < 100) {
        cache.last_failure_reason = "not_enough_ground_points";
        return false;
    }

    std::vector<float> plane_coeffs;
    float confidence = 0.0f;
    PlaneFitting(plane_coeffs, confidence, cloud, config.ransac_distance_threshold);
    if (plane_coeffs.size() < 4) {
        cache.last_failure_reason = "plane_fit_empty";
        return false;
    }

    std::array<float, 4> plane = {plane_coeffs[0], plane_coeffs[1], plane_coeffs[2], plane_coeffs[3]};
    if (!NormalizePlane(plane)) {
        cache.last_failure_reason = "plane_normal_invalid";
        return false;
    }

    if (plane[2] < 0.0f) {
        for (float &value : plane) {
            value = -value;
        }
    }

    float cos_tilt = std::cos(config.max_normal_tilt_deg * static_cast<float>(CV_PI) / 180.0f);
    if (std::fabs(plane[2]) < cos_tilt) {
        cache.last_failure_reason = "plane_tilt_too_large";
        return false;
    }

    if (confidence < config.min_inlier_ratio) {
        cache.last_failure_reason = "plane_inlier_ratio_low";
        return false;
    }

    cache.valid = true;
    cache.plane_base = plane;
    cache.timestamp = timestamp;
    cache.inlier_ratio = confidence;
    cache.last_p_eye2base = p_eye2base;
    return PrecomputePlaneTransform(cache, p_eye2base);
}

bool PrecomputePlaneTransform(GroundPlaneCache &cache,
                              const Pose &p_eye2base) {
    if (!cache.valid) {
        cache.last_failure_reason = "cache_invalid";
        return false;
    }

    cv::Mat rot = p_eye2base.getRotationMatrix();
    cv::Mat trans = p_eye2base.getTranslationVecMatrix();
    cv::Mat normal_base = (cv::Mat_<float>(3, 1) << cache.plane_base[0], cache.plane_base[1], cache.plane_base[2]);
    cv::Mat normal_eye = rot.t() * normal_base;
    float d_eye = cache.plane_base[3] + normal_base.dot(trans);

    std::array<float, 4> plane_eye = {
        normal_eye.at<float>(0, 0),
        normal_eye.at<float>(1, 0),
        normal_eye.at<float>(2, 0),
        d_eye,
    };
    if (!NormalizePlane(plane_eye)) {
        cache.last_failure_reason = "transformed_plane_invalid";
        return false;
    }

    cache.plane_eye = plane_eye;
    cache.last_failure_reason.clear();
    return true;
}

bool CalculatePositionWithCache(cv::Point3f &position_base,
                                const GroundPlaneCache &cache,
                                const Pose &p_eye2base,
                                const cv::Point2f &target_uv,
                                const Intrinsics &intr,
                                std::string *failure_reason) {
    auto fail = [&](const std::string &reason) {
        if (failure_reason != nullptr) {
            *failure_reason = reason;
        }
        return false;
    };

    if (!cache.valid) {
        return fail("cache_invalid");
    }

    cv::Point3f ray = intr.BackProject(target_uv);
    if (!IsFinitePoint(ray)) {
        return fail("ray_invalid");
    }

    const auto &plane = cache.plane_eye;
    float denom = plane[0] * ray.x + plane[1] * ray.y + plane[2] * ray.z;
    if (!std::isfinite(denom) || std::fabs(denom) < 1e-6f) {
        return fail("ray_parallel_to_plane");
    }

    float scale = -plane[3] / denom;
    if (!std::isfinite(scale) || scale <= 0.0f) {
        return fail("intersection_behind_camera");
    }

    cv::Point3f point_eye(ray.x * scale, ray.y * scale, ray.z * scale);
    position_base = p_eye2base * point_eye;
    if (!IsFinitePoint(position_base)) {
        return fail("intersection_invalid");
    }
    if (failure_reason != nullptr) {
        failure_reason->clear();
    }
    return true;
}

} // namespace booster_vision
