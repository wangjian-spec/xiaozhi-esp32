# 数据库总览（EnglishTeacher）

本文档用于统一说明：

1. SD 卡数据库与资源目录结构
2. `user.db` / `words.db` 的核心表模型
3. 统一数据库 API 的使用方式与约束
4. 新 App 接入数据库的标准流程

---

## 1. 存储目录约定

### 1.1 静态资源（只读）

- 词典数据库：`/sdcard/resource/database/words.db`
- 题库数据库：`/sdcard/resource/database/question.db`
- 图片资源：`/sdcard/resource/image/`
- 音频资源：`/sdcard/resource/audio/`

### 1.2 动态数据（可写）

- 用户数据库：`/sdcard/user/user.db`

---

## 2. 数据库结构（按业务分组）

> 说明：以下为当前项目使用的核心表。字段含义以注释为准，新增字段请同步更新本文档。

### 2.1 静态词典库 `words.db`

#### `word`

```sql
word — 单词主表，该表为完整单词库，将各阶段和各教材的单词全部加入该表中，word_type，1：单词，2：词组，3：缩写，等等；
CREATE TABLE word (
    id INTEGER PRIMARY KEY,
    word TEXT NOT NULL UNIQUE,
    phonetic TEXT,
    word_type TEXT,
    image TEXT
);


word_meaning — 不同学习阶段的释义表，word_tag用于对单词进行分类，如动物，物品，词组，动词，职业，听力，缩写，人名等；pos 为词性，source 用于记录释义来源
CREATE TABLE word_meaning (
    id INTEGER PRIMARY KEY,
    word_id INTEGER NOT NULL,
    stage INTEGER,
    pos TEXT,
    meaning_en TEXT，
    meaning_zh TEXT，
    source TEXT,
    word_tag TEXT
);


word_form - 词形表
CREATE TABLE word_form (
    id INTEGER PRIMARY KEY,
    word_id INTEGER,
    form_type TEXT,
    form TEXT
);

word_example — 不同学习阶段的例句表，不同难度，example_tag用于对例句进行分类，如听力，动画等；
CREATE TABLE word_example (
    id INTEGER PRIMARY KEY,
    meaning_id  INTEGER,
    example_en TEXT,
    example_zh TEXT,
    difficulty INTEGER,
    image TEXT,
    example_tag TEXT,
    selection_zh TEXT,
    selection_en TEXT
);


    阶段定义
    1 小学
    2 初中
    3 高中
    4 CET4
    5 CET6
    6 考研
    7 专四
    8 专八
    9 托福
    10 雅思
    11 GRE
```

### 2.2 静态题库 `question.db`

#### `question_bank`

```sql
CREATE TABLE question_bank (
    id INTEGER PRIMARY KEY,
    question_type INTEGER,
    stage INTEGER,
    difficulty INTEGER,
    content_json TEXT
);
```

  `DatabaseCreate.py` 生成规则（`question_bank`）：

  - `question_type`：随机生成 `1~10`数字
  - `audio_path`：每条记录都写入有效路径（非空）
  - `image_path`：每条记录都写入有效路径（非空）

### 2.3 动态用户库 `user.db`

#### A. 账号与设备

账号与设备基础信息不再放在 `user.db`，统一迁移到 `/sdcard/user/user.json`：

- `users.name`
- `users.current_stage`
- `users.level`
- `learning_preferences.today_mission_count`
- `learning_preferences.today_practice_word`
- `settings.enable_read_questions`
- `practice_stats.continuous_days`
- `practice_stats.last_practice_date`
- `devices.device_id`
- `devices.firmware`

`user.db` 仅保留单词学习、统计、任务与 AI 过程相关的动态表。

#### B. 生词与记忆

```sql
CREATE TABLE vocab_items (
  id              INTEGER PRIMARY KEY,
  user_id         INTEGER,
  word_id         INTEGER,
  added_at        INTEGER,
  source          TEXT,
  is_favorite     INTEGER DEFAULT 0,
  is_difficult    INTEGER DEFAULT 0,
  is_mastered     INTEGER DEFAULT 0,
  is_deleted      INTEGER DEFAULT 0
);

上面生词表用户自己维护，用于用户把感兴趣的单词添加到生词表，用户可以对生词表单词做重点练习，可以标记为是否掌握，是否移除等；


CREATE TABLE word_learning_profile (
  user_id               INTEGER NOT NULL,
  word_id               INTEGER NOT NULL,
  textbook_name         TEXT NOT NULL,
  stage                 INTEGER DEFAULT 0,
  strength              INTEGER DEFAULT 0,
  recall_score          INTEGER DEFAULT 0,
  output_score          INTEGER DEFAULT 0,
  next_review_at        INTEGER DEFAULT 0,
  lapse_count           INTEGER DEFAULT 0,
  last_practiced_at     INTEGER DEFAULT 0,
  last_decay_at         INTEGER DEFAULT 0,
  last_reviewed_at      INTEGER DEFAULT 0,
  last_response_time_ms INTEGER DEFAULT 0,
  persistent_boost      INTEGER DEFAULT 0,
  mastered              INTEGER DEFAULT 0,
  PRIMARY KEY(user_id, word_id, textbook_name)
);

字段说明：

- **user_id**: 用户标识，整型，表示该记录所属用户。
- **word_id**: 单词 ID，整型，对应静态词典 `word.id`。
- **textbook_name**: 教材或来源名称，文本，用于区分同一单词在不同教材下的学习记录。
- **stage**: UI 展示阶段，基于 `strength` 和 `mastered` 推导后持久化。
- **strength**: 当前综合掌握强度，范围 `0~100`，是晋级、掌握、弱词判定的核心数值。
- **recall_score**: 主动回忆能力分数，达到阈值后才会放行 Output 训练。
- **output_score**: 输出能力分数，达到阈值后才会放行 Advanced Speak 训练。
- **next_review_at**: 下次进入 due review 的时间戳，Unix 秒。
- **lapse_count**: 遗忘/答错累计次数。
- **last_practiced_at**: 最近一次练习时间戳，Unix 秒。
- **last_decay_at**: 最近一次执行逾期衰减的时间戳，Unix 秒。
- **last_reviewed_at**: 最近一次真正参与复习调度的时间戳，Unix 秒。
- **last_response_time_ms**: 最近一次答题耗时，毫秒。
- **persistent_boost**: 长期弱词强化值，错误累计较多时会置高，帮助调度器重复强化。
- **mastered**: 是否达到“掌握”门槛的持久化标记。

上面表用于单词练习 App 的词级掌握状态。当前版本按 `word_id` 维护 `strength / recall_score / output_score / persistent_boost` 四类核心信号。

运行时约束补充：

- 当前 `word_practice` 运行时以 `PRAGMA user_version = 4` 为目标版本。
- `EnsureSchemaVersion()` 会为 `word_learning_profile` 自动补齐运行时依赖列，并在发现旧主键不包含 `textbook_name` 时执行表重建迁移。
- `json_database_create/DatabaseCreate.py` 创建 `user.db` 时使用与运行时一致的 `word_learning_profile`、`word_practice_history` 和结果统计表结构，并设置 `PRAGMA user_version = 4`。
- `DatabaseCreate.py` 对已有旧版 `user.db` 不是只执行 `CREATE TABLE IF NOT EXISTS`：它会补齐 `textbook_name / stage / strength / recall_score / output_score / next_review_at / lapse_count / last_practiced_at / last_decay_at / last_reviewed_at / last_response_time_ms / persistent_boost / mastered`，并在主键不是 `(user_id, word_id, textbook_name)` 时重建 `word_learning_profile`。这保证 `word_practice` 启动后可以直接打开 `user.db`，并能通过 `INSERT INTO word_learning_profile(user_id, word_id, textbook_name, ...)` 写入或补种 profile。
- `DatabaseCreate.py` 与运行时已统一使用相同的 `word_learning_profile` 字段集合，不再保留旧版 `familiarity / stability / recognition_score` 字段。

`word_practice_history` 用于保存逐题作答历史：

```sql
CREATE TABLE word_practice_history (
  id              INTEGER PRIMARY KEY,
  user_id         INTEGER NOT NULL,
  word_id         INTEGER NOT NULL,
  question_type   INTEGER DEFAULT 0,
  target_skill    TEXT,
  review_type     TEXT,
  rating          INTEGER DEFAULT 0,
  response_time   INTEGER DEFAULT 0,
  correct         INTEGER DEFAULT 0,
  question_reason TEXT,
  practiced_at    INTEGER DEFAULT 0
);
```

结果模块还会维护 4 张运行统计表：

```sql
CREATE TABLE learned (
  user_id       INTEGER NOT NULL,
  textbook_name TEXT NOT NULL,
  word_id       INTEGER NOT NULL,
  correct_count INTEGER DEFAULT 0,
  wrong_count   INTEGER DEFAULT 0,
  last_seen_at  INTEGER DEFAULT 0,
  PRIMARY KEY (user_id, textbook_name, word_id)
);

CREATE TABLE word_practice_stats_daily (
  user_id       INTEGER NOT NULL,
  date          TEXT NOT NULL,
  textbook_name TEXT NOT NULL,
  total_count   INTEGER DEFAULT 0,
  correct_count INTEGER DEFAULT 0,
  wrong_count   INTEGER DEFAULT 0,
  pass_count    INTEGER DEFAULT 0,
  fail_count    INTEGER DEFAULT 0,
  PRIMARY KEY (user_id, date, textbook_name)
);

CREATE TABLE word_practice_daily_progress (
  user_id          INTEGER NOT NULL,
  date             TEXT NOT NULL,
  textbook_name    TEXT NOT NULL,
  completed_words  INTEGER DEFAULT 0,
  target_words     INTEGER DEFAULT 0,
  progress_percent INTEGER DEFAULT 0,
  updated_at       INTEGER DEFAULT 0,
  PRIMARY KEY (user_id, date, textbook_name)
);

CREATE TABLE word_practice_runtime_state (
  user_id           INTEGER NOT NULL,
  textbook_name     TEXT NOT NULL,
  completed_rounds  INTEGER DEFAULT 0,
  last_round_passed INTEGER DEFAULT 0,
  last_round_at     INTEGER DEFAULT 0,
  PRIMARY KEY (user_id, textbook_name)
);
```

其他表字段说明（根据代码实现整理）：

- `word`:
  - **id**: 单词主键。
  - **word**: 单词文本，唯一。
  - **phonetic**: 音标/发音文本。
  - **word_type**: 单词类型（如单词/词组/缩写等）。
  - **image**: 相关图片路径或资源标识。

- `word_meaning`:
  - **id**: 主键。
  - **word_id**: 关联 `word.id`。
  - **stage**: 难度/学习阶段索引。
  - **pos**: 词性（part of speech）。
  - **meaning_en**: 英文释义或释义示例。
  - **meaning_zh**: 中文释义或说明。
  - **source**: 释义来源字段。
  - **word_tag**: 用于分类（如动物、动词、听力等）。

- `word_form`:
  - **id**: 主键。
  - **word_id**: 关联 `word.id`。
  - **form_type**: 词形类型（如复数、过去式等）。
  - **form**: 对应词形文本。

- `word_example`:
  - **id**: 主键。
  - **meaning_id**: 关联 `word_meaning.id`。
  - **example_en**: 英文例句。
  - **example_zh**: 中文释义/翻译/注释。
  - **difficulty**: 难度等级。
  - **image**: 示例相关图片路径/资源。
  - **example_tag**: 例句分类标签（如听力/动画）。
  - **selection_zh**: 多选/填空的中文选项文本（可选）。
  - **selection_en**: 多选/填空的英文选项文本（可选）。

- `question_bank`:
  - **id**: 主键。
  - **question_type**: 题型标识（数字）。
  - **stage**: 适用学习阶段。
  - **difficulty**: 难度等级。
  - **content_json**: 题目内容的 JSON 序列化（包含音频/图片路径等）。

- `vocab_items`:
  - **id**: 主键。
  - **user_id**: 用户 ID。
  - **word_id**: 关联 `word.id`。
  - **added_at**: 添加时间戳（Unix 秒）。
  - **source**: 添加来源描述（如来自哪套题/手动添加）。
  - **is_favorite**: 收藏标志（0/1）。
  - **is_difficult**: 困难标志（0/1）。
  - **is_mastered**: 已掌握标志（0/1）。
  - **is_deleted**: 已删除/移除标志（0/1）。

- `word_practice_history`:
  - **id**: 主键。
  - **user_id**: 用户 ID。
  - **word_id**: 练习词 ID。
  - **question_type**: 题型（数字）。
  - **target_skill**: 本次训练目标（recognition/recall/output）。
  - **review_type**: 出题类型（new_word/review_due/...）。
  - **rating**: 评分或质量值（整型）。
  - **response_time**: 答题耗时（整型，单位未强制，通常秒或毫秒）。
  - **correct**: 是否正确（0/1）。
  - **question_reason**: 出题原因描述。
  - **practiced_at**: 练习时间戳（Unix 秒）。

- `ai_sessions`:
  - **id**: 主键。
  - **user_id**: 用户 ID。
  - **session_type**: 会话类型（文本/口语练习等）。
  - **topic**: 会话主题。
  - **started_at**: 会话开始时间戳。
  - **ended_at**: 会话结束时间戳。
  - **total_duration**: 会话总时长（秒）。
  - **created_at**: 记录创建时间戳。

- `ai_messages`:
  - **id**: 主键。
  - **session_id**: 关联 `ai_sessions.id`。
  - **role**: 消息角色（user/assistant/system）。
  - **content**: 文本内容。
  - **audio_path**: 若有，消息对应的音频文件路径。
  - **created_at**: 创建时间戳。

- `ai_speech_evaluations`:
  - **id**: 主键。
  - **user_id**: 用户 ID（评估对象）。
  - **session_id**: 会话 ID。
  - **message_id**: 被评估的消息 ID（关联 `ai_messages`）。
  - **pronunciation_score**: 发音评分（实数）。
  - **fluency_score**: 流利度评分（实数）。
  - **grammar_score**: 语法评分（实数）。
  - **overall_score**: 综合评分（实数）。
  - **feedback_text**: 文字反馈或建议。
  - **created_at**: 创建时间戳。

- `ai_detected_errors`:
  - **id**: 主键。
  - **user_id**: 用户 ID。
  - **session_id**: 会话 ID。
  - **message_id**: 关联消息 ID。
  - **word_id**: 识别到错误的单词 ID（若有）。
  - **error_type**: 错误类型（发音/语法/词汇等）。
  - **severity**: 严重度等级（整数）。
  - **created_at**: 创建时间戳。

- `learning_stats_daily`:
  - **user_id**: 用户 ID。
  - **date**: 日期字符串 `YYYY-MM-DD`。
  - **reviews**: 今日复习次数。
  - **correct**: 今日正确数。
  - **wrong**: 今日错误数。
  - **new_words**: 今日新增单词数。
  - **study_time_sec**: 今日学习时长（秒）。

- `tasks`:
  - **id**: 主键。
  - **user_id**: 所属用户 ID。
  - **task_type**: 任务类型标识。
  - **target_id**: 目标关联 ID（如单词/题目等）。
  - **title**: 标题。
  - **description**: 任务描述。
  - **start_at**: 开始时间戳。
  - **due_at**: 到期时间戳。
  - **is_completed**: 完成标志（0/1）。
  - **is_deleted**: 删除标志（0/1）。
  - **created_at**: 创建时间戳。

- `game_profile`:
  - **user_id**: 用户 ID（主键）。
  - **level**: 等级。
  - **exp**: 经验值。
  - **coins**: 金币数。
  - **streak_days**: 连续登录天数/连胜天数等。
  - **last_play_at**: 上次游玩时间戳。

- `game_rewards_log`:
  - **id**: 主键。
  - **user_id**: 用户 ID。
  - **reward_type**: 奖励类型。
  - **value**: 奖励数值（如金币数量）。
  - **reason**: 奖励原因说明。
  - **created_at**: 创建时间戳。

- `sync_state`:
  - **table_name**: 表名（主键，用于记录同步进度）。
  - **last_sync_at**: 最近同步时间戳。
  - **last_row_id**: 最近同步的行 ID（用于增量同步）。

（下列为 `word_practice` 模块中运行态/统计表的字段）

- `learned`:
  - **user_id**: 用户 ID（主键的一部分）。
  - **textbook_name**: 教材名（主键的一部分）。
  - **word_id**: 单词 ID（主键的一部分）。
  - **correct_count**: 正确次数计数。
  - **wrong_count**: 错误次数计数。
  - **last_seen_at**: 最近出现/见到时间戳。

- `word_practice_stats_daily`:
  - **user_id**: 用户 ID。
  - **date**: 日期字符串（主键的一部分）。
  - **textbook_name**: 教材名（主键的一部分）。
  - **total_count**: 总答题数。
  - **correct_count**: 正确数。
  - **wrong_count**: 错误数。
  - **pass_count**: 通过次数（或通过题目数）。
  - **fail_count**: 失败次数。

- `word_practice_daily_progress`:
  - **user_id**: 用户 ID。
  - **date**: 日期字符串。
  - **textbook_name**: 教材名。
  - **completed_words**: 当日已完成词数。
  - **target_words**: 当日目标词数。
  - **progress_percent**: 进度百分比（0-100）。
  - **updated_at**: 最近更新时间戳。

- `word_practice_runtime_state`:
  - **user_id**: 用户 ID。
  - **textbook_name**: 教材名。
  - **completed_rounds**: 已完成回合数。
  - **last_round_passed**: 上一回合是否通过（0/1）。
  - **last_round_at**: 上一回合时间戳。

上述字段说明基于仓库中 `DatabaseCreate.py`、迁移脚本与 C++ 建表 SQL（如 `word_practice_learning_module.cc`、`word_practice_result_module.cc`）的实现，如需我把这些字段说明插入到 README 的对应位置（或生成变更日志），我可以继续同步修改或生成 PR 注记。

CREATE TABLE word_practice_history (
  id              INTEGER PRIMARY KEY,
  user_id         INTEGER NOT NULL,
  word_id         INTEGER NOT NULL,
  question_type   INTEGER DEFAULT 0,
  target_skill    TEXT,
  review_type     TEXT,
  rating          INTEGER DEFAULT 0,
  response_time   INTEGER DEFAULT 0,
  correct         INTEGER DEFAULT 0,
  question_reason TEXT,
  practiced_at    INTEGER DEFAULT 0
);

#### C. AI 学习过程

```sql
CREATE TABLE ai_sessions (
  id              INTEGER PRIMARY KEY,
  user_id         INTEGER,
  session_type    TEXT,
  topic           TEXT,
  started_at      INTEGER,
  ended_at        INTEGER,
  total_duration  INTEGER,
  created_at      INTEGER
);

CREATE TABLE ai_messages (
  id              INTEGER PRIMARY KEY,
  session_id      INTEGER,
  role            TEXT,
  content         TEXT,
  audio_path      TEXT,
  created_at      INTEGER
);

CREATE TABLE ai_speech_evaluations (
  id                  INTEGER PRIMARY KEY,
  user_id             INTEGER,
  session_id          INTEGER,
  message_id          INTEGER,
  pronunciation_score REAL,
  fluency_score       REAL,
  grammar_score       REAL,
  overall_score       REAL,
  feedback_text       TEXT,
  created_at          INTEGER
);

CREATE TABLE ai_detected_errors (
  id              INTEGER PRIMARY KEY,
  user_id         INTEGER,
  session_id      INTEGER,
  message_id      INTEGER,
  word_id         INTEGER,
  error_type      TEXT,
  severity        INTEGER,
  created_at      INTEGER
);
```

#### D. 学习统计与任务

```sql
CREATE TABLE learning_stats_daily (
  user_id         INTEGER,
  date            TEXT,
  reviews         INTEGER DEFAULT 0,
  correct         INTEGER DEFAULT 0,
  wrong           INTEGER DEFAULT 0,
  new_words       INTEGER DEFAULT 0,
  study_time_sec  INTEGER DEFAULT 0,
  PRIMARY KEY (user_id, date)
);

CREATE TABLE tasks (
  id              INTEGER PRIMARY KEY,
  user_id         INTEGER,
  task_type       TEXT,
  target_id       INTEGER,
  title           TEXT,
  description     TEXT,
  start_at        INTEGER,
  due_at          INTEGER,
  is_completed    INTEGER DEFAULT 0,
  is_deleted      INTEGER DEFAULT 0,
  created_at      INTEGER
);
```

#### E. 游戏与同步

```sql
CREATE TABLE game_profile (
  user_id         INTEGER PRIMARY KEY,
  level           INTEGER DEFAULT 1,
  exp             INTEGER DEFAULT 0,
  coins           INTEGER DEFAULT 0,
  streak_days     INTEGER DEFAULT 0,
  last_play_at    INTEGER
);

CREATE TABLE game_rewards_log (
  id              INTEGER PRIMARY KEY,
  user_id         INTEGER,
  reward_type     TEXT,
  value           INTEGER,
  reason          TEXT,
  created_at      INTEGER
);

CREATE TABLE sync_state (
  table_name      TEXT PRIMARY KEY,
  last_sync_at    INTEGER,
  last_row_id     INTEGER
);
```

需要创建的索引如下：
| 表名             | 建议创建索引的字段    | 索引名示例                 | 说明          |
| -------------- | ------------ | --------------------- | ----------- |
| `word`         | `word`       | `idx_word_word`       | 按单词快速查找单词信息 |
| `word_meaning` | `word_id`    | `idx_meaning_word`    | 根据单词 ID 查释义 |
| `word_meaning` | `stage`      | `idx_meaning_stage`   | 按学习阶段快速筛选   |
| `word_form`    | `word_id`    | `idx_form_word`       | 查单词的词形变化    |
| `word_example` | `meaning_id` | `idx_example_meaning` | 根据释义 ID 查例句 |


| 表名              | 建议创建索引的字段       | 索引名示例                     | 说明        |
| --------------- | --------------- | ------------------------- | --------- |
| `question_bank` | `stage`         | `idx_question_stage`      | 按阶段快速筛选题目 |
| `question_bank` | `difficulty`    | `idx_question_difficulty` | 按难度筛选题目   |
| `question_bank` | `question_type` | `idx_question_type`       | 按题型筛选     |

| 表名                     | 建议创建索引的字段                       | 索引名示例                        | 说明               |
| ---------------------- | ------------------------------- | ---------------------------- | ---------------- |
| `vocab_items`          | `user_id`                       | `idx_vocab_user`             | 查询某个用户的生词本       |
| `vocab_items`          | `word_id`                       | `idx_vocab_word`             | 查询单词在某用户生词本情况    |
| `word_learning_profile`| `(user_id, next_review_at)`     | `idx_word_learning_profile_user_next_review` | 单词练习按用户和到期时间筛选待复习词 |
| `word_practice_history`| `(user_id, word_id, practiced_at)` | `idx_word_practice_history_user_word` | 查询某词在单词练习中的历史轨迹 |
| `tasks`                | `(user_id, is_deleted, due_at)` | `idx_tasks_user_deleted_due` | 按用户、未删除任务、到期时间筛选 |
| `tasks`                | `(is_deleted, is_completed)`    | `idx_tasks_deleted_done`     | 筛选完成/删除状态任务      |
| `learning_stats_daily` | `(user_id, date)`               | 主键                           | 已经是联合主键，不需要额外索引  |
| `game_rewards_log`     | `user_id`                       | `idx_game_reward_user`       | 查询用户奖励记录         |


> `tasks` 与 `vocab_items` 是当前 App（`calendar_schedule` / `dictionary` / `words_book`）最核心依赖表。

---

## 3. 统一数据库 API（已落地）

统一入口文件：

- `main/eteacher/database_manager/sqlite_db_api.h`
- `main/eteacher/database_manager/sqlite_db_api.cc`

### 3.1 运行时与存储

- `EnsureSqliteRuntimeReady(log_tag)`
- `EnsureSqliteSdMounted(log_tag)`

### 3.2 路径发现与库校验

- `DiscoverDictionaryDbPath(log_tag)`
- `DiscoverUserDataDbPath(log_tag, required_table)`
  - `required_table="vocab_items"`：词典/生词本场景
  - `required_table=nullptr`：允许首次建表场景（如日程）
  - 若固定路径上的 `user.db` 不存在，会返回目标路径供后续 `sqlite3_open_v2(...CREATE...)` 首次建库。
  - 若固定路径上的 `user.db` 被识别为损坏或不是有效 SQLite 文件，会先尝试隔离为 `.corrupt` 备份，再返回原路径供后续重建。

### 3.3 词典库访问

- `OpenValidatedDictionaryDbReadonly(...)`
- `AttachValidatedDictionaryDb(...)`

### 3.4 写连接与事务模板

- `ConfigureWriteConnection(...)`
- `BeginTransaction(...)`
- `CommitTransaction(...)`
- `RollbackTransaction(...)`

### 3.5 任务表保障

- `EnsureTasksTable(...)`
  - 自动创建 `tasks`
  - 自动创建索引：`idx_tasks_user_deleted_due`、`idx_tasks_deleted_done`

### 3.6 数据库日志模块

- 日志模块文件：`main/eteacher/database_manager/database_debug.h`
- 统一日志宏：`DB_LOGE` / `DB_LOGW` / `DB_LOGI` / `DB_LOGD`
- 开关宏：`DATABASE_DEBUG_ENABLE`
  - `1`：开启数据库日志（默认）
  - `0`：关闭数据库日志

示例：

```c
#define DATABASE_DEBUG_ENABLE 0
#include "eteacher/database_manager/database_debug.h"
```

---

## 4. App 迁移状态

- `dictionary`：已迁移（路径发现、attach、写策略、事务模板）
- `words_book`：已迁移（路径发现、attach、写策略、事务模板）
- `calendar_schedule`：已迁移（路径发现、任务表保障、写策略、事务模板）

---

## 5. 开发约束（必须遵守）

1. 不允许在 App 内重复声明数据库路径常量。
2. 不允许在 App 内直接设置 `PRAGMA journal_mode=MEMORY` / `OFF`。
3. 写操作必须走统一事务模板，失败必须回滚。
4. 不允许新增“递归扫描任意 `.db` 兜底”逻辑。
5. 新 App 接入数据库时，先补 `sqlite_db_api`，再在 App 层调用。

---

## 6. 新 App 接入模板（建议）

1. `EnsureSqliteRuntimeReady` + `EnsureSqliteSdMounted`
2. 通过 `Discover*DbPath` 获取库路径
3. 只读操作：`OpenValidatedDictionaryDbReadonly` 或普通只读 `sqlite3_open_v2`
4. 写操作：`ConfigureWriteConnection` → `BeginTransaction` → 执行 SQL → `CommitTransaction`
5. 任一步失败：`RollbackTransaction` 并关闭连接

---

## 7. 常见问题排查

### Q1：`no such table: vocab_items`

- 说明用户库路径识别到错误文件，或目标库未初始化该表。
- 先检查 `DiscoverUserDataDbPath(..., "vocab_items")` 返回路径。

### Q4：`selection abort missing db user_db=missing`

- 对 `word_practice` 来说，说明 `DiscoverUserDataDbPath(..., nullptr)` 最终没有返回可写目标路径。
- 常见原因有两类：
  - 固定路径上存在损坏文件，但隔离失败，导致 discover 仍返回空。
  - 当前运行固件不是包含最新 `sqlite_db_api.cc` 逻辑的版本，仍把“文件不存在”当成失败。
- 优先查看数据库日志中的 `skip invalid sqlite user db file`、`quarantined corrupt user db`、`user db target path reserved for create`、`discover user db failed`。

### Q2：`commit transaction failed rc=14`

- 常见于事务中保留了不需要写入的附加库（`ATTACH`）或路径不可写。
- 建议写事务前先 `DETACH` 只读附加库。

### Q3：`cannot rollback - no transaction is active`

- 表示连接已回到 autocommit 状态；统一 API 已在该场景静默处理。
