#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace word_practice::config {

// word_practice 调参分层约定：
// 1. 日常调试优先改“Session/通关节奏”与“等级阶段显示”两组。
// 2. “高级画像/复习公式”会改变长期学习曲线，只建议在联调后批量调整。
// 3. “固定协议/UI 常量”默认不要改；改动前需要同步题型、UI 与持久化契约。
// 4. 当前模型已经收敛到“effective_interval + overdue_ratio + lapse_count”视角。
//    因此真正适合日常调的旋钮，优先只看下面这 12 个：
//    - 负载/通关: kDefaultDailyTotalTarget, kBatchMaxSlots,
//      kBatchMaxSlots, kSessionMinimumQuestionFloor, kSessionRequiredCompletedWordsRatio,
//      kSessionSuccessAccuracyThreshold
//    - 新词/复习配比: kDefaultDailyNewWordTarget, kDefaultDailyReviewWordTarget,
//      kColdStartDesiredNewMin, kNormalDesiredNewMin, kIntensiveDesiredWeakMin
//    - 复习压力: kReviewIntervalBaseSec, kDecayPenaltyPerOverdueDay
// 5. 仍然公开但不建议高频调的参数，主要是为了兼容旧逻辑、做极端实验或批量联调。

// -----------------------------------------------------------------------------
// 固定协议与题型常量：默认不要修改。
// -----------------------------------------------------------------------------

// 题型常量。替代散落各处的魔数 1-12。
// 映射关系见 word_practice_design.md §8.4。
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

// 题型选项协议：题型 1 固定 3 图，题型 2/3/11/12 固定 4 选项。
inline constexpr std::size_t kImageChoiceOptionCount = 3;
inline constexpr std::size_t kStandardChoiceOptionCount = 4;

// 数值上限属于运行时收敛边界，不建议改动。
inline constexpr int kScoreMax = 100;
inline constexpr int kEvidenceMax = 5;

// -----------------------------------------------------------------------------
// 等级/阶段显示：会直接影响首页等级、阶段展示与“已掌握”的判定。
// -----------------------------------------------------------------------------

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

// 首页等级显示。数组下标对应 stage1-stage12，每档表示升 1 级需要新增多少 mastered words。
inline constexpr std::array<int, 12> kDefaultStageLevelupCount = {10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 48};

// 首页等级图标布局，只影响显示，不属于日常调参项。
inline constexpr int kLevelIconCellSize = 20;
inline constexpr int kLevelIconMaxPerRow = 5;
inline constexpr int kLevelIconMaxCount = 25;

// -----------------------------------------------------------------------------
// 高级画像/复习公式：会影响长期学习曲线，建议成组调整。
// 说明：这一组里只有少数参数还是“一等旋钮”，其余很多已退居辅助或兼容角色。
// -----------------------------------------------------------------------------

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

// 答对/答错后 strength 的变化公式参数。
// 说明：strength 仍影响 stage 与一部分 interval，但已经不是调度主因子。
// 直觉示例（按当前默认配置）：
// - Recognition 答对且很快：7.0 * 1.0 * 1.2 ≈ +8
// - Recall 正常答对：7.0 * 1.5 * 1.0 ≈ +11
// - Output 正常答对：7.0 * 2.0 * 1.0 = +14
// - Recognition 答错：6.0 * 1.0 * 1.0 = -6
// - Recall 答错：6.0 * 1.5 * 1.2 ≈ -11
// - Output 慢答错：6.0 * 2.0 * (1 + 0.35 + 0.15) = -18
// 如果你想整体放大/缩小“每答一题的影响”，优先先改这两个 base 值，
// 不要同时改 skill weight、confidence、severity 三层参数。
inline constexpr double kCorrectStrengthBaseDelta = 7.0;
inline constexpr int kPersistentBoostDecayOnCorrect = 5;
inline constexpr double kWrongStrengthBasePenalty = 6.0;
inline constexpr int kCorrectStrengthMinDelta = 1;
inline constexpr int kWrongStrengthMinPenalty = 2;

// 兼容旧画像的 persistent_boost 保留项。
// 说明：persistent_boost 已不是优先级和 learning mode 的一等信号，默认不建议围绕它调参。
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
// 说明：当前公开保留的主旋钮是 base interval、evidence 增益、lapse 惩罚。
// strength 放大系数、evidence 基线、lapse floor 已内化到 review_model，不再作为公开调参项。
// 当前实现大致是：
// interval ~= (base_sec + strength * 300) * strength_factor * evidence_factor * lapse_factor
// 其中 strength_factor / evidence_factor / lapse_factor 为固定实现常量与下面 3 个参数共同作用。
// 调参时如果你只想“整体更快复习/更慢复习”，先只改 kReviewIntervalBaseSec。
inline constexpr int64_t kReviewIntervalBaseSec = 6 * 3600;
inline constexpr double kReviewEvidenceFactorPerPoint = 0.08;
inline constexpr double kReviewLapseFactorPenaltyPerLapse = 0.12;

// 逾期衰减规则。
// 说明：当前衰减主要跟 overdue_ratio 对应的 pressure 和 lapse_count 走，
// 想让复习更紧/更松，优先只改 kReviewIntervalBaseSec 和 kDecayPenaltyPerOverdueDay。
inline constexpr int kDecayPenaltyPerOverdueDay = 3;
inline constexpr int kDecayMinPenalty = 1;
inline constexpr int kDecayMaxPenalty = 18;
inline constexpr int kDecayRecallDropPerTrigger = 1;

// 弱词与技能升级阈值。调整后会明显改变题型推进路径。
// 说明：WeakWord 判定现在优先看 overdue_ratio 和 lapse_count；strength 阈值只作为辅助。
inline constexpr int kWeakWordStrengthThreshold = 45;
inline constexpr int kWeakWordLapseThreshold = 3;
inline constexpr float kColdStartNewWordRatioThreshold = 0.5f;
inline constexpr float kIntensiveReviewWeakOrDueRatioThreshold = 0.5f;
inline constexpr int kRecallToOutputThreshold = 3;
inline constexpr int kOutputToAdvancedSpeakThreshold = 3;

// 单词 completion 计算权重。
inline constexpr float kWordCompletionRecognitionWeight = 0.3f;
inline constexpr float kWordCompletionRecallWeight = 0.4f;
inline constexpr float kWordCompletionOutputWeight = 0.3f;

// 调度器词优先级分值。
// 说明：这些值仍然有效，但现在只是统一 priority 的基准分，不再代表硬分桶。
inline constexpr int kSchedulerPriorityWeakWord = 100;
inline constexpr int kSchedulerPriorityDueReview = 80;
inline constexpr int kSchedulerPriorityReviewBacklog = 60;
inline constexpr int kSchedulerPriorityNewWord = 50;
inline constexpr int kSchedulerIntensiveReviewBonus = 10;
inline constexpr int kSchedulerSeenPenaltyPerShow = 10;

// -----------------------------------------------------------------------------
// Session/通关节奏：这是日常联调时最值得优先调整的一组。
// 说明：如果你的目标是“更容易调试/更快验证”，优先改这一组，不要先碰高级画像公式。
// -----------------------------------------------------------------------------

// user.json 缺省值。适合联调时直接调节任务强度。
inline constexpr int kDefaultDailyNewWordTarget = 10;
inline constexpr int kDefaultDailyReviewWordTarget = 5;
inline constexpr int kDefaultDailyTotalTarget = 15;
// 兼容旧命名的别名。当前 user.json 正式字段请使用 daily_new_word_target /
// daily_review_word_target / daily_total_target，下面两个 total alias 只用于保留旧代码阅读习惯。
inline constexpr int kDefaultTodayMissionCount = kDefaultDailyTotalTarget;
inline constexpr int kDefaultTodayPracticeWordCount = kDefaultDailyTotalTarget;

// 每轮默认练习词数。仅在未从 user.json 提供覆盖时使用。
inline constexpr int kDefaultRoundWordTarget = 15;

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

// 口语题最大失败重试次数。
inline constexpr int kMaxSpeakRetryCount = 3;

// 题目池初次预热和增量扩池的 seed 数量。
inline constexpr std::size_t kInitialQuestionSeedWarmupCount = 4;
inline constexpr std::size_t kIncrementalQuestionSeedWarmupCount = 2;

}  // namespace word_practice::config