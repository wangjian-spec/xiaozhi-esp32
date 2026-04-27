# Word Practice 详细设计与实现说明

## 1. 文档定位

本文档描述 `main/eteacher/apps/word_practice` 当前代码的真实实现。它用于开发协作、问题排查、重构评估和联调，不是未来方案草稿。若文档与代码冲突，以代码为准，同时应同步修正文档。

覆盖文件：

- `word_practice.h/.cc`
- `word_practice_types.h`
- `word_practice_selection_module.h/.cc`
- `word_practice_question_seed_module.h/.cc`
- `word_practice_learning_module.h/.cc`
- `word_practice_quiz_module.h/.cc`
- `word_practice_result_module.h/.cc`
- `word_practice_session_module.h/.cc`
- `word_practice_session_pass_policy.h/.cc`
- `word_practice_flow_controller.h/.cc`
- `word_practice_time_utils.h/.cc`
- `word_practice_db_utils.h`
- `word_practice_ui.h/.cc`

一句话总结：

`word_practice` 不是预先生成固定题库再顺序消费，而是“根据用户阶段和学习画像选词，按需加载词库素材，计算每个词可出的题型，由调度器选择下一个 `word_id + skill + question_type`，再即时生成单题，答题后立即更新画像、批次进度、统计和用户 JSON”的练习系统。

主数据链路：

```text
user.json/current_stage
  -> SelectionModule 选词
  -> SelectedWord
  -> QuestionSeedModule 加载 VocabularySeed
  -> WordMasteryDao 读取/衰减 WordMasteryProfile
  -> LearningBatchPlanner 生成 LearningBatch
  -> QuestionScheduler 生成 ScheduledQuestion
  -> QuestionSeedModule::GenerateQuestionOnDemand
  -> QuestionData
  -> QuizModule::Generate
  -> ChoiceState/UI
  -> 用户按键或 ASR
  -> QuestionAttemptRecord
  -> WordMasteryDao + UserProgressDao + user.json
```

## 2. 总体架构

### 2.1 App 总控层

`WordPracticeApp` 继承 `AppBase` 和 `CustomEpdDisplay::ChatMessageListener`，是模块编排中心。

主要职责：

- 生命周期：`OnEnter`、`OnExit`
- 按键路由：`OnButton`
- ASR 回调：`OnChatMessage`
- UI 加载、场景切换、控件绑定、首页预览、结算页显示
- 启动一轮练习：`StartPracticeRound`
- 载入或复用题目候选：`LoadQuestionPool`、`CanReuseQuestionPool`
- 调度下一题：`PickNextQuestion`
- 即时生成题目并展示：`CommitScheduledQuestion`、`PresentCurrentQuestion`
- 处理不同题型作答：`HandleAnswer`、`HandleType4Action`、`HandleType56Action`、`HandleSpeakAction`
- 答题后持久化：`SaveAnswerStats`、`RecordCurrentAttempt`
- 资源播放：`PlayAudioFromSd`、`ReadAudioBundleEntry`、`ResolveBundledImagePath`

### 2.2 业务模块分层

| 层 | 主要类 | 职责 |
|---|---|---|
| 流程配置 | `PracticeFlowController` | 每轮选词规模、口语重试次数 |
| 选词 | `SelectionModule` | 维护活跃 profile 池，从词典和 profile 选出本轮单词 |
| seed 素材 | `QuestionSeedModule` | 从阶段词库读取词义、例句、图片、音频素材，按需生成 `QuestionData` |
| 题目解析 | `QuizModule` | 将 `QuestionData.content_json` 解析成 UI 可消费的 `ChoiceState` |
| 会话计数 | `SessionModule` | 正确数、错误数、分数、答题数、是否等待下一题 |
| 过关策略 | `SessionPassPolicy` | 判断本轮是否通过 |
| 学习画像 | `WordMasteryDao` | 读写 `word_learning_profile` 和 `word_practice_history` |
| 批次计划 | `LearningBatchPlanner` | 将选中词分成新词、复习词、弱词 |
| 批次进度 | `BatchProgressTracker` | 跟踪每个词是否完成本轮目标 |
| 调度 | `QuestionScheduler` | 决定下一题训练哪个词、哪种能力、哪种题型 |
| 统计 | `ResultModule` / `UserProgressDao` | 统计表、每日进度、轮次记录、结算摘要 |

## 3. 核心数据结构

### 3.1 `QuestionData`

位于 `word_practice_types.h`。表示一道题的最小载体。

```cpp
struct QuestionData {
    int id;
    int type;
    std::string stage;
    int difficulty;
    std::string content_json;
    std::string answer;
};
```

字段含义：

- `id`：题目 ID。按需生成题使用 `word_id * 100 + question_type`。
- `type`：题型编号，范围主要为 1 到 12。
- `stage`：教材/阶段名，例如 `stage1`。
- `difficulty`：新词通常为 2，复习词通常为 1。
- `content_json`：题目 JSON 内容，包含 prompt、选项、图片、音频、例句等。
- `answer`：标准答案。选择题通常是 `A/B/C/D`，配对题是 `left-right;...`，输入/朗读题是目标文本。

### 3.2 `ChoiceState`

`QuizModule::Generate` 从 `QuestionData.content_json` 解析得到，供 UI 层使用。

关键字段：

- `prompt`：题干展示文本。
- `textbook_name`：题目所属教材/阶段。
- `audio_filename`：音频资源名或路径。
- `source_word` / `source_word_id`：当前题对应词。
- `option_keys`：选项键，默认 `A/B/C/D`。
- `options`：选项文本。
- `option_images`：图片选择题的选项图片。
- `hints`：句子拼写题可选词块。
- `pair_left` / `pair_left_audio` / `pair_right`：配对题左右列。
- `expected`：标准答案。

### 3.3 选词与 seed

`SelectedWord`：

- `word_id`
- `word`
- `image`
- `is_review`：由 `last_practiced_at > 0` 推导。选词时写入过但未练过的 profile 会被视作新词。

`VocabularySeed`：

- 来自阶段词典库，包含 `word`、`meaning_zh`、`meaning_en`、`image`、`example_en`、`example_zh`、`selection_zh`、`selection_en`。
- `meaning_count`、`word_example_count` 等计数字段用于诊断和资源可用性判断。
- `stage` 用于映射 `stage1` 等教材名和资源路径。

### 3.4 学习画像

`WordMasteryProfile` 对应用户库表 `word_learning_profile`。

关键字段：

- `familiarity`：熟悉度，0 到 100。
- `stability`：稳定度，0 到 100。
- `recognition_score`：当前实现同步为 `familiarity`。
- `recall_score`：回忆证据，0 到 5。
- `output_score`：输出证据，0 到 5。
- `next_review_at`：下次复习时间，epoch seconds。
- `lapse_count`：错误/遗忘次数。
- `last_practiced_at`、`last_decay_at`、`last_error_at`
- `consecutive_correct`、`consecutive_wrong`、`consecutive_recall_correct`
- `recent_review_failed`
- `mastered`
- `stage`：UI 学习阶段，由熟悉度和稳定度派生。

掌握判定：

```text
recall_score >= 3
AND output_score >= 3
AND consecutive_recall_correct >= 2
AND recent_review_failed == false
AND stability >= 60
AND lapse_count <= 3
```

### 3.5 批次与调度

`LearningBatch` 保存本轮计划：

- `items`：`BatchWordPlan` 列表。
- `planned_new_words`
- `planned_review_words`
- `planned_weak_words`

`BatchWordPlan`：

- `selected_word`
- `kind`：`NewWord`、`ReviewWord`、`WeakWord`
- `progress_state`
- `recognition_done`
- `recall_done`
- `shown_count`
- `correct_count`

`ScheduledQuestion`：

- `word_id`
- `question_type`
- `target_skill`：`Recognition`、`Recall`、`Output`、`AdvancedSpeak`
- `reason_type`：`NewWord`、`ReviewDue`、`MistakeFollowup`、`WeakReinforce`、`BatchTarget`
- `reason_text`：JSON 字符串，如 `{"type":"review_due","stage":2,"target_skill":"recall"}`

## 4. 用户配置与 JSON

用户配置文件固定路径：

```text
/sdcard/user/user.json
```

`LoadUserJson` 读取字段：

- `users.user_id`
- `users.name`
- `users.current_stage`
- `users.level`
- `users.today_mission`，兼容旧结构
- `devices.device_id`
- `devices.firmware`
- `settings.enable_read_questions`
- `learning_preferences.enable_read_questions`
- `learning_preferences.new_word_count`
- `learning_preferences.review_word_count`
- `learning_preferences.today_mission`
- `practice_stats.continuous_days`
- `practice_stats.last_practice_date`
- `stage_levelup_count`
- `stage_words_quantity`
- `stage_new_word_cursor`

`SaveUserJson` 写出结构：

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
    "new_word_count": 5,
    "review_word_count": 10
  },
  "practice_stats": {
    "continuous_days": 1,
    "last_practice_date": "YYYY-MM-DD"
  },
  "stage_levelup_count": [10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 48],
  "stage_words_quantity": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  "stage_new_word_cursor": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
}
```

阶段解析：

- `stage1` 到 `stage12` 或数字字符串都支持。
- 范围被夹到 1 到 12。
- 阶段名通过 `StageNumberToTag` / `StageKey` 映射成 `stageN`。

## 5. 数据库与表结构

### 5.1 数据库路径

代码通过 `eteacher::database_manager::sqlite_db_api` 发现数据库。

用户库：

```text
/sdcard/user/user.db
```

阶段词典库：

```text
/sdcard/resource/database/stage_<N>.db
```

题库数据库路径存在于公共数据库模块，但当前 `word_practice` 主链路不依赖静态题库表，而是按需由词典 seed 生成题。

### 5.2 阶段词典库依赖表

`SelectionModule` 依赖：

- `dictdb.word(id, word[, image])`

`QuestionSeedModule` 依赖：

- `word(id, word, image)`
- `word_meaning(id, word_id, meaning_zh, meaning_en, stage)`
- `word_example(id, meaning_id, example_en, example_zh, selection_zh, selection_en)`

单词 ID 优先用 `SelectedWord.word_id` 查询。若按 ID 找不到 meaning，会回退到 `SELECT id FROM word WHERE word = ? LIMIT 1` 再查 meaning。

### 5.3 `word_learning_profile`

由 `SelectionModule::EnsureWordLearningProfileTables` 和 `WordMasteryDao::EnsureTables` 创建。

```sql
CREATE TABLE IF NOT EXISTS word_learning_profile (
  user_id INTEGER NOT NULL,
  word_id INTEGER NOT NULL,
  textbook_name TEXT NOT NULL,
  stage INTEGER DEFAULT 0,
  familiarity INTEGER DEFAULT 0,
  stability INTEGER DEFAULT 0,
  recognition_score INTEGER DEFAULT 0,
  recall_score INTEGER DEFAULT 0,
  output_score INTEGER DEFAULT 0,
  next_review_at INTEGER DEFAULT 0,
  lapse_count INTEGER DEFAULT 0,
  last_practiced_at INTEGER DEFAULT 0,
  last_decay_at INTEGER DEFAULT 0,
  last_error_at INTEGER DEFAULT 0,
  consecutive_correct INTEGER DEFAULT 0,
  consecutive_wrong INTEGER DEFAULT 0,
  consecutive_recall_correct INTEGER DEFAULT 0,
  recent_review_failed INTEGER DEFAULT 0,
  last_response_time_ms INTEGER DEFAULT 0,
  mastered INTEGER DEFAULT 0,
  downgraded_from_stage INTEGER DEFAULT -1,
  PRIMARY KEY(user_id, word_id, textbook_name)
);
```

索引：

```sql
CREATE INDEX IF NOT EXISTS idx_word_learning_profile_user_next_review
ON word_learning_profile(user_id, next_review_at);

CREATE INDEX IF NOT EXISTS idx_word_learning_profile_pick_candidate
ON word_learning_profile(
  user_id, textbook_name, mastered, stage, familiarity, stability,
  recall_score, output_score, next_review_at, word_id
)
WHERE mastered = 0;
```

### 5.4 `word_practice_history`

由 `WordMasteryDao::EnsureTables` 创建。

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

索引：

```sql
CREATE INDEX IF NOT EXISTS idx_word_practice_history_user_word
ON word_practice_history(user_id, word_id, practiced_at);
```

写入时：

- `target_skill` 写 `recognition/recall/output/advanced_speak`
- `review_type` 固定写 `word_practice`
- `rating` 写 `correct ? 1 : 0`
- `response_time` 写答题耗时毫秒
- `question_reason` 写调度原因 JSON

### 5.5 统计表

`UserProgressDao::EnsureStatsTables` 创建。

```sql
CREATE TABLE IF NOT EXISTS learned (
  user_id INTEGER NOT NULL,
  textbook_name TEXT NOT NULL,
  word_id INTEGER NOT NULL,
  correct_count INTEGER DEFAULT 0,
  wrong_count INTEGER DEFAULT 0,
  last_seen_at INTEGER DEFAULT 0,
  PRIMARY KEY (user_id, textbook_name, word_id)
);
```

```sql
CREATE TABLE IF NOT EXISTS word_practice_stats_daily (
  user_id INTEGER NOT NULL,
  date TEXT NOT NULL,
  textbook_name TEXT NOT NULL,
  total_count INTEGER DEFAULT 0,
  correct_count INTEGER DEFAULT 0,
  wrong_count INTEGER DEFAULT 0,
  pass_count INTEGER DEFAULT 0,
  fail_count INTEGER DEFAULT 0,
  PRIMARY KEY (user_id, date, textbook_name)
);
```

```sql
CREATE TABLE IF NOT EXISTS word_practice_daily_progress (
  user_id INTEGER NOT NULL,
  date TEXT NOT NULL,
  textbook_name TEXT NOT NULL,
  completed_words INTEGER DEFAULT 0,
  target_words INTEGER DEFAULT 0,
  progress_percent INTEGER DEFAULT 0,
  updated_at INTEGER DEFAULT 0,
  PRIMARY KEY (user_id, date, textbook_name)
);
```

```sql
CREATE TABLE IF NOT EXISTS word_practice_runtime_state (
  user_id INTEGER NOT NULL,
  textbook_name TEXT NOT NULL,
  completed_rounds INTEGER DEFAULT 0,
  last_round_passed INTEGER DEFAULT 0,
  last_round_at INTEGER DEFAULT 0,
  PRIMARY KEY (user_id, textbook_name)
);
```

## 6. 选词逻辑

入口：

```cpp
SelectionModule::SelectWordsFromVocabulary(
    const WordSelectionConfig& config,
    int user_id,
    int stage_index,
    const std::string& textbook_name,
    int last_new_word_id,
    int* next_new_word_id)
```

当前 `PracticeFlowController::BuildRoundPlan` 固定：

```text
review_word_count = 15
new_word_count = 0
total = 15
```

注意：`user.json` 中的学习偏好会被读取和保存，但本轮实际选词规模由 `PracticeFlowController` 返回的 plan 覆盖。

选词流程：

1. 确保 SQLite runtime 和 SD 挂载。
2. 发现用户库和当前阶段词典库。
3. 以 `READWRITE | CREATE` 打开用户库。
4. 创建/检查 `word_learning_profile`。
5. `ATTACH DATABASE ? AS dictdb` 附加词典库。
6. 检查 `dictdb.word`、`dictdb.word.id`、`dictdb.word.word`。
7. 统计未掌握且到期的活跃 profile 数。
8. 如果到期数量少于 `kMinimumActiveProfileWords`，从词典顺序补 profile。
9. 从 profile 中按弱到强排序选出本轮单词。
10. 返回 `SelectedWord` 列表，并更新 `next_new_word_id`。

到期词统计：

```sql
SELECT COUNT(1)
FROM word_learning_profile AS p
WHERE p.user_id = ?
AND p.textbook_name = ?
AND COALESCE(p.mastered, 0) = 0
AND (p.next_review_at IS NULL OR p.next_review_at = 0 OR p.next_review_at <= ?);
```

补新词 profile：

```sql
SELECT w.id
FROM dictdb.word AS w
LEFT JOIN word_learning_profile AS p
  ON p.user_id = ? AND p.textbook_name = ? AND p.word_id = w.id
WHERE w.word IS NOT NULL AND TRIM(w.word) <> ''
AND w.id > ?
AND p.word_id IS NULL
ORDER BY w.id ASC
LIMIT ?;
```

插入默认 profile：

```sql
INSERT OR IGNORE INTO word_learning_profile(
  user_id, word_id, textbook_name, stage,
  familiarity, stability, recognition_score, recall_score, output_score,
  next_review_at, lapse_count, last_practiced_at, last_decay_at, last_error_at,
  consecutive_correct, consecutive_wrong, consecutive_recall_correct,
  recent_review_failed, last_response_time_ms, mastered, downgraded_from_stage
) VALUES(?, ?, ?, 1, 8, 6, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);
```

选择候选 profile：

```sql
SELECT p.word_id,
       CASE WHEN COALESCE(p.last_practiced_at, 0) > 0 THEN 1 ELSE 0 END
FROM word_learning_profile AS p
WHERE p.user_id = ?
AND p.textbook_name = ?
AND COALESCE(p.mastered, 0) = 0
ORDER BY
  COALESCE(p.stage, 0) ASC,
  COALESCE(p.familiarity, 0) ASC,
  COALESCE(p.stability, 0) ASC,
  COALESCE(p.recall_score, 0) ASC,
  COALESCE(p.output_score, 0) ASC,
  COALESCE(p.next_review_at, 0) ASC,
  p.word_id ASC
LIMIT ?;
```

随后用 `dictdb.word` 批量查词面和图片：

```sql
SELECT w.id, w.word, COALESCE(w.image, '')
FROM dictdb.word AS w
WHERE w.id IN (?, ...)
AND w.word IS NOT NULL AND TRIM(w.word) <> '';
```

## 7. Seed 加载与题目生成

### 7.1 seed 加载

批量加载入口：

```cpp
QuestionSeedModule::LoadVocabularySeedPool(selected_words, stage_index)
```

按需单词加载入口：

```cpp
QuestionSeedModule::LoadVocabularySeedForWord(selected_word, stage_index, out_seed)
```

核心查询：

```sql
SELECT id FROM word WHERE word = ? LIMIT 1;
```

```sql
SELECT id, COALESCE(meaning_zh, ''), COALESCE(meaning_en, ''), COALESCE(stage, 1)
FROM word_meaning
WHERE word_id = ?
ORDER BY id
LIMIT 1;
```

```sql
SELECT id, COALESCE(example_en, ''), COALESCE(example_zh, ''),
       COALESCE(selection_zh, ''), COALESCE(selection_en, '')
FROM word_example
WHERE meaning_id = ?
ORDER BY id
LIMIT 1;
```

图片干扰项不足时，从当前 stage 随机补充：

```sql
SELECT id, COALESCE(word, ''), COALESCE(image, '')
FROM word
WHERE id <> ?
AND word IS NOT NULL AND TRIM(word) <> ''
AND image IS NOT NULL AND TRIM(image) <> ''
ORDER BY RANDOM()
LIMIT ?;
```

### 7.2 冷启动判断

`ShouldUseColdStartMode` 返回 true 的条件：

- 没有复习词。
- 新词占比至少 50%。
- 没有任何 profile。
- 平均 `familiarity < 30` 或平均 `stability < 25`。

冷启动模式影响：

- 新词和低画像词优先识别、轻回忆。
- 不启用跨题纠错链。
- 出题策略更保守。

### 7.3 可用题型计算

入口：

```cpp
BuildAvailableQuestionTypesByWord(
    loaded_seeds,
    profiles,
    include_speak_questions,
    cold_start_mode)
```

每个 seed 先计算 `QuestionBuildPolicy`：

- 新词：只开放 `recognition` 和 `recall`，关闭输出和口语。
- 复习词：
  - `recognition`：冷启动、无 profile、熟悉度低、稳定度低、最近复习失败时开启。
  - `recall`：冷启动、无 profile、熟悉度达到一定值、回忆证据不足、连续回忆不足、最近失败时开启。
  - `output`：非冷启动，且 familiarity >= 35、stability >= 30、recall_score >= 3、consecutive_recall_correct >= 2。
  - `speak`：开启朗读题开关且 output 条件满足。
  - `advanced_speak`：speak 开启且 output_score >= 4、stability >= 45。

题型可用性：

| 题型 | 能力 | 资源条件 |
|---|---|---|
| 1 图片选词 | Recognition | 有图片，图片选项至少 3 个 |
| 2 中文释义选英文 | Recognition | 标准选项至少 3 个 |
| 3 英文选中文释义 | Recall | 标准选项至少 3 个 |
| 4 单词配对 | Recall | 标准选项至少 4 个 |
| 5 中文翻译成英文句子 | Output | 有中英文例句 |
| 6 听写英文句子 | Output | 有中英文例句 |
| 7 朗读单词 | Speak | 开启口语且有单词 |
| 8 翻译并朗读单词 | Recall/Speak | 开启口语且有单词 |
| 9 朗读句子 | AdvancedSpeak | 有中英文例句 |
| 10 翻译并朗读句子 | AdvancedSpeak | 有中英文例句 |
| 11 听音选单词 | Recognition | 标准选项至少 3 个 |
| 12 听音选释义 | Recognition | 标准选项至少 3 个 |

### 7.4 按需生成题

入口：

```cpp
QuestionSeedModule::GenerateQuestionOnDemand(
    loaded_seeds,
    profiles,
    word_id,
    question_type,
    include_speak_questions,
    cold_start_mode,
    out_question)
```

生成规则：

- 题 ID：`word_id * 100 + question_type`
- `stage`：`stageN`
- `difficulty`：复习词 1，新词 2
- 选择题 `answer`：正确选项所在的 `A/B/C/D`
- 配对题 `answer`：`left-right;left-right`
- 句子/朗读题 `answer`：目标英文或单词

题型 JSON 主要格式：

选择题：

```json
{
  "question": "prompt",
  "textbook": "stage1",
  "word_id": 123,
  "audio": "xxx.ogg",
  "word": {"word": "apple"},
  "word_meaning": {"meaning_zh": "苹果", "meaning_en": "apple"},
  "options": {
    "A": {"word": {"word": "apple"}, "word_meaning": {...}, "word_id": 123, "image": "xxx.bin"},
    "B": {...}
  }
}
```

配对题：

```json
{
  "question": "单词配对",
  "textbook": "stage1",
  "left": [
    {"word": "apple", "audio": "xxx.ogg"}
  ],
  "right": ["苹果"]
}
```

句子/朗读题：

```json
{
  "question": "中文或英文提示",
  "prompt": "中文或英文提示",
  "textbook": "stage1",
  "word_id": 123,
  "audio": "xxx.ogg",
  "word": {"word": "apple"},
  "word_meaning": {"meaning_zh": "苹果"},
  "hints": ["I", "like", "apples"]
}
```

## 8. 题型解析与 UI 场景

`QuizModule::Generate` 解析 `content_json` 得到 `ChoiceState`。它兼容：

- `question`、`prompt`、`text`
- `options` 或 `option`
- options 既可为对象，也可为数组
- option 可直接是字符串，也可包含嵌套 `word`、`word_meaning`、`image`
- `left/right` 配对数组
- `hints` 数组或字符串

题型到场景映射：

| 题型 | `QuizType` | 场景 |
|---|---|---|
| 1 | `ImageChoice` | `page_3b66` |
| 2,3,11,12 | `TranslationChoice` | `page_0b9a` |
| 4 | `Match` | `page_1faf` |
| 5,6 | `SentenceBuild` | `page_d55e` |
| 7,8,9,10 | `Speak` | `page_5d74` |
| 首页 | - | `page_9abe` |
| 公共层 | - | `public` |

公共层包含：

- top/bottom bar
- 老师图标
- 题型标题
- 奖杯图标
- 正确/错误计数
- 对错反馈图标
- alert 文案
- speaker 图标
- 题干 label

首页 `page_9abe` 包含：

- 阶段、等级、朗读开关
- 今日任务、进度环
- 单词预览
- 设置按钮、列表、确认/取消按钮

## 9. 启动与一轮练习时序

### 9.1 进入 App

`OnEnter` 时序：

```text
OnEnter
  -> 保存 ctx/epd，并设置 ChatMessageListener
  -> 清空 UI 指针和运行态
  -> session_module_.ResetForNewRound(pass_target_questions_)
  -> selection_module_.ResetProgress()
  -> question_scheduler_.Reset()
  -> LoadUi
  -> InitUiEngine
  -> router 设置 Activate 回调
  -> 激活首页 page_9abe
  -> ShowLoadingPreview
  -> EnsureSqliteRuntimeReady / EnsureSqliteSdMounted
  -> LoadUserJson
  -> SetWordResourceStage(CurrentStageIndex)
  -> 设置 current_user_id_
  -> mastery_dao_.SetUserId
  -> result_module_.SetUserId
  -> enable_speak_questions_ = user_json_.enable_read_questions
  -> word_selection_config_ = practice_flow_controller_.BuildRoundPlan()
  -> current_textbook_name_ = stageN
  -> RefreshHomePreview
  -> Render
```

### 9.2 首页开始练习

`StartPracticeRound` 时序：

```text
StartPracticeRound
  -> HideSelectionDialog
  -> ui_mode_ = Practicing
  -> 记录 session_started_at_sec_
  -> 记录 mastered/progress before
  -> 清空错词、已学词
  -> BuildRoundPlan
  -> ResetRoundState
  -> 根据 user_id/stage/cursor/speak/config 判断是否复用题池
  -> 若不能复用，LoadQuestionPool
  -> PickNextQuestion
  -> Render
```

### 9.3 加载题候选

`LoadQuestionPool` 时序：

```text
LoadQuestionPool
  -> InvalidateQuestionPoolCache
  -> 清空 selected_words_/seed_pool/available_types/profiles/batch
  -> EnsureSqliteRuntimeReady / EnsureSqliteSdMounted
  -> 计算 stage_index/current_textbook_name_
  -> 从 user_json.stage_new_word_cursor 获取 cursor
  -> SelectionModule::SelectWordsFromVocabulary
  -> 如 cursor 变化，更新 user.json 并保存
  -> WordMasteryDao::LoadProfiles
  -> WordMasteryDao::ApplyDueDecayIfNeeded
  -> QuestionSeedModule::ShouldUseColdStartMode
  -> WarmQuestionCandidates(4, "startup")
  -> 如果可用题型为空，按 2 个 seed 递增加载
  -> LearningBatchPlanner::Build
  -> BatchProgressTracker::Reset
  -> QuestionScheduler::Reset
  -> BuildRoundGoalText
  -> UpdateQuestionPoolCacheState
```

### 9.4 调度并展示下一题

```text
PickNextQuestion
  -> 如果可用题型为空，WarmQuestionCandidates
  -> 如果 session 已结束，返回 true
  -> QuestionScheduler::ScheduleNext
  -> 若调度失败，继续增量 warm；仍失败则 FinishRoundIfNeeded
  -> current_scheduled_question_ = scheduled
  -> CommitScheduledQuestion
       -> 如果 seed 不在池中，按需 LoadVocabularySeedForWord
       -> GenerateQuestionOnDemand
       -> current_question_slot_.has_value = true
       -> batch_progress_tracker_.MarkPresented
       -> PresentCurrentQuestion
```

`PresentCurrentQuestion` 做：

- 根据题型切场景。
- `BuildChoiceState` 解析题 JSON。
- 设置 `current_audio_path_`。
- 对有音频题设置自动播放 timer。
- 更新题型标题、正确/错误计数、提示文案。
- 更新题干显示，包括多行/虚线样式。
- 初始化题型专属状态：
  - 4：左右配对数组、答案索引。
  - 5/6：候选词块、输入框。
  - 7-10：ASR 提示、录音状态。
- 隐藏上一题对错图标。

## 10. 调度策略

### 10.1 批次构建

`LearningBatchPlanner::Build`：

- `total_slots = min(10, max(8, selected_words.size()))`
- 冷启动：新词目标 3 到 5，弱词 0。
- 非冷启动：新词目标 2 到 4，弱词 1 到 3。
- review 目标为剩余槽位，至少 1。
- `selected.is_review == false` -> `NewWord`
- 复习词中 `recent_review_failed || lapse_count > 0 || stability < 45` -> `WeakWord`
- 其他 -> `ReviewWord`
- 添加顺序：弱词、新词、复习词，然后用剩余项补满。

### 10.2 批次完成规则

`BatchProgressTracker::MarkOutcome`：

- 新词：需要正确完成 `Recognition` 和 `Recall` 两个 checkpoint。
- 复习词：答对一次即完成。
- 弱词：出现 `MistakeFollowup` 或 `WeakReinforce` 结果即完成。

`IsBatchComplete`：

```text
completed_items >= ceil(total_items * 0.8)
```

### 10.3 技能选择

`QuestionScheduler::ChooseSkill`：

优先级：

1. 错题链强制技能。
2. 新词未完成识别 checkpoint -> `Recognition`。
3. 新词未完成回忆 checkpoint -> `Recall`。
4. 冷启动：
   - easy confirmation -> `Recognition`
   - 无 profile 或 familiarity < 25 -> `Recognition`
   - 否则 `Recall`
5. 非冷启动：
   - 无 profile -> `Recognition`
   - familiarity < 35 -> `Recognition`
   - stability < 30 -> `Recognition`
   - recall_score < 3 或 consecutive_recall_correct < 2 -> `Recall`
   - output_score < 3 或 recent_review_failed -> `Recall`
   - output_score >= 4 且 stability >= 45 -> `AdvancedSpeak`
   - 否则 `Output`

技能到偏好题型：

| 技能 | 题型轮转 |
|---|---|
| Recognition | 1, 2, 11, 12 |
| Recall | 1, 3, 4, 8 |
| Output | 1, 5, 6, 7 |
| AdvancedSpeak | 9, 10 |
| Unknown | 2, 3, 5 |

调度器会记录每个技能的 cursor，避免长期固定同一题型。

### 10.4 候选词优先级

调度流程：

1. 维护最近词窗口 `recent_words_`，窗口大小 5，每词最多连续轮次 2。
2. 非冷启动且有 `mistake_queue_` 时，优先错题跟进。
3. 优先从最近窗口中未完成且出现次数小于等于 1 的词挑。
4. 否则遍历 batch 中未完成词。
5. 候选排序：
   - easy confirmation 时：复习词 > 新词 > 弱词。
   - 正常时：弱词 > 复习词 > 新词。
   - 到期复习加分。
   - stability/familiarity 越低加分越高。
   - 出现次数越多扣分。
6. 避免同一词连续超过 2 次。
7. 避免同一词重复相同题型、相同技能。
8. 若连续两个 Output，再遇 Output 降成 Recall。

### 10.5 错题链

`QuestionScheduler::RecordResult` 中处理：

- 答错、非冷启动、未超过冷却和触发上限时，将该词加入 `mistake_queue_`。
- 强制技能降级：
  - `AdvancedSpeak -> Output`
  - `Output -> Recall`
  - `Recall -> Recognition`
  - `Recognition -> Recognition`
- 同一词错题链冷却到 `total_answered + 3`。
- 触发上限 `kMistakeChainMaxTriggers = 3`。
- 错题链题数最多约为 `hard_limit * 0.3`，超过则清空队列。

## 11. 答题处理

### 11.1 普通选择题 1/2/3/11/12

`HandleAnswer`：

1. 按键映射：A/B/C/D。
2. 标准答案用 `NormalizeAnswerToken` 统一。
3. 正确：`session_module_.RecordAnswer(true)`，显示 good 图标，加入 learned words。
4. 错误：`RecordAnswer(false)`，显示 bad 图标和正确答案。
5. 调用 `SaveAnswerStats`。

`NormalizeAnswerToken` 支持：

- `a/b/c/d` -> 大写
- `OPTION_A` -> `A`
- `UP/LEFT/DOWN/RIGHT` -> `A/B/C/D`

### 11.2 配对题 4

`HandleType4Action`：

- Up/Down 在左侧单词间移动焦点。
- A/B/C/D 选择右侧释义。
- 正确配对后记录该 left 的 right index，并自动跳到下一个未匹配 left。
- 全部匹配后本题记正确。
- 任意错误匹配立即本题记错误。
- 左侧单词焦点变化时，若有音频则播放。

### 11.3 句子拼写/听写题 5/6

`HandleType56Action`：

- Up/Down/Left/Right 在候选词网格中移动。
- C：追加当前词块到输入。
- B：删除最后一个词。
- Start：播放题目音频。
- D：确认提交。
- 比较前使用 `NormalizeType56ForCompare` 规整文本。
- 错误时将 dialog 内容替换为正确答案。
- 题型 6 提交后会隐藏题干提示。

### 11.4 朗读题 7/8/9/10

`HandleSpeakAction`：

- Start PressDown：`AppService::GetInstance().StartListening()`。
- Start PressUp：`StopListening()`。
- D：手动跳过，本题记错。

`OnChatMessage`：

- 只处理 `role == "user"`。
- 只在当前题型 7 到 10 且未等待下一题时处理。
- 7/8 单词朗读：比较 `NormalizeLettersOnlyLower(asr)` 与标准答案。
- 9/10 句子朗读：分词后计算覆盖率，`coverage >= 0.8` 判正确。
- 错误累计 `current_speak_asr_failure_count_`。
- 达到 `MaxSpeakRetryCount() == 3` 后自动记错，并展示正确文本。

## 12. 会话、过关与结算

### 12.1 会话计数

`SessionModule::RecordAnswer`：

- 正确：`correct_count++`，`score += 10`，`consecutive_correct_answers++`
- 错误：`wrong_count++`，`score = max(0, score - 2)`，连续正确归零
- `total_answered++`
- 若未强制结束且未达到题数上限，进入 `awaiting_next_question_`

`pass_target_questions_` 在 `WordPracticeApp` 中默认是 18。

### 12.2 本轮提前结束

`FinishRoundIfNeeded`：

```text
minimum_questions_before_finish =
  min(pass_target_questions_,
      max(summary.completed_items, min(summary.total_items, 6)))

if summary.batch_completed
AND TotalAnswered >= minimum_questions_before_finish:
  session_module_.FinishNow()
```

### 12.3 过关策略

`SessionPassPolicy::IsPassed`：

```text
total_answered > 0 AND batch_summary.batch_completed
```

当前是否过关只看批次完成，不直接看分数或正确率。

### 12.4 结算页

`ShowSessionSummary`：

1. `MaybeRecordRoundCompletion`
2. 生成 `last_session_summary_`
3. 更新 `user_json_.mastered_words`
4. 如升级，更新 `user_json_.level`
5. 更新连续学习天数和日期
6. `SyncUserProgressState`
7. 保存 user.json
8. 隐藏题型专属控件
9. 显示 `结算` 标题、good/bad 图标、操作提示
10. `overlay_mode_ = Settlement`
11. 显示继续/返回选择

`BuildSessionSummaryData` 统计：

- 总题数、错题数、正确率、用时
- 新词总数和完成数
- 复习词/弱词总数和完成数
- 掌握词数量 before/after
- 本日进度 before/after
- 是否升级
- 错词列表
- 连续学习天数

## 13. 持久化写入流程

答题后统一进入：

```cpp
WordPracticeApp::SaveAnswerStats(const QuestionData& q, bool correct)
```

写入流程：

```text
SaveAnswerStats
  -> InvalidateQuestionPoolCache
  -> 打开 user.db READWRITE|CREATE
  -> ConfigureWriteConnection
  -> mastery_dao_.EnsureTables
  -> result_module_.ProgressDao().EnsureStatsTables
  -> BeginTransaction
  -> RecordCurrentAttempt
       -> 组装 QuestionAttemptRecord
       -> mastery_dao_.ApplyAttempt
            -> UpdateProfileFromAttempt
            -> SaveProfile
            -> RecordAttempt
       -> batch_progress_tracker_.MarkOutcome
       -> question_scheduler_.RecordResult
       -> 记录错词
       -> FinishRoundIfNeeded
  -> UpdateDailyProgress
  -> 如连续正确或纠错成功，设置 easy_confirmation_pending_
  -> UserProgressDao::SaveAnswerStats
  -> 若 session 结束且未记录轮次，RecordRoundCompletion
  -> Commit 或 Rollback
  -> SyncUserProgressState
  -> SaveUserJson
```

### 13.1 学习画像更新公式

正确：

```text
familiarity += round(6.0 * skill_weight * confidence_factor)
stability   += round(4.0 * skill_weight * confidence_factor)
consecutive_correct++
consecutive_wrong = 0
```

错误：

```text
familiarity -= round(5.0 * skill_weight * severity)
stability   -= round(6.0 * skill_weight * severity)
lapse_count++
consecutive_wrong++
consecutive_correct = 0
recent_review_failed = true
last_error_at = practiced_at
```

技能权重：

| 技能 | 权重 |
|---|---|
| Recognition | 1.0 |
| Recall | 1.5 |
| Output | 2.0 |
| AdvancedSpeak | 2.0 |

速度置信因子：

| 响应耗时 | 因子 |
|---|---|
| <= 0 | 1.0 |
| <= 4s | 1.2 |
| <= 8s | 1.0 |
| <= 12s | 0.8 |
| > 12s | 0.65 |

错误严重度：

- 基础 1.0
- Output/AdvancedSpeak +0.35
- Recall +0.2
- 响应超过 8 秒 +0.15

正确时证据更新：

- Recall 正确：`recall_score++`，`consecutive_recall_correct++`，清除 `recent_review_failed`
- Output/AdvancedSpeak 正确：`output_score++`，清除 `recent_review_failed`

错误时证据更新：

- Recall 错误：`recall_score--`，`consecutive_recall_correct = 0`
- Output/AdvancedSpeak 错误：`output_score--`

所有分数夹紧：

- familiarity/stability：0 到 100
- recall_score/output_score：0 到 5

### 13.2 派生字段

`SyncDerivedProfileState`：

- `recognition_score = familiarity`
- `mastered = IsMasteredProfile(profile)`
- `stage = DeriveUiStage(profile)`

`DeriveUiStage`：

```text
combined = round(familiarity * 0.6 + stability * 0.4)
stage = 1 + combined / 20
stage clamp 到 1..5
mastered 且 stage < 4 时，stage = 4
```

### 13.3 下次复习时间

`NextReviewIntervalSec`：

```text
base_interval = 6h + familiarity * 720s
error_factor = max(0.35, 1.0 - lapse_count * 0.18)
stability_factor = 0.75 + stability / 100 * 1.25
recall_weight = 0.8 + recall_score * 0.2
recent_error_factor = 最近 24h 有错 ? 0.55 : 1.0
interval = base * error_factor * stability_factor * recall_weight * recent_error_factor
interval clamp 到 1h..21d
next_review_at = practiced_at + interval
```

### 13.4 每日衰减

`ApplyDueDecayIfNeeded`：

- 只处理 `next_review_at > 0` 且当前时间超过下次复习时间的词。
- 同一天只衰减一次。
- `familiarity` 和 `stability`：
  - >= 80：减 2
  - >= 55：减 1
  - < 55：不减
- 写回 profile。

## 14. 资源与音频

### 14.1 图片资源

UI 图标位于：

```text
main/eteacher/apps/word_practice/images
```

包括：

- `word_practice_teacher.bin`
- `word_practice_cup.bin`
- `word_practice_correct.bin`
- `word_practice_wrong.bin`
- `word_practice_good.bin`
- `word_practice_bad.bin`
- `word_practice_write.bin`
- `word_practice_mike.bin`
- `word_practice_speaker.bin`

单词图片通过 `ResolveBundledImagePath` 解析，优先使用题目 JSON 中的图片名。

### 14.2 音频资源

音频字段由 `QuestionSeedModule` 生成：

- 单词音频：`ResolveStage1AudioName`
- 例句音频：`ResolveStage1ExampleAudioName`
- 题目音频路径：`BuildQuestionAudioPath`
- 例句代理路径：`BuildExampleQuestionAudioPath`

`WordPracticeApp::PlayAudioFromSd`：

1. 根据音频路径规范化 bundle entry 名。
2. 优先从音频 bundle 读取 OGG/Opus 数据。
3. 校验 payload 包含 `OggS` 和 `OpusHead`。
4. 播放失败时返回 false，并由 UI 显示“音频播放失败”。

音频 bundle 索引魔数：

```cpp
{'O', 'G', 'G', 'B', 'I', 'N', '1', '\0'}
```

题目展示时，如果 `current_audio_path_` 非空，会通过 timer 安排自动播放；离开或无音频时取消 timer。

## 15. 首页与设置行为

首页由 `ShowHomePreview` / `RefreshHomePreview` 更新。

显示内容：

- 当前阶段
- 当前等级
- 朗读题开关状态
- 今日任务
- 今日完成百分比
- 待练单词预览
- 等级图标

`BuildTodayMissionText` 和 `BuildWordPreviewText` 负责文案拼接。

设置交互：

- 首页 Start：开始练习。
- 结算页 Left/Right：切换继续/返回焦点。
- 结算页 Start：继续下一轮或返回首页。
- 结算页 B/Select：返回首页。

代码中存在 mission preset：

```text
(5,10), (8,12), (10,15), (12,18), (15,20), (20,20), (20,30)
```

但当前练习轮次实际仍由 `PracticeFlowController` 固定为 15 个复习目标、0 个新词目标。

## 16. 函数调用关系速查

### 16.1 一轮主链路

```text
OnButton(Start on home)
  -> HandleHomePreviewAction
  -> StartPracticeRound
  -> LoadQuestionPool
       -> SelectionModule::SelectWordsFromVocabulary
       -> WordMasteryDao::LoadProfiles
       -> WordMasteryDao::ApplyDueDecayIfNeeded
       -> QuestionSeedModule::ShouldUseColdStartMode
       -> WarmQuestionCandidates
            -> QuestionSeedModule::LoadVocabularySeedForWord
            -> QuestionSeedModule::BuildAvailableQuestionTypesByWord
       -> LearningBatchPlanner::Build
       -> BatchProgressTracker::Reset
       -> QuestionScheduler::Reset
  -> PickNextQuestion
       -> QuestionScheduler::ScheduleNext
       -> CommitScheduledQuestion
            -> QuestionSeedModule::GenerateQuestionOnDemand
            -> QuizModule::Generate
            -> PresentCurrentQuestion
```

### 16.2 答题主链路

```text
OnButton
  -> HandleAnswer / HandleType4Action / HandleType56Action / HandleSpeakAction
  -> SessionModule::RecordAnswer
  -> SaveAnswerStats
       -> RecordCurrentAttempt
            -> WordMasteryDao::ApplyAttempt
                 -> UpdateProfileFromAttempt
                 -> SaveProfile
                 -> RecordAttempt
            -> BatchProgressTracker::MarkOutcome
            -> QuestionScheduler::RecordResult
            -> FinishRoundIfNeeded
       -> UserProgressDao::UpdateDailyProgress
       -> UserProgressDao::SaveAnswerStats
       -> UserProgressDao::RecordRoundCompletion
       -> SyncUserProgressState
       -> SaveUserJson
```

### 16.3 下一题

```text
OnButton while AwaitingNextQuestion
  -> IsNextQuestionTriggerButton
  -> session_module_.SetAwaitingNextQuestion(false)
  -> PickNextQuestion
  -> 若 session finished，则 ShowSessionSummary
```

## 17. 当前实现注意点

1. `word_practice_detail_design_doc.md` 应按 UTF-8 保存；旧文件曾出现编码乱码。
2. `PracticeFlowController` 当前固定每轮 15 个复习词、0 个新词，与 `user.json.learning_preferences` 的任务设置并不完全一致。
3. 题目不是从 `question.db` 读静态题，而是由阶段词库按需生成。
4. `word_learning_profile` 既由选词模块创建，也由学习模块创建；字段保持一致，但维护时要同时注意两处 SQL。
5. `LoadProfileColumns` 依赖 SELECT 列顺序，修改查询字段必须同步调整列索引。
6. `SaveAnswerStats` 把学习画像、历史、每日进度、统计、轮次记录放在同一个事务里；新增写表应优先并入同一事务。
7. `IsSessionPassed` 只依赖批次完成度，不依赖正确率。
8. 冷启动会关闭错题链，并降低输出/口语题比例。
9. `QuestionScheduler` 依赖 `available_question_types_by_word`，如果题型生成失败，会从可用列表删除该题型并递归重试。
10. 口语题错误不会立即记错，直到 3 次 ASR 失败或 D 跳过。
11. `QueryMasteredWordCount` 只按 `user_id` 统计，不按 `textbook_name` 过滤。
12. `stage_new_word_cursor` 用于顺序补 profile；词典遍历到尾部后会从 0 回绕。
13. 图片选择题要求 3 个选项，普通选择题最低 3 个选项；UI 标签仍可能有 D，但题型 1 主要使用 A/B/C。
14. `QuestionData.id` 通过 `word_id * 100 + type` 反推 word_id 时要求 id 大于 100。
15. 音频播放同时支持直接路径和 bundle entry，bundle payload 会校验 OGG/Opus 头。

## 18. 维护建议

新增题型时至少同步修改：

- `QuestionSeedModule::BuildAvailableQuestionTypesForSeed`
- `QuestionSeedModule::AppendGeneratedQuestionByType`
- `QuizModule::ResolveQuizType`
- `QuizModule::SelectSceneId`
- `QuizModule::TypeTitle`
- `QuizModule::TypeInstruction`
- `WordPracticeApp::PresentCurrentQuestion`
- 对应 `Handle...Action`
- 调度器技能到题型映射 `QuestionTypesForSkill`

修改学习画像字段时至少同步修改：

- `word_practice_types.h::WordMasteryProfile`
- `SelectionModule::EnsureWordLearningProfileTables`
- `WordMasteryDao::EnsureTables`
- `BuildLoadProfilesSql`
- `LoadProfileColumns`
- `SaveProfile`
- `QueryMasteredWordCount`

修改用户任务/阶段策略时至少同步检查：

- `PracticeFlowController::BuildRoundPlan`
- `LoadUserJson` / `SaveUserJson`
- `SyncUserProgressState`
- `SelectionModule::SelectWordsFromVocabulary`
- `BuildSessionSummaryData`
