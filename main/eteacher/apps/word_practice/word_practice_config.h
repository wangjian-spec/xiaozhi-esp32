#pragma once

#include <cstddef>
#include <cstdint>

namespace word_practice::config {

// 画像数值上限。strength 与 persistent_boost 都会被夹到该范围内。
inline constexpr int kScoreMax = 100;

// 技能证据上限。recall_score / output_score 都会被夹到该范围内。
inline constexpr int kEvidenceMax = 5;

// UI 阶段映射参数。stage = 1 + strength / kUiStageStep。
inline constexpr int kUiStageStep = 20;
inline constexpr int kUiStageMin = 1;
inline constexpr int kUiStageMax = 5;

// 已掌握单词最低显示阶段。即使 strength 还没推到对应 stage，也至少显示到该阶段。
inline constexpr int kMasteredUiStageFloor = 4;

// 掌握单词判定门槛。
inline constexpr int kMasteredMinRecallScore = 3;
inline constexpr int kMasteredMinOutputScore = 3;
inline constexpr int kMasteredMinStrength = 60;
inline constexpr int kMasteredMaxLapseCount = 3;

// 技能权重。影响答对/答错时 strength 的变化幅度。
inline constexpr double kRecognitionSkillWeight = 1.0;
inline constexpr double kRecallSkillWeight = 1.5;
inline constexpr double kOutputSkillWeight = 2.0;
inline constexpr double kAdvancedSpeakSkillWeight = 2.0;

// 响应时间分段阈值与其对应置信度加成。
inline constexpr int kFastResponseThresholdMs = 4000;
inline constexpr int kMediumResponseThresholdMs = 8000;
inline constexpr int kSlowResponseThresholdMs = 12000;
inline constexpr double kFastResponseConfidence = 1.2;
inline constexpr double kMediumResponseConfidence = 1.0;
inline constexpr double kSlowResponseConfidence = 0.8;
inline constexpr double kVerySlowResponseConfidence = 0.65;

// 错误严重度系数。输出类题目和超时作答会增加惩罚。
inline constexpr double kOutputErrorSeverityBonus = 0.35;
inline constexpr double kRecallErrorSeverityBonus = 0.2;
inline constexpr int kSlowErrorSeverityThresholdMs = 8000;
inline constexpr double kSlowErrorSeverityBonus = 0.15;

// 答对时 strength 增长公式：round(kCorrectStrengthBaseDelta * skill_weight * confidence_factor)。
inline constexpr double kCorrectStrengthBaseDelta = 7.0;

// 答对后长期弱词强化值的自然衰减量。
inline constexpr int kPersistentBoostDecayOnCorrect = 5;

// 答错时 strength 惩罚公式：round(kWrongStrengthBasePenalty * skill_weight * severity)。
inline constexpr double kWrongStrengthBasePenalty = 6.0;
inline constexpr int kCorrectStrengthMinDelta = 1;
inline constexpr int kWrongStrengthMinPenalty = 2;

// 错误累计到该次数后，将该词标记为长期弱词并设置 persistent_boost。
inline constexpr int kPersistentBoostTriggerLapseCount = 3;
inline constexpr int kPersistentBoostTriggeredValue = 30;

// 新建 profile 时的默认画像初始值。
inline constexpr int kDefaultNewWordStrength = 12;
inline constexpr int kDefaultReviewStrength = 36;
inline constexpr int kDefaultReviewRecallScore = 1;

// 复习间隔下界与上界。
inline constexpr int64_t kMinReviewIntervalSec = 3600;
inline constexpr int64_t kMaxReviewIntervalSec = 21 * 86400;

// 复习间隔主公式参数。
inline constexpr int64_t kReviewIntervalBaseSec = 6 * 3600;
inline constexpr int64_t kReviewIntervalPerStrengthSec = 900;
inline constexpr double kReviewStrengthFactorBase = 0.75;
inline constexpr double kReviewStrengthFactorScale = 1.15;
inline constexpr double kReviewEvidenceFactorBase = 0.8;
inline constexpr double kReviewEvidenceFactorPerPoint = 0.08;
inline constexpr double kReviewLapseFactorFloor = 0.35;
inline constexpr double kReviewLapseFactorPenaltyPerLapse = 0.12;
inline constexpr double kReviewPersistentBoostFactor = 0.8;

// 逾期衰减规则。
inline constexpr int kDecayPenaltyPerOverdueDay = 3;
inline constexpr int kDecayPersistentBoostDiscount = 4;
inline constexpr int kDecayMinPenalty = 1;
inline constexpr int kDecayMaxPenalty = 18;
inline constexpr int kDecayRecallDropStartOverdueDays = 2;
inline constexpr int kDecayRecallDropPerTrigger = 1;

// Session 启动时，至少保留多少个可用于后续轮转的活跃 profile。
inline constexpr int kMinimumActiveProfileWords = 20;

// 批次大小范围。planner 会把批次词数夹在该范围内。
inline constexpr int kBatchMinSlots = 8;
inline constexpr int kBatchMaxSlots = 10;

// ColdStart 下新词目标数公式参数。
inline constexpr int kColdStartDesiredNewMin = 3;
inline constexpr int kColdStartDesiredNewMax = 5;
inline constexpr int kColdStartDesiredNewDivisor = 2;

// Normal/IntensiveReview 下新词目标数公式参数。
inline constexpr int kNormalDesiredNewMin = 1;
inline constexpr int kNormalDesiredNewMax = 3;
inline constexpr int kNormalDesiredNewDivisor = 4;

// Normal 下弱词目标数公式参数。
inline constexpr int kNormalDesiredWeakMin = 1;
inline constexpr int kNormalDesiredWeakMax = 3;
inline constexpr int kNormalDesiredWeakDivisor = 5;

// IntensiveReview 下弱词目标数公式参数。
inline constexpr int kIntensiveDesiredWeakMin = 2;
inline constexpr int kIntensiveDesiredWeakMax = 4;
inline constexpr int kIntensiveDesiredWeakDivisor = 3;

// 每轮至少保留多少个 review 槽位。
inline constexpr int kMinimumDesiredReviewSlots = 1;

// 整个 batch 进入“完成”状态时，需要完成的单词比例。
inline constexpr double kBatchCompletionRatio = 0.8;

// 弱词判定参数。复习词满足任一条件即会被归到 WeakWord。
inline constexpr int kWeakWordStrengthThreshold = 45;
inline constexpr int kWeakWordLapseThreshold = 3;

// 新词、复习词、弱词在当前 Session 中的完成规则。
inline constexpr int kNewWordRequiredShown = 0;
inline constexpr int kNewWordRequiredAnyCorrect = 0;
inline constexpr int kNewWordRequiredRecognitionCorrect = 1;
inline constexpr int kNewWordRequiredRecallCorrect = 1;
inline constexpr int kNewWordRequiredOutputCorrect = 0;

inline constexpr int kReviewWordRequiredShown = 0;
inline constexpr int kReviewWordRequiredAnyCorrect = 1;
inline constexpr int kReviewWordRequiredRecognitionCorrect = 0;
inline constexpr int kReviewWordRequiredRecallCorrect = 0;
inline constexpr int kReviewWordRequiredOutputCorrect = 0;

inline constexpr int kWeakWordRequiredShown = 2;
inline constexpr int kWeakWordRequiredAnyCorrect = 1;
inline constexpr int kWeakWordRequiredRecognitionCorrect = 0;
inline constexpr int kWeakWordRequiredRecallCorrect = 0;
inline constexpr int kWeakWordRequiredOutputCorrect = 0;

// LearningMode 判定阈值。
inline constexpr float kColdStartNewWordRatioThreshold = 0.5f;
inline constexpr float kIntensiveReviewWeakOrDueRatioThreshold = 0.5f;

// 画像达到这些门槛后，训练技能会从 Recall 进入 Output，再进入 AdvancedSpeak。
inline constexpr int kRecallToOutputThreshold = 3;
inline constexpr int kOutputToAdvancedSpeakThreshold = 3;

// 调度器词优先级分值。
inline constexpr int kSchedulerPriorityWeakWord = 100;
inline constexpr int kSchedulerPriorityDueReview = 80;
inline constexpr int kSchedulerPriorityReviewBacklog = 60;
inline constexpr int kSchedulerPriorityNewWord = 50;
inline constexpr int kSchedulerIntensiveReviewBonus = 10;
inline constexpr int kSchedulerSeenPenaltyPerShow = 10;

// 单词 completion 计算权重。
inline constexpr float kWordCompletionRecognitionWeight = 0.3f;
inline constexpr float kWordCompletionRecallWeight = 0.4f;
inline constexpr float kWordCompletionOutputWeight = 0.3f;

// Session 成功门槛。
inline constexpr float kSessionSuccessCompletionThreshold = 0.8f;
inline constexpr float kSessionSuccessAccuracyThreshold = 0.7f;

// 通过内容门槛时，至少需要完成多少比例的词。
inline constexpr float kSessionRequiredCompletedWordsRatio = 0.8f;

// Session 至少要答多少题才允许通过进度门槛。
inline constexpr int kSessionMinimumQuestionFloor = 4;

// 计算 minimum_questions 时，完成词数与目标词数会被该上限夹住，避免门槛过大。
inline constexpr int kSessionMinimumQuestionSoftCap = 6;

// 口语长句题（type 9/10）ASR 覆盖率门槛。
inline constexpr float kSpeakSentenceCoverageThreshold = 0.8f;

// 每轮默认练习词数。
inline constexpr int kDefaultRoundWordTarget = 15;

// 口语题最大失败重试次数。
inline constexpr int kMaxSpeakRetryCount = 3;

// 题目池初次预热和增量扩池的 seed 数量。
inline constexpr std::size_t kInitialQuestionSeedWarmupCount = 4;
inline constexpr std::size_t kIncrementalQuestionSeedWarmupCount = 2;

// 题型常量。替代散落各处的魔数 1-12。
// 映射关系见 word_practice_detail_design_doc.md §8.4。
inline constexpr int kQuestionTypeImageChoice = 1;
inline constexpr int kQuestionTypeMeaningChoice = 2;
inline constexpr int kQuestionTypeWordToMeaning = 3;
inline constexpr int kQuestionTypePairMatch = 4;
inline constexpr int kQuestionTypeSentenceFillZh = 5;
inline constexpr int kQuestionTypeSentenceBuildEn = 6;
inline constexpr int kQuestionTypeSpeakWord = 7;
inline constexpr int kQuestionTypeSpeakMeaning = 8;
inline constexpr int kQuestionTypeSpeakSentence = 9;
inline constexpr int kQuestionTypeSpeakTranslate = 10;
inline constexpr int kQuestionTypeAudioWordChoice = 11;
inline constexpr int kQuestionTypeAudioMeaningChoice = 12;

}  // namespace word_practice::config