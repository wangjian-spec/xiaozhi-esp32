#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "eteacher/apps/word_practice/word_practice_config.h"
#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice::review_model {

inline constexpr int64_t kStrengthIntervalBonusSec = 300;
inline constexpr double kStrengthFactorBase = 0.75;
inline constexpr double kStrengthFactorScale = 0.4025;
inline constexpr double kEvidenceFactorBase = 0.8;
inline constexpr double kLapseFactorFloor = 0.35;

inline int64_t NextReviewIntervalSec(const WordMasteryProfile &profile) {
	const int skill_evidence = profile.recall_score + profile.output_score;
	const int64_t base_interval = config::kReviewIntervalBaseSec +
		static_cast<int64_t>(profile.strength) * kStrengthIntervalBonusSec;
	const double strength_factor = kStrengthFactorBase +
		(static_cast<double>(profile.strength) / static_cast<double>(config::kScoreMax)) * kStrengthFactorScale;
	const double evidence_factor = kEvidenceFactorBase +
		static_cast<double>(skill_evidence) * config::kReviewEvidenceFactorPerPoint;
	const double lapse_factor = std::max(
		kLapseFactorFloor,
		1.0 - (static_cast<double>(profile.lapse_count) * config::kReviewLapseFactorPenaltyPerLapse));
	const double interval = static_cast<double>(base_interval) * strength_factor * evidence_factor * lapse_factor;
	return std::max<int64_t>(
		config::kMinReviewIntervalSec,
		std::min<int64_t>(config::kMaxReviewIntervalSec, static_cast<int64_t>(std::llround(interval))));
}

inline int64_t EffectiveReviewIntervalSec(const WordMasteryProfile &profile) {
	if (profile.last_practiced_at > 0 && profile.next_review_at > profile.last_practiced_at) {
		return std::max<int64_t>(config::kMinReviewIntervalSec, profile.next_review_at - profile.last_practiced_at);
	}
	return NextReviewIntervalSec(profile);
}

inline double OverdueRatio(const WordMasteryProfile &profile, int64_t now_sec) {
	if (profile.next_review_at <= 0 || now_sec <= profile.next_review_at) {
		return 0.0;
	}
	const int64_t effective_interval = EffectiveReviewIntervalSec(profile);
	if (effective_interval <= 0) {
		return 0.0;
	}
	return static_cast<double>(now_sec - profile.next_review_at) / static_cast<double>(effective_interval);
}

}  // namespace word_practice::review_model