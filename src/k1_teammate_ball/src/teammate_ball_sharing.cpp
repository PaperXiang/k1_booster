#include "k1_teammate_ball/teammate_ball_sharing.hpp"

#include <cmath>

namespace k1_teammate_ball {

bool TeammateBallSharing::shouldShare(double local_confidence, bool local_detected) const {
    if (!cfg_.enable) return false;
    return local_detected && local_confidence >= cfg_.min_confidence_to_share;
}

void TeammateBallSharing::ingest(const BallReport &report) {
    reports_[report.player_id] = report; // 按 player_id 覆盖, 只保留最新一条
}

void TeammateBallSharing::reset() {
    reports_.clear();
    last_reliable_ = FusedBall{};
    last_reliable_sec_ = -1e9;
}

FusedBall TeammateBallSharing::fuse(double self_x, double self_y, bool /*self_location_known*/, double now_sec) {
    FusedBall result;
    if (!cfg_.enable) {
        return result; // 未启用: reliable=false
    }

    const BallReport *best = nullptr;
    for (const auto &kv : reports_) {
        const BallReport &r = kv.second;

        if (now_sec - r.stamp_sec > cfg_.teammate_timeout_sec) continue; // 过期报告丢弃
        if (!r.detected) continue;                                       // 必须当前检测到
        if (cfg_.require_location_known && !r.location_known) continue;   // 可选: 要求队友自认球位已知
        if (r.confidence < cfg_.min_confidence_to_trust) continue;        // 高置信度门控 (核心)

        // 自距门控: 队友球须距我足够远才采信 (规避双方定位误差导致的大方向误判)
        double dist = std::hypot(r.x - self_x, r.y - self_y);
        if (dist <= cfg_.self_dist_threshold) continue;

        // 选择: 置信度最高者优先; 置信度相同则取 range 更小者 (看得更近通常更准)
        if (best == nullptr
            || r.confidence > best->confidence
            || (r.confidence == best->confidence && r.range < best->range)) {
            best = &r;
        }
    }

    if (best != nullptr) {
        result.reliable = true;
        result.is_fresh = true;
        result.source_player_id = best->player_id;
        result.x = best->x;
        result.y = best->y;
        result.confidence = best->confidence;
        last_reliable_ = result;
        last_reliable_sec_ = now_sec;
        return result;
    }

    // 无新的可信来源: 在 trust_timeout_sec 内维持上次结果 (位置沿用上次值)
    if (last_reliable_.reliable && (now_sec - last_reliable_sec_) <= cfg_.trust_timeout_sec) {
        FusedBall held = last_reliable_;
        held.is_fresh = false;
        return held;
    }

    return result; // reliable=false
}

} // namespace k1_teammate_ball
