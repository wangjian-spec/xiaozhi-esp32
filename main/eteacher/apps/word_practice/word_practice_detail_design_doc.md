# Word Practice 详细设计说明

## 1. 文档定位

本文档描述 `main/eteacher/apps/word_practice` 当前代码的真实实现，用于开发、联调、问题定位与后续重构评估。

本文档的要求是“完整正式版”，因此只保留当前实现，不保留历史方案、过时字段解释或补丁式增量说明。若文档与代码冲突，以代码为准，并应同步修正文档。

覆盖文件：

- `word_practice.h/.cc`
- `word_practice_types.h`
- `word_practice_flow_controller.h/.cc`
- `word_practice_selection_module.h/.cc`
- `word_practice_question_seed_module.h/.cc`
- `word_practice_learning_module.h/.cc`
- `word_practice_quiz_module.h/.cc`
- `word_practice_session_module.h/.cc`
- `word_practice_session_evaluator.h/.cc`
- `word_practice_result_module.h/.cc`
- `word_practice_db_utils.h`
- `word_practice_time_utils.h/.cc`

一句话概括：

`word_practice` 是一个“按当前阶段选词 -> 即时装载词典 seed -> 按学习画像和批次进度调度下一题 -> 即时生成题目 -> 作答后立刻更新画像与统计 -> 满足 Session 门槛后结算”的动态练习系统，而不是预先生成固定题单的静态题库系统。

## 2. 总体架构

### 2.1 总控类

`WordPracticeApp` 是模块编排中心，负责：

- 进入和退出应用
- 读取和保存 `/sdcard/user/user.json`
- 加载 UI、切换首页/练习页/结算页
- 启动一轮练习
- 驱动选词、seed 预热、题目调度、即时出题
- 处理用户按键和 ASR 结果
- 将答题结果持久化到 user.db 和 user.json

### 2.2 模块分工

| 模块 | 主要类 | 职责 |
|---|---|---|
| 流程配置 | `PracticeFlowController` | 一轮练习的目标词数、口语题失败重试上限 |
| 选词 | `SelectionModule` | 补种 profile、维护活跃词池、按教材与复习状态选词 |
| seed 装载 | `QuestionSeedModule` | 从阶段词典数据库读取词、词义、例句、图片、音频信息 |
| 题目生成 | `QuestionSeedModule` | 按 `word_id + question_type` 即时生成 `QuestionData` |
| 题目解析 | `QuizModule` | 将题目 JSON 解析为 `ChoiceState` |
| 学习画像 | `WordMasteryDao` | 维护 `word_learning_profile` 与 `word_practice_history` |
| 批次规划 | `LearningBatchPlanner` | 将本轮选中词划分为新词、复习词、弱词 |
| 批次进度 | `BatchProgressTracker` | 跟踪每个词在当前 Session 的展示次数、checkpoint 与完成状态 |
| 调度器 | `QuestionScheduler` | 决定下一题训练哪个词、哪个技能、哪种题型 |
| 会话计数 | `SessionModule` | 记录正确数、错误数、跳过数、分数、答题数 |
| 会话评估 | `SessionEvaluator` | 统一计算 `finished`、`success`、`completion`、`accuracy` |
| 结果统计 | `ResultModule` / `UserProgressDao` | 更新 learned、daily stats、daily progress、runtime_state |

### 2.3 当前实现的四个核心概念

1. `Session`
指用户一次完整练习。从首页进入，到满足结束条件或题目耗尽为止。

2. `LearningBatch`
指本轮真正参与调度的目标词集合。它是从 `selected_words` 中进一步筛选和分组得到的，不等于词库全量，也不等于用户全部未掌握词。

3. `WordMasteryProfile`
指某用户对某词的长期学习画像。当前主状态是 `strength + next_review_at`，技能证据由 `recall_score` 和 `output_score` 承担。

4. `BatchProgressTracker`
指某词在当前 Session 内的短期 checkpoint 状态。它决定“这个词本轮是否完成”，也决定 Session 的 skill coverage 是否达标。

## 3. 运行流程

### 3.1 进入应用

进入应用后：

1. 初始化 UI 与场景。
2. 调用 `LoadUserJson()` 读取用户配置。
3. 调用 `SyncUserProgressState()` 同步当前掌握词数、等级和今日进度。
4. 展示首页预览。

### 3.2 启动一轮练习

`StartPracticeRound()` 的主流程：

1. 清理上一轮运行态。
2. 从 `PracticeFlowController::BuildRoundPlan()` 读取本轮目标词数。
3. 调用 `LoadQuestionPool()` 建立当前题目候选池。
4. 调用 `PickNextQuestion()` 获取第一题。
5. 调用 `CommitScheduledQuestion()` 即时生成题目并展示。

### 3.3 `LoadQuestionPool()` 的完整步骤

`LoadQuestionPool()` 是本系统的启动核心，顺序如下：

1. 清空 `selected_words_`、`question_seed_pool_`、`available_question_types_by_word_`、`mastery_profiles_`、`learning_batch_` 等缓存。
2. 解析 `CurrentStageIndex()`，得到当前 `stage_index`。
3. 映射 `current_textbook_name_`，例如 `primary`、`middle`、`high`。
4. 读取 `user_json_.stage_new_word_cursor[stage_index - 1]`，作为新词游标。
5. 调用 `SelectionModule::SelectWordsFromVocabulary(...)` 选出本轮 `selected_words`，并回写新的新词游标。
6. 调用 `mastery_dao_.LoadProfiles(...)` 读取这些词的画像。
7. 调用 `mastery_dao_.ApplyDueDecayIfNeeded(...)` 对过期过久未练的词做衰减。
8. 调用 `question_seed_module_.DetermineLearningMode(...)` 判定本轮模式。
9. 调用 `WarmQuestionCandidates(...)` 逐词加载 seed，并立即生成 `available_question_types_by_word_`。
10. 首次启动只预热前 `4` 个词；若题型仍为空，则按每次 `2` 个词继续扩池。
11. 调用 `batch_planner_.Build(...)` 构造 `LearningBatch`。
12. 调用 `batch_progress_tracker_.Reset(...)` 重置本轮短期进度。
13. 调用 `question_scheduler_.Reset()` 重置调度器状态。
14. 构造本轮目标文案 `current_round_goal_text_`。

### 3.4 单题运行链路

一题从调度到持久化的链路如下：

1. `PickNextQuestion()`
   - 从 `learning_batch_` 中找尚未完成的词。
   - 用 `QuestionScheduler` 决定词、目标技能、题型。

2. `CommitScheduledQuestion()`
   - 用 `QuestionSeedModule::GenerateQuestionOnDemand(...)` 即时生成 `QuestionData`。

3. `PresentCurrentQuestion()`
   - 用 `QuizModule::Generate(...)` 生成 `ChoiceState`。
   - 刷新 UI。
   - 记录当前题展示时间。
   - 调用 `batch_progress_tracker_.MarkPresented(word_id)`。

4. 用户答题
   - 不同题型由不同按钮处理函数接收答案或跳过。

5. `SaveAnswerStats()`
   - 进入 user.db 事务。
   - 调用 `RecordCurrentAttempt(...)` 更新画像和批次进度。
   - 调用 `UpdateDailyProgress(...)` 更新当日完成词数与进度。
   - 调用 `ResultModule::ProgressDao().SaveAnswerStats(...)` 更新 learned 和 daily stats。
   - 若本轮完成且尚未记过回合结果，再调用 `RecordRoundCompletion(...)` 更新 `word_practice_runtime_state`。
   - 事务提交后调用 `SyncUserProgressState()` 与 `SaveUserJson()` 同步首页运行态。

6. `UpdateSessionState()`
   - 调用 `EvaluateSession()` 判断本轮是否应结束。

## 4. 用户配置与运行态

### 4.1 `user.json` 路径

固定路径：

```text
/sdcard/user/user.json
```

### 4.2 读取逻辑

`LoadUserJson()` 会读取：

- `users.user_id`
- `users.name`
- `users.current_stage`
- `users.level`
- `learning_preferences.enable_read_questions`
- `learning_preferences.today_mission_count`
- `learning_preferences.today_practice_word`
- `settings.enable_read_questions`
- `practice_stats.continuous_days`
- `practice_stats.last_practice_date`
- `stage_levelup_count`
- `stage_words_quantity`
- `stage_new_word_cursor`

### 4.3 保存结构

`SaveUserJson()` 当前输出的核心结构为：

```json
{
  "users": {
    "user_id": 0,
    "name": "student",
    "current_stage": "stage1",
    "level": 0
  },
  "devices": {
    "device_id": "",
    "firmware": ""
  },
  "settings": {
    "enable_read_questions": true
  },
  "learning_preferences": {
    "enable_read_questions": true,
    "today_mission_count": 15,
    "today_practice_word": 15
  },
  "practice_stats": {
    "continuous_days": 1,
    "last_practice_date": ""
  },
  "stage_levelup_count": [10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 48],
  "stage_words_quantity": [0, ...],
  "stage_new_word_cursor": [0, ...]
}
```

### 4.4 阶段映射

`StageNumberToTag(...)` 当前映射：

| stage_index | textbook_name |
|---|---|
| 1 | `primary` |
| 2 | `middle` |
| 3 | `high` |
| 4 | `cet4` |
| 5 | `cet6` |

未映射的阶段不会生成教材 tag；需要教材名时，当前 `WordPracticeApp` 调用方统一按 `default` 归一化。

## 5. 核心数据结构

### 5.1 `QuestionData`

```cpp
struct QuestionData {
  int id = 0;
  int type = 1;
  std::string stage;
  int difficulty = 1;
  std::string content_json;
  std::string answer;
};
```

说明：

- `id` 对按需生成题通常采用 `(word_id << 8) | question_type`。
- `type` 是题型编号，当前主要使用 1 到 12。
- `content_json` 是 UI 解析的直接输入。

### 5.2 `ChoiceState`

`QuizModule` 将 `QuestionData.content_json` 解析成 `ChoiceState`，关键字段包括：

- `prompt`
- `textbook_name`
- `audio_filename`
- `source_word`
- `source_word_id`
- `option_keys` / `options` / `option_images`
- `hints`
- `pair_left` / `pair_right`
- `expected`

### 5.3 `SelectedWord`

```cpp
struct SelectedWord {
  int word_id = 0;
  std::string word;
  std::string image;
  bool is_review = false;
};
```

`is_review` 表示该词在当前用户下已有练习历史，而不是静态词库标签。

### 5.4 `VocabularySeed`

表示词典素材，主要字段包括：

- `word_id`
- `meaning_id`
- `example_id`
- `word`
- `meaning_zh` / `meaning_en`
- `image`
- `example_en` / `example_zh`
- `selection_zh` / `selection_en`
- `stage`
- `is_review`

### 5.5 `WordMasteryProfile`

当前画像模型：

```cpp
struct WordMasteryProfile {
  int user_id = 0;
  int word_id = 0;
  std::string textbook_name;
  int stage = 0;
  int strength = 0;
  int recall_score = 0;
  int output_score = 0;
  int64_t next_review_at = 0;
  int lapse_count = 0;
  int64_t last_practiced_at = 0;
  int64_t last_decay_at = 0;
  int64_t last_reviewed_at = 0;
  int last_response_time_ms = 0;
  int persistent_boost = 0;
  bool mastered = false;
};
```

字段含义：

- `strength`：整体掌握强度，范围 0 到 100。
- `recall_score`：回忆能力证据，范围 0 到 5。
- `output_score`：输出能力证据，范围 0 到 5。
- `next_review_at`：下次建议复习时间。
- `lapse_count`：遗忘或错误累计次数。
- `persistent_boost`：长期弱词强化权重，非 0 表示近期应被优先拉起。
- `stage`：UI 展示阶段，由 `strength` 与 `mastered` 派生。

### 5.6 `LearningMode`

当前显式模式：

- `ColdStart`
- `Normal`
- `IntensiveReview`

该模式同时影响：

- 批次规划
- 题型放行策略
- Session 的 output coverage 门槛
- 调度器优先级

### 5.7 `LearningBatch` 与 `BatchWordPlan`

`LearningBatch` 表示本轮目标词集合，包含：

- `items`
- `planned_new_words`
- `planned_review_words`
- `planned_weak_words`

`BatchWordPlan` 表示批次中一个词的计划，包含：

- `selected_word`
- `kind`：`NewWord` / `ReviewWord` / `WeakWord`
- `progress_state`
- `recognition_done`
- `recall_done`
- `shown_count`
- `correct_count`

### 5.8 `BatchProgressSummary`

`BatchProgressTracker::BuildSummary()` 输出：

- 总词数与完成词数
- 新词 / 复习词 / 弱词的总数与完成数
- `skill_coverage`
- `recognition_coverage_ok`
- `recall_coverage_ok`
- `output_coverage_ok`
- `skill_coverage_ok`
- `batch_completed`

## 6. 数据库模型

### 6.1 用户画像表 `word_learning_profile`

由 `WordMasteryDao::EnsureTables()` 创建，当前建表 SQL：

```sql
CREATE TABLE IF NOT EXISTS word_learning_profile (
  user_id INTEGER NOT NULL,
  word_id INTEGER NOT NULL,
  textbook_name TEXT NOT NULL,
  stage INTEGER DEFAULT 0,
  strength INTEGER DEFAULT 0,
  recall_score INTEGER DEFAULT 0,
  output_score INTEGER DEFAULT 0,
  next_review_at INTEGER DEFAULT 0,
  lapse_count INTEGER DEFAULT 0,
  last_practiced_at INTEGER DEFAULT 0,
  last_decay_at INTEGER DEFAULT 0,
  last_reviewed_at INTEGER DEFAULT 0,
  last_response_time_ms INTEGER DEFAULT 0,
  persistent_boost INTEGER DEFAULT 0,
  mastered INTEGER DEFAULT 0,
  PRIMARY KEY(user_id, word_id, textbook_name)
);
```

说明：

- 当前运行时主逻辑以 `strength` 为准。
- `EnsureSchemaVersion()` 当前会逐列补齐运行时依赖字段，并把 `PRAGMA user_version` 升到 4。

索引：

```sql
CREATE INDEX IF NOT EXISTS idx_word_learning_profile_user_next_review
ON word_learning_profile(user_id, next_review_at);
```

### 6.2 历史表 `word_practice_history`

```sql
CREATE TABLE IF NOT EXISTS word_practice_history (
  id INTEGER PRIMARY KEY,
  user_id INTEGER NOT NULL,
  word_id INTEGER NOT NULL,
  question_type INTEGER DEFAULT 0,
  target_skill TEXT,
  review_type TEXT,
  rating INTEGER DEFAULT 0,
  response_time INTEGER DEFAULT 0,
  correct INTEGER DEFAULT 0,
  question_reason TEXT,
  practiced_at INTEGER DEFAULT 0
);
```

每次 `ApplyAttempt()` 成功后，都会记录一条历史。`question_reason` 存的是 `ScheduledQuestion.reason_text` 的 JSON 串。

### 6.3 统计表

由 `UserProgressDao::EnsureStatsTables()` 创建：

1. `learned`
   - 按词累计正确数、错误数、最后出现时间。

2. `word_practice_stats_daily`
   - 记录某天某教材的总题数、正确数、错误数、通过轮次、失败轮次。

3. `word_practice_daily_progress`
   - 记录某天某教材的完成词数、目标词数、进度百分比。

4. `word_practice_runtime_state`
   - 记录某教材已完成轮次、上一轮是否通过、上一轮时间。

## 7. 选词机制

### 7.1 设计目标

`SelectionModule::SelectWordsFromVocabulary(...)` 的目标是：

- 确保当前用户在当前教材下有足够多的活跃 profile 可供练习。
- 优先出到期复习词。
- 再补入尚未练过的新词。
- 最后用未到期但仍未掌握的 backlog 复习词补齐。

### 7.2 profile 补种

选词前会做以下事情：

1. 打开 user.db。
2. 调用 `WordMasteryDao::EnsureTables()` 确保画像表和迁移完成。
3. `ATTACH` 当前阶段词典数据库为 `dictdb`。
4. 统计当前教材下到期复习词数量。
5. 若活跃词少于 `kMinimumActiveProfileWords = 20`，则从词典 `word` 表按 `word_id` 游标补写新的 `word_learning_profile`。
6. 补种固定放在显式事务里执行；若 `BEGIN IMMEDIATE` 或 `COMMIT` 失败，则本次补种终止并回滚。
7. 由于设备端 SQLite 连接在 `ATTACH dictdb` 后直接提交 user.db 写事务时，曾出现 `COMMIT -> unable to open database file`，当前实现先收集候选 `word_id`，再 `DETACH dictdb`，只对主库 `user.db` 执行补种提交，提交后再重新附加 `dictdb` 继续后续查询。

补写的新 profile 初始值为：

- `stage = 1`
- `strength = 12`
- 其余能力证据与时间字段为 0

### 7.3 实际选词顺序

`AppendSelectedProfileWords(...)` 固定按以下顺序选词：

1. 到期复习词
   - `mastered = 0`
   - `last_practiced_at > 0`
   - `next_review_at <= now` 或为空

2. 新词
   - `mastered = 0`
   - `last_practiced_at = 0`

3. backlog 复习词
   - `mastered = 0`
   - `last_practiced_at > 0`
   - `next_review_at > now`

最后再回查词典数据库，把 `word` 和 `image` 回填到 `SelectedWord`。

## 8. Seed 装载与即时出题

### 8.1 seed 层目标

seed 层只负责“把一个词当前可出题所需的词典素材拉齐”，不负责决定本轮应该练哪个词。

### 8.2 当前 seed 加载路径

当前运行时不预先批量装载整轮 seed 池，而是由 `WarmQuestionCandidates(...)` 按 `selected_words_` 顺序逐词调用 `QuestionSeedModule::LoadVocabularySeedForWord(...)`。

单词级 seed 查询固定走两步：

1. 按 `word_id` 读取第一条 `word_meaning`
2. 按 `meaning_id` 读取第一条 `word_example`

因此当前日志会体现为多条：

- `single seed load word_id=...`
- `warm question candidates reason=... target=... loaded_now=...`

### 8.3 可用题型计算

`BuildAvailableQuestionTypesForSeed(...)` 会根据：

- seed 是否为复习词
- 当前 `LearningMode`
- profile 中的 `recall_score` / `output_score`
- 是否启用口语题
- 是否有图片、例句、音频和足够干扰项

动态计算该词当前能出的题型集合。

### 8.4 题型与技能映射

当前题型映射：

| 题型 | 技能定位 | 说明 |
|---|---|---|
| 1 | Recognition | 图片识别 |
| 2 | Recognition | 词义识别 |
| 3 | Recall | 词到义回忆 |
| 4 | Recall | 配对 |
| 5 | Output | 中文提示例句填词 |
| 6 | Output | 英文例句补全 |
| 7 | Output | 单词口语输出 |
| 8 | Recall | 听义或口头回忆 |
| 9 | AdvancedSpeak | 英文句子高级朗读 |
| 10 | AdvancedSpeak | 中文提示句朗读 |
| 11 | Recognition | 音频辅助识别 |
| 12 | Recognition | 音频辅助识别变体 |

### 8.5 即时出题

调度器只给出 `word_id + question_type`。真正的 `QuestionData` 在 `GenerateQuestionOnDemand(...)` 中即时构造。

优势：

- 不必预生成整轮题单。
- 可以随着 seed 增量装载逐步扩池。
- 同一词可根据当前画像和模式动态放行不同题型。

## 9. 学习模式与批次规划

### 9.1 学习模式判定

`DetermineLearningMode(...)` 当前规则：

1. 若 `selected_words` 为空，返回 `Normal`。
2. 统计：
   - 真实新词数 `real_new_word_count`
   - 已有 profile 的词数 `profile_count`
   - 弱词或到期词数 `weak_or_due_count`
3. 判定：
   - `profile_count == 0` 或 `real_new_word_ratio > 0.5` -> `ColdStart`
   - `weak_or_due_ratio > 0.5` -> `IntensiveReview`
   - 其他 -> `Normal`

其中“弱词或到期词”的条件是：

- `persistent_boost > 0`，或
- `lapse_count >= 3`，或
- `next_review_at <= now`

### 9.2 批次规划 `LearningBatchPlanner::Build()`

核心目标：

- 批次大小不追求覆盖所有选中词，而是保持在 8 到 10 个词的可控范围内。
- `ColdStart` 更多新词，`IntensiveReview` 更多弱词。

基本参数：

- `total_slots = min(10, max(8, selected_words.size()))`

词分类规则：

- `selected.is_review == false` -> `NewWord`
- 否则，若 `persistent_boost > 0` 或 `lapse_count >= 3` 或 `strength < 45` -> `WeakWord`
- 其他 -> `ReviewWord`

填充顺序：

1. 先放弱词
2. 再放新词
3. 再放复习词
4. 若仍未满，则按“复习词 -> 新词 -> 弱词”补齐到 `total_slots`

## 10. 批次进度与 Skill Coverage

### 10.1 单词完成规则

当前 `CompletionRule`：

- `NewWord`
  - 至少 1 次 Recognition 正确
  - 至少 1 次 Recall 正确

- `ReviewWord`
  - 至少 1 次任意正确

- `WeakWord`
  - `shown_count >= 2`
  - `any_correct_count >= 1`

### 10.2 展示与答题后的进度更新

展示题目时：

- `shown_count++`
- 状态从 `NotStarted` 进入 `InProgress`

答题后：

- 按技能更新 `recognition_done` / `recall_done` / `output_attempted`
- 若答对，再更新 `recognition_count` / `recall_count` / `output_count`
- 若满足该词的 `CompletionRule`，则状态变为 `Completed`

### 10.3 Session 的 skill coverage 门槛

`BuildSummary()` 当前规则：

- 新词必须覆盖 `recognition_done` 和 `recall_done`
- `ColdStart` 轮默认允许 `output_coverage_ok = true`
- 非 `ColdStart` 轮要求本轮至少有一个 `ReviewWord` 或 `WeakWord` 发生过 `output_attempted`

最终：

```text
skill_coverage_ok = recognition_coverage_ok && recall_coverage_ok && output_coverage_ok
```

这条规则用于防止用户只刷低阶识别题就提前通过一轮 Session。

## 11. 调度器设计

### 11.1 当前调度风格

当前 `QuestionScheduler` 使用确定性优先级，不再依赖复杂的短期错误加权、easy confirmation 或深层最近窗口惩罚。

### 11.2 词优先级

`ScheduleNext()` 的打分规则：

- `WeakWord` -> `+100`
- 到期复习词 -> `+80`
- `NewWord` -> `+50`
- 其他复习 backlog -> `+60`
- `IntensiveReview` 下非新词额外 `+10`
- `word_seen_count * 10` 作为重复展示惩罚

排序时若分数相同，则优先展示次数更少的词。

### 11.3 技能选择 `ChooseSkill()`

当前策略：

1. 对 `NewWord`：
   - 未过 Recognition checkpoint -> `Recognition`
   - 已识别但未过 Recall checkpoint -> `Recall`

2. 对非新词：
   - `profile == nullptr` -> `Recall`
   - `recall_score < 3` -> `Recall`
   - `output_score < 3` -> `Output`
   - 否则 -> `AdvancedSpeak`

### 11.4 题型选择 `FindQuestionForWord()`

当前技能偏好序列：

- `Recognition` -> `[1, 2, 11, 12]`
- `Recall` -> `[1, 3, 4, 8]`
- `Output` -> `[1, 5, 6, 7]`
- `AdvancedSpeak` -> `[9, 10]`

回退规则：

- 若 `AdvancedSpeak` 没有 9/10，则降为 `Output`
- 若 `Output` 没有 5/6/7，则降为 `Recall`

此外，如果同一词上一题的题型与当前候选题型相同，并且该技能有多个可选题型，则优先跳过这次重复，尽量轮换。

## 12. 学习画像更新规则

### 12.1 派生状态收敛

`SyncDerivedProfileState()` 会统一收敛：

- `strength` 到 0 到 100
- `recall_score` / `output_score` 到 0 到 5
- `persistent_boost` 到 0 到 100
- `mastered = (recall_score >= 3 && output_score >= 3 && strength >= 60 && lapse_count <= 3)`
- `stage = 1 + strength / 20`，范围 1 到 5；若已掌握但阶段低于 4，则至少提升到 4

### 12.2 正确作答

答对时：

- `strength += round(7.0 * skill_weight * confidence_factor)`，最少加 1
- `persistent_boost -= 5`
- Recall 正确：`recall_score++`
- Output / AdvancedSpeak 正确：`output_score++`
- 更新 `last_practiced_at`
- 更新 `last_reviewed_at`
- 更新 `last_response_time_ms`
- 重新计算 `next_review_at`

### 12.3 错误作答

答错时：

- `strength -= round(6.0 * skill_weight * severity)`，最少减 2
- `lapse_count++`
- 若 `lapse_count >= 3`，则 `persistent_boost = 30`
- Recall 错误：`recall_score--`
- Output / AdvancedSpeak 错误：`output_score--`
- 重新计算派生状态与 `next_review_at`

### 12.4 复习间隔

`NextReviewIntervalSec()` 当前由以下因素共同决定：

- 基础时间：`6h + strength * 900s`
- `strength_factor`
- `evidence_factor`（`recall_score + output_score`）
- `lapse_factor`
- `boost_factor`

最终被限制在：

- 最短 `1h`
- 最长 `21d`

### 12.5 到期衰减

`ApplyDueDecayIfNeeded()` 对逾期 profile 做遗忘衰减：

- 衰减值随逾期天数和 `lapse_count` 增大
- 若 `persistent_boost > 0`，会降低一部分惩罚
- 逾期超过 2 天且 `recall_score > 0` 时，额外降低 1 点 recall evidence

## 13. Session 结束与通过规则

### 13.1 `SessionModule`

`SessionModule` 只负责会话计数，包括：

- 正确数
- 错误数
- 跳过数
- 分数
- 已答题数
- 答题上限
- 是否被强制结束

它本身并不判断学习质量是否达标。

### 13.2 completion 计算

`SessionEvaluator::ComputeWordCompletion()` 当前公式：

```text
0.3 * strength/100 + 0.4 * recall_score/5 + 0.3 * output_score/5
```

### 13.3 `SessionEvaluator::Evaluate()`

当前门槛：

1. `pass_words`
   - 当前批次至少完成 80% 的词

2. `pass_minimum_questions`
   - 最少 4 题
   - 不超过 `PassTargetQuestions()`
   - 同时受已完成词数和目标词数影响

3. `pass_skill_coverage`
   - 来自 `BatchProgressSummary.skill_coverage_ok`

4. `pass_completion`
   - 平均 completion >= 0.8

5. `pass_accuracy`
   - 正确率 >= 0.7

完成条件：

```text
finished = finish_by_answer_limit || finish_by_progress_gate || scheduler_exhausted
```

其中：

```text
finish_by_progress_gate = pass_words && pass_minimum_questions && pass_skill_coverage
```

通过条件：

```text
success = finished && pass_words && pass_skill_coverage && pass_completion && pass_accuracy
```

这意味着：

- 即使答题上限已到，也可能 `finished=1` 但 `success=0`
- 即使调度器无题可出，也可能只结束、不通过

## 14. 答题后的持久化链路

### 14.1 `RecordCurrentAttempt()`

一次答题后的核心步骤：

1. 根据当前 `ScheduledQuestion` 找到目标 profile。
2. 构造 `QuestionAttemptRecord`。
3. 调用 `mastery_dao_.ApplyAttempt(db, profile, attempt)` 更新画像和历史。
4. 更新内存缓存中的画像。
5. 调用 `batch_progress_tracker_.MarkOutcome(...)` 更新当前批次短期进度。
6. 调用 `question_scheduler_.RecordResult(...)` 更新调度器去重与展示统计。
7. 更新本题反馈文案与错词列表。
8. 调用 `UpdateSessionState()` 重新评估当前 Session。

### 14.2 `SaveAnswerStats()`

`WordPracticeApp::SaveAnswerStats()` 在 user.db 事务中完成：

- `RecordCurrentAttempt(...)`
- `UserProgressDao::SaveAnswerStats(...)`
- 若本轮结束：
  - `UpdateDailyProgress(...)`
  - `RecordRoundCompletion(...)`
  - 更新 `practice_stats`
  - 更新 `today_progress_percent`
  - 回写 `user.json`

### 14.3 learned 与 daily stats

每次答题都会更新：

- `learned`
- `word_practice_stats_daily`

若本轮结束，还会更新：

- `word_practice_daily_progress`
- `word_practice_runtime_state`

## 15. 首页与结算页统计

### 15.1 首页

首页显示来自两部分：

1. `user.json`
   - 当前阶段
   - 当前等级
   - 今日任务配置

2. `UserProgressDao`
   - 当前教材的当日完成词数与进度百分比

`QueryMasteredWordCount()` 会直接统计 `word_learning_profile` 中已掌握的词数，用于刷新首页等级与掌握量显示。

### 15.2 结算页

`BuildSessionSummaryData()` 汇总：

- 用时
- 正确率
- 总题数 / 错题数 / 跳过数
- 新词数量与完成数
- 复习词和弱词数量与完成数
- 本轮前后的掌握词量
- 本日前后进度
- 是否升级
- 连续学习天数
- 本轮错词列表

## 16. 当前数据契约

### 16.1 `user.json`

当前版本只读取并写回正式结构中的这些字段：

- `users.user_id`
- `users.name`
- `users.current_stage`
- `users.level`
- `learning_preferences.enable_read_questions`
- `learning_preferences.today_mission_count`
- `learning_preferences.today_practice_word`
- `settings.enable_read_questions`
- `practice_stats.continuous_days`
- `practice_stats.last_practice_date`
- `stage_levelup_count`
- `stage_words_quantity`
- `stage_new_word_cursor`

### 16.2 `word_learning_profile`

当前运行时只依赖以下画像字段：

- `stage`
- `strength`
- `recall_score`
- `output_score`
- `next_review_at`
- `lapse_count`
- `last_practiced_at`
- `last_decay_at`
- `last_reviewed_at`
- `last_response_time_ms`
- `persistent_boost`
- `mastered`

`EnsureSchemaVersion()` 会确保这些运行时依赖字段存在，并把 `PRAGMA user_version` 升到 4。`SelectionModule` 在选词前保证 `EnsureTables()` 已完成；若当前教材下 `word_learning_profile` 为空，选词阶段会先补种至少 `20` 个新 profile，再从中选出本轮目标词。

## 17. 问题诊断建议

### 17.1 本次真实根因总结

这次“无法出题”并不是单一故障，而是一条串联故障链：

1. 设备端 SQLite 3.25.2 上，`PRAGMA integrity_check(1)` 返回了 `SQLITE_DONE + 空结果`，旧代码把它直接判成数据库损坏。
2. 即使 `user.db` 实际可读，也会被误判为 corrupt，导致 `DiscoverUserDataDbPath()` 早期返回空路径，选词阶段直接 `selection abort missing db`。
3. 修正发现逻辑后，又暴露出 `word_learning_profile` 的历史主键迁移问题。设备端环境对 `ALTER TABLE ... RENAME ...` 路径不稳定，导致 `EnsureTables()` 失败，选词仍无法继续。
4. 再修正迁移后，又暴露出一个只在 `ATTACH dictdb` 后写 user.db 时出现的问题：补种插入本身成功，但 `COMMIT` 失败为 `unable to open database file`，所以新 profile 没有真正落盘。
5. 选题链路打通后，首题展示阶段又因为一条调试日志向 `printf` 传递了不安全的字符串参数，触发 `LoadProhibited` 崩溃。

因此，本次真正的根因应归纳为：

- 不是词库 `stage_1.db` 空。
- 不是题型调度器本身没有候选题。
- 是设备端 SQLite/SD 卡环境与运行时代码之间存在多个兼容性边界，旧实现对这些边界缺少保护，导致链路在“库发现、schema 迁移、补种提交、展示日志”四层先后失败。

### 17.2 已验证的防坑约束

后续设计和实现必须遵守以下约束：

1. 不要把 `PRAGMA integrity_check(1)` 的“空结果”直接等价为数据库损坏。设备端 SQLite 版本可能与桌面版行为不同。
2. `DiscoverUserDataDbPath()` 允许在“文件缺失但路径合法”时返回创建目标路径；否则业务层会把“可创建”误判成“库不存在”。
3. `word_learning_profile` 的历史 schema 迁移不能依赖 `ALTER TABLE ... RENAME ...`。当前实现使用“备份表复制 -> 删除原表 -> 重建 -> 回拷”的方式避开设备端 rename 问题。
4. 主键是否已迁移完成，优先检查 `sqlite_master.sql` 中的建表定义，不要只依赖 `PRAGMA table_info` 结果做判断。
5. 不要在已有事务内部再次触发 schema 重建。`EnsureTables()` 允许被频繁调用，但迁移逻辑必须避免在 `autocommit=0` 时再次开启事务。
6. 当连接已 `ATTACH dictdb` 时，不要直接在同一连接上对 `user.db` 执行补种事务提交。当前稳定路径是：先查候选 `word_id`，再 `DETACH dictdb`，提交主库写事务，最后重新 `ATTACH`。
7. 设备端 `esp_log_write` 中不要把可空字符串直接作为 `%s` 参数传入，也不要假定所有 `printf` 长整型格式在目标环境上都稳定可读。调试日志应优先输出长度、布尔状态、ID 等纯值字段。
8. `user.json save failed` 目前不是“无法出题”的根因，但它说明 `/sdcard/user/` 的普通文件写入链路仍有独立问题，不能因为题目已能展示就忽略该风险。

出现“无法出题”时，优先看以下日志点：

1. `load question pool start ...`
2. `selection db paths user=... words=...`
3. `selection profile inventory textbook=... total=... due=... fresh=... backlog=... mastered=...`
4. `selection topup begin transaction failed ...` 或 `selection topup commit failed ...`
5. `selected words summary total=... inserted_profiles=... cursor_after=...`
6. `single seed load word_id=...`
7. `warm question candidates reason=... target=... loaded_now=... seeds=... available_words=...`
8. `load question pool available_words=... by type ...`
9. `session evaluation finished=... scheduler_exhausted=...`

定位顺序建议：

1. 确认 user.db 与阶段词典数据库路径是否存在。
2. 确认 `EnsureTables()` 是否成功，以及失败的 SQLite 具体报错。若失败点在迁移，优先排查是否进入了事务内重建。
3. 看补种事务是否失败并已回滚；若是 `COMMIT` 失败，重点看是否处于 `ATTACH dictdb` 的连接上写主库。
4. 看 `selection profile inventory` 是否已经补出当前教材下的 fresh profile。
5. 看 `selected_words` 是否大于 0。
6. 看 `single seed load` 和 `warm question candidates` 是否成功把 seed 装进 `question_seed_pool_`。
7. 若词和 seed 都已经成功，继续看 `pick next question`、`schedule detail` 与 `present question`，确认是否是展示阶段崩溃，而不是选题阶段失败。
8. 最后看 `available_question_types_by_word_` 是否为空，以及为空是因为素材不足还是策略过严。

## 18. 结论

当前版本 `word_practice` 的稳定实现可以概括为：

- 长期状态由 `WordMasteryProfile` 管理，核心是 `strength + evidence + next_review_at`
- 短期 Session 进度由 `LearningBatch + BatchProgressTracker` 管理
- 题目不是静态池，而是即时按需生成
- 调度规则以确定性优先级为主，便于解释与维护
- Session 结束与通过由 `SessionEvaluator` 统一控制
- 数据库层只描述当前运行时 schema 与当前持久化路径

这就是当前代码对应的正式实现基线。后续若继续调整画像模型、题型策略或 Session 门槛，应基于本文档同步更新。