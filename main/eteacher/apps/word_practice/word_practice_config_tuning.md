# Word Practice Config Tuning Guide

本文的目标不是“把所有参数解释一遍”，而是把真正值得你日常调试的少数旋钮收敛出来。

当前模型原理可以先记成一句话：

- 练习负载主要由 Session/batch 参数控制
- 题型推进主要由 recall/output 阈值和 gain 选择控制
- 复习压力主要由 `effective_interval + overdue_ratio + lapse_count` 控制
- `strength` 现在更像 stage 和长期画像的辅助变量，不再是调度主因子
- `persistent_boost` 仍保留在数据结构里兼容旧数据，但已经不是一等调参入口

如果你只是想“更容易使用和调试”，先只用下面的核心旋钮，不要一开始就碰全部参数。

## 0. 12 个核心旋钮

这 12 个参数覆盖了大多数日常联调场景：

- 任务负载：`kDefaultDailyNewWordTarget`、`kDefaultDailyReviewWordTarget`、`kDefaultDailyTotalTarget`、`kBatchMaxSlots`
- 通关节奏：`kSessionMinimumQuestionFloor`、`kSessionRequiredCompletedWordsRatio`、`kSessionSuccessAccuracyThreshold`
- 新词推进：`kColdStartDesiredNewMin`、`kColdStartDesiredNewMax`、`kNormalDesiredNewMin`
- 复习加强：`kIntensiveDesiredWeakMin`、`kReviewIntervalBaseSec`、`kDecayPenaltyPerOverdueDay`

建议用法：

- 想更快看到完整闭环：先调前 6 个
- 想改变“新词多还是复习多”：再调中间 4 个
- 想改变“逾期词拉回速度”：最后再调后 2 个

## 1. 推荐日常调节

这一组最适合联调、试错、压测通关速度。优先改这些，不要先碰长期画像公式。

### 1.1 任务量与批次大小

- `kDefaultDailyNewWordTarget`
- `kDefaultDailyReviewWordTarget`
- `kDefaultDailyTotalTarget`
- `kDefaultRoundWordTarget`
- `kBatchMinSlots`
- `kBatchMaxSlots`
- `kMinimumDesiredReviewSlots`
- `kMinimumActiveProfileWords`

作用：

- 控制一轮大概要练多少词
- 控制新词、复习词、弱词是否更容易进入当前批次
- 决定首轮启动和后续轮转的密度

建议：

- 想加快通关：先减小 `kBatchMaxSlots`、`kDefaultDailyTotalTarget`，再看是否降低 `kMinimumActiveProfileWords`
- 想增加覆盖：先提高 `kBatchMaxSlots`，不要先调评分公式
- 想做功能验证而不是体验调优：优先把 `kDefaultDailyTotalTarget` 和 `kBatchMaxSlots` 压小，这样最容易快速跑完一轮

### 1.2 新词/弱词配比

- `kColdStartDesiredNewMin`
- `kColdStartDesiredNewMax`
- `kColdStartDesiredNewDivisor`
- `kNormalDesiredNewMin`
- `kNormalDesiredNewMax`
- `kNormalDesiredNewDivisor`
- `kNormalDesiredWeakMin`
- `kNormalDesiredWeakMax`
- `kNormalDesiredWeakDivisor`
- `kIntensiveDesiredWeakMin`
- `kIntensiveDesiredWeakMax`
- `kIntensiveDesiredWeakDivisor`

作用：

- 控制新词和弱词在每轮出现的比例
- 直接影响“感觉太难/太慢/总在复习”的主观体验

建议：

- 要让新词推进更快：提高 `ColdStart` 和 `Normal` 的 new min/max
- 要加强复习：提高 `Intensive` 的 weak min/max
- 如果你只想微调，不要同时改 min/max/divisor，先只改 min/max，保持 divisor 不动

### 1.3 Session 通关门槛

- `kBatchCompletionRatio`
- `kSessionSuccessCompletionThreshold`
- `kSessionSuccessAccuracyThreshold`
- `kSessionRequiredCompletedWordsRatio`
- `kSessionMinimumQuestionFloor`
- `kSessionMinimumQuestionSoftCap`
- `kMaxSpeakRetryCount`
- `kInitialQuestionSeedWarmupCount`
- `kIncrementalQuestionSeedWarmupCount`

作用：

- 控制一轮什么时候算结束
- 控制是否容易“通过”或“卡在最后几题”
- 控制首屏准备速度和后续扩题速度

建议：

- 想更快通关：先降低 `kSessionRequiredCompletedWordsRatio` 和 `kSessionMinimumQuestionFloor`
- 想减少进入做题页后的首题等待：先减小 `kInitialQuestionSeedWarmupCount`
- 想让结果更稳定：优先固定 `kSessionSuccessAccuracyThreshold`，不要和完成率阈值一起频繁来回改

说明：

- 进入首页的等待时间，当前主要受“首页选词”影响，不再受 `kInitialQuestionSeedWarmupCount` 影响。
- `kInitialQuestionSeedWarmupCount` 只影响从首页按下 Start 后，首题候选池准备要花多久。

## 2. 等级/阶段参数

这一组影响首页等级显示、掌握状态和技能升级节奏。可以调，但不要高频来回改。

### 2.1 阶段与掌握判定

- `kUiStageStep`
- `kUiStageMin`
- `kUiStageMax`
- `kMasteredUiStageFloor`
- `kMasteredMinRecallScore`
- `kMasteredMinOutputScore`
- `kMasteredMinStrength`
- `kMasteredMaxLapseCount`

作用：

- 控制 `strength` 映射到 stage 的方式
- 控制一个词什么时候被认为“已掌握”

### 2.2 等级增长速度

- `kDefaultStageLevelupCount`

作用：

- 控制每个 stage 的 level 增长速度
- 数值越大，升级越慢

建议：

- 你要调首页“升级快慢”，优先只改这一项，不要碰画像公式

### 2.3 技能升级阈值

- `kRecallToOutputThreshold`
- `kOutputToAdvancedSpeakThreshold`
- `kWeakWordStrengthThreshold`
- `kWeakWordLapseThreshold`

作用：

- 决定一个词从 Recall 题转到 Output，再转到 AdvancedSpeak 的时机
- 决定哪些词更容易被打回弱词

建议：

- 如果现象是“题型突然变难”，优先改 `kRecallToOutputThreshold`
- 如果现象是“弱词回流不够明显”，优先改 `kWeakWordLapseThreshold`，不要先去动 interval 公式

## 3. 高级画像与复习公式

这一组会改变长期学习轨迹、复习间隔和强度分布。除非你在做系统级调参，否则不建议先动。

### 3.1 正误分数与响应时间

- `kRecognitionSkillWeight`
- `kRecallSkillWeight`
- `kOutputSkillWeight`
- `kAdvancedSpeakSkillWeight`
- `kFastResponseThresholdMs`
- `kMediumResponseThresholdMs`
- `kSlowResponseThresholdMs`
- `kFastResponseConfidence`
- `kMediumResponseConfidence`
- `kSlowResponseConfidence`
- `kVerySlowResponseConfidence`
- `kOutputErrorSeverityBonus`
- `kRecallErrorSeverityBonus`
- `kSlowErrorSeverityThresholdMs`
- `kSlowErrorSeverityBonus`
- `kCorrectStrengthBaseDelta`
- `kPersistentBoostDecayOnCorrect`
- `kWrongStrengthBasePenalty`
- `kCorrectStrengthMinDelta`
- `kWrongStrengthMinPenalty`
- `kPersistentBoostTriggerLapseCount`
- `kPersistentBoostTriggeredValue`

### 3.2 初始画像与复习间隔

- `kDefaultNewWordStrength`
- `kDefaultReviewStrength`
- `kDefaultReviewRecallScore`
- `kMinReviewIntervalSec`
- `kMaxReviewIntervalSec`
- `kReviewIntervalBaseSec`
- `kReviewEvidenceFactorPerPoint`
- `kReviewLapseFactorPenaltyPerLapse`
- `kDecayPenaltyPerOverdueDay`
- `kDecayMinPenalty`

- `kDecayMaxPenalty`
- `kDecayRecallDropPerTrigger`
- `kColdStartNewWordRatioThreshold`
- `kIntensiveReviewWeakOrDueRatioThreshold`
- `kSchedulerPriorityWeakWord`
- `kSchedulerPriorityDueReview`
- `kSchedulerPriorityReviewBacklog`
- `kSchedulerPriorityNewWord`
- `kSchedulerSeenPenaltyPerShow`

说明：

- 当前代码已经引入 `effective_interval` 和 `overdue_ratio`，调度、弱词归类、逾期衰减优先看相对逾期程度
- `strength` 仍会影响间隔与 UI stage，但已经不是调度主因子
- `persistent_boost` 仍保留在数据结构里以兼容旧数据，但不再是优先级和学习模式判断的一等信号
- 复习间隔里的 `strength` 基线、strength 放大系数、evidence 基线、lapse floor 已经内化到实现，默认不再作为公开调参项
- `kReviewPersistentBoostFactor`、`kDecayPersistentBoostDiscount`、`kDecayRecallDropStartOverdueDays` 已退出当前主模型，不建议继续围绕它们调参

原理：

- `kReviewIntervalBaseSec` 决定“默认多久回看一次”
- `kReviewEvidenceFactorPerPoint` 决定 recall/output 证据每多 1 点，interval 拉长多少
- `kReviewLapseFactorPenaltyPerLapse` 决定错误历史会把 interval 压短多少
- `kDecayPenaltyPerOverdueDay` 决定逾期后强度回落有多快
- `kSchedulerPriorityDueReview` 与 `kSchedulerSeenPenaltyPerShow` 决定 overdue 词会被多积极地再次拉起

建议：

- 这些参数建议一次只改一个主题，例如“只改复习间隔”或“只改答错惩罚”
- 改完至少观察一个完整 round 和一批 profile 的演化日志
- 如果你是在查 bug，不是做模型实验，这一组通常先不要碰

## 3.3 先建立数值直觉

如果你不知道“改大 1 点会发生什么”，先记下面这组近似值。它们来自当前默认配置：

- Recognition 快速答对：`+8` 左右的 strength
- Recall 正常答对：`+11` 左右的 strength
- Output 正常答对：`+14` 左右的 strength
- Recognition 答错：`-6` 左右的 strength
- Recall 答错：`-11` 左右的 strength
- Output 慢答错：`-18` 左右的 strength

换句话说：

- `kCorrectStrengthBaseDelta` 决定“答对一题整体加多少”
- `kWrongStrengthBasePenalty` 决定“答错一题整体减多少”
- `kRecognitionSkillWeight / kRecallSkillWeight / kOutputSkillWeight` 决定不同技能相对轻重
- `kFastResponseConfidence` 和 `kSlowErrorSeverityBonus` 决定快答慢答带来的附加放大倍数

如果你只是觉得“每题变化太大”或者“每题变化太小”，优先先改 base delta / base penalty，不要第一步就改一堆 weight。

## 3.4 先看哪些日志

当前代码里最值得看的是这几类日志：

1. `startup timing home_preview_selection ...`
	用来看进入首页前，首页选词实际花了多久。
2. `startup timing load_question_pool ...`
	用来看从首页进入做题前，完整建池到底慢在选词、profile、seed 还是 batch。
3. `apply attempt factors ...`
	直接告诉你这题用了哪个 skill、当前 weight 和 confidence/severity 是多少，最终带来了多少 `strength_delta / recall_delta / output_delta`，以及下次复习间隔是多少秒。
4. `attempt result ...`
	用来看一次答题对 batch 进度、coverage 和 session 门槛的影响。

推荐做法：

1. 先打一轮日志样本，不要先改参数。
2. 看 5 到 10 题后，判断是“每题变化过大/过小”，还是“通过门槛过高/过低”。
3. 前者改画像参数，后者改 Session/batch 参数，不要两组一起改。

## 4. 不需要修改

这些值通常不是调试瓶颈。它们可以改，但常规联调不需要碰。

- `kLevelIconCellSize`
- `kLevelIconMaxPerRow`
- `kLevelIconMaxCount`
- `kNewWordRequiredShown`
- `kNewWordRequiredAnyCorrect`
- `kNewWordRequiredRecognitionCorrect`
- `kNewWordRequiredRecallCorrect`
- `kNewWordRequiredOutputCorrect`
- `kReviewWordRequiredShown`
- `kReviewWordRequiredAnyCorrect`
- `kReviewWordRequiredRecognitionCorrect`
- `kReviewWordRequiredRecallCorrect`
- `kReviewWordRequiredOutputCorrect`
- `kWeakWordRequiredShown`
- `kWeakWordRequiredAnyCorrect`
- `kWeakWordRequiredRecognitionCorrect`
- `kWeakWordRequiredRecallCorrect`
- `kWeakWordRequiredOutputCorrect`

原因：

- 它们更像完成规则或展示细节
- 一旦改动，往往要连同 SessionEvaluator 和交互预期一起重测

## 5. 固定不变

这一组属于协议、数据契约或 UI 约束，默认视为常量，不应在调试中随意修改。

- `kQuestionTypeImageChoice` 到 `kQuestionTypeAudioMeaningChoice`
- `kImageChoiceOptionCount`
- `kStandardChoiceOptionCount`
- `kScoreMax`
- `kEvidenceMax`

原因：

- 题型编号已经和调度、UI、日志、持久化语义绑定
- 题型 1 当前就是 3 图题，题型 2/3/11/12 当前就是固定 4 选项协议
- `kScoreMax` 和 `kEvidenceMax` 是很多 clamp 和 completion 公式的边界

## 6. 现象到参数的快速映射

- 现象：一轮太长，验证太慢
	先改：`kDefaultDailyTotalTarget`、`kBatchMaxSlots`、`kSessionMinimumQuestionFloor`
- 现象：新词推进太慢
	先改：`kColdStartDesiredNewMin`、`kColdStartDesiredNewMax`、`kNormalDesiredNewMin`
- 现象：总在复习，像出不去
	先改：`kIntensiveDesiredWeakMin`、`kReviewIntervalBaseSec`
- 现象：弱词回流不明显
	先改：`kWeakWordLapseThreshold`、`kDecayPenaltyPerOverdueDay`
- 现象：题型升级太突然
	先改：`kRecallToOutputThreshold`、`kOutputToAdvancedSpeakThreshold`
- 现象：明明结束了，但结果太苛刻
	先改：`kSessionRequiredCompletedWordsRatio`、`kSessionSuccessAccuracyThreshold`

## 7. 建议调试流程

如果你现在的目标是“更容易调试和验证”，建议按下面顺序，不要跨层同时乱改：

1. 先只调 `kDefaultDailyNewWordTarget`、`kDefaultDailyReviewWordTarget`、`kDefaultDailyTotalTarget`、`kBatchMaxSlots`
	当前更推荐对应到正式命名：`kDefaultDailyNewWordTarget`、`kDefaultDailyReviewWordTarget`、`kDefaultDailyTotalTarget`、`kBatchMaxSlots`
2. 再调 `kSessionMinimumQuestionFloor`、`kSessionRequiredCompletedWordsRatio`、`kSessionSuccessAccuracyThreshold`
3. 如果觉得升级太慢，再只调 `kDefaultStageLevelupCount`
4. 只有在题型推进明显不合理时，再碰 `kRecallToOutputThreshold`、`kOutputToAdvancedSpeakThreshold`
5. 最后才碰复习间隔、强度增减、衰减惩罚这一组高级画像公式

推荐操作法：

1. 一次只改 1 到 3 个同层参数
2. 每改完跑一个完整 round，再看日志和体感，不要中途连改两层
3. 把“现象 -> 改了哪几个参数 -> 结果”记到一张表里，否则很快失去可解释性