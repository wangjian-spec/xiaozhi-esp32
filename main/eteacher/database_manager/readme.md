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
    example_tag TEXT，
    audio_path  TEXT
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

```sql
CREATE TABLE users (
  id              INTEGER PRIMARY KEY,
  nickname        TEXT,
  avatar          TEXT,
  level           INTEGER DEFAULT 1,
  created_at      INTEGER,
  last_active_at  INTEGER
);

CREATE TABLE devices (
  id              TEXT PRIMARY KEY,
  name            TEXT,
  model           TEXT,
  firmware_ver    TEXT,
  last_seen_at    INTEGER
);

CREATE TABLE user_device_bindings (
  user_id     INTEGER,
  device_id   TEXT,
  bind_at     INTEGER,
  PRIMARY KEY (user_id, device_id)
);
```

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



CREATE TABLE vocab_learning_state (
  user_id         INTEGER,
  vocab_id        INTEGER,
  familiarity     REAL DEFAULT 0,
  ease_factor     REAL DEFAULT 2.5,
  interval_days   INTEGER DEFAULT 1,
  repetition      INTEGER DEFAULT 0,
  last_review_at  INTEGER,
  next_review_at  INTEGER,
  lapses          INTEGER DEFAULT 0,
  stability       REAL DEFAULT 0,
  PRIMARY KEY(user_id, vocab_id)
);

上面表用于记录记忆曲线，

CREATE TABLE vocab_review_log (
  id              INTEGER PRIMARY KEY,
  user_id         INTEGER,
  vocab_id        INTEGER,
  review_type     TEXT,
  rating          INTEGER,
  response_time   INTEGER,
  is_correct      INTEGER,
  created_at      INTEGER
);

上面表用于记录单词的学习记录，每做一次题，做一次记录，记录到数据库，用于后面做学习统计；

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
| `vocab_learning_state` | `(user_id, vocab_id)`           | 主键                           | 已经是联合主键，不需要额外索引  |
| `vocab_review_log`     | `user_id`                       | `idx_review_user`            | 查询用户复习记录         |
| `vocab_review_log`     | `vocab_id`                      | `idx_review_vocab`           | 查询单词复习记录         |
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

### Q2：`commit transaction failed rc=14`

- 常见于事务中保留了不需要写入的附加库（`ATTACH`）或路径不可写。
- 建议写事务前先 `DETACH` 只读附加库。

### Q3：`cannot rollback - no transaction is active`

- 表示连接已回到 autocommit 状态；统一 API 已在该场景静默处理。