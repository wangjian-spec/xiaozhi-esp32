# 单词练习出题策略重构方案

## 1. 当前实现的真实出题策略

基于现有实现，当前单词练习的流程可以拆成四步：

1. 先从词库里选词。
2. 再把每个词展开成固定题型集合。
3. 然后把所有题放进题池。
4. 每次从题池里按题型轮转或按旧版自适应规则抽一道题。

对应代码位置：

- 选词入口在 [main/eteacher/apps/word_practice/word_practice.cc](main/eteacher/apps/word_practice/word_practice.cc#L2600)
- 选词实现在 [main/eteacher/apps/word_practice/word_practice_selection_module.cc](main/eteacher/apps/word_practice/word_practice_selection_module.cc#L417)
- 题池构建在 [main/eteacher/apps/word_practice/word_practice.cc](main/eteacher/apps/word_practice/word_practice.cc#L1508)
- 抽题实现在 [main/eteacher/apps/word_practice/word_practice_selection_module.cc](main/eteacher/apps/word_practice/word_practice_selection_module.cc#L611)
- 答题结果落库在 [main/eteacher/apps/word_practice/word_practice_result_module.cc](main/eteacher/apps/word_practice/word_practice_result_module.cc#L187)

### 1.1 当前的选词逻辑

当前默认配置是：

- 复习词 10 个
- 新词 5 个

代码定义在 [main/eteacher/apps/word_practice/word_practice_types.h](main/eteacher/apps/word_practice/word_practice_types.h#L63)。

现有选词规则：

- 复习词：优先从用户学习表里拿到期应复习的词，按 next_review_at、familiarity、repetition 排序。
- 新词：从总词库按 word.id 升序依次补足，不按主题、不按教材单元、不按认知难度、不按词性、不按语义场。

这意味着当前的选词是一个数据库驱动的混合装配过程，不是一个教学驱动的学习计划。

### 1.2 当前的出题逻辑

每个被选中的词，会被尽量展开为 12 类题：

- 1 看词选图
- 2 看义选词
- 3 看词选义
- 4 配对题
- 5 例句中文提示拼句
- 6 例句英文提示拼句
- 7 朗读单词
- 8 看义说词
- 9 朗读英文例句
- 10 看中文说英文例句
- 11 听音选词
- 12 听音选义

题型定义和标题在 [main/eteacher/apps/word_practice/word_practice_quiz_module.cc](main/eteacher/apps/word_practice/word_practice_quiz_module.cc#L473)。

这套逻辑的本质不是根据学习目标生成题，而是根据单词资源是否齐全，尽可能把可出的题全部展开。

### 1.3 当前的抽题逻辑

当前有两种抽题策略：

- TypeCycleRandom：按 1 到 12 题型循环，某题型存在则随机抽一题。
- LegacyAdaptive：优先按照 answered % 12 期望题型，再在该题型里根据阶段匹配、难度匹配、遗忘时间、正确率做打分。

实现位置：

- [main/eteacher/apps/word_practice/word_practice_selection_module.cc](main/eteacher/apps/word_practice/word_practice_selection_module.cc#L644)
- [main/eteacher/apps/word_practice/word_practice_selection_module.cc](main/eteacher/apps/word_practice/word_practice_selection_module.cc#L682)

当前默认启用的是 TypeCycleRandom，定义在 [main/eteacher/apps/word_practice/word_practice.h](main/eteacher/apps/word_practice/word_practice.h#L192)。

### 1.4 当前的记录粒度

当前的 learned 表记录键是：

- user_id
- textbook_name
- question_index

其中 question_index 实际是 word_id * 100 + question_type。

实现位置：

- question id 生成见 [main/eteacher/apps/word_practice/word_practice.cc](main/eteacher/apps/word_practice/word_practice.cc#L869)
- 统计表写入见 [main/eteacher/apps/word_practice/word_practice_result_module.cc](main/eteacher/apps/word_practice/word_practice_result_module.cc#L187)

这说明系统记录的是某个词在某个题型上的表现，不是这个词本身的掌握状态。

## 2. 当前策略为什么不合理

### 2.1 学习目标缺位

当前系统的目标实际上是题型轮转，不是词汇掌握。

它关心的是：

- 下一题该轮到哪一类题型
- 题库里有没有这种题

它不真正关心：

- 这个词现在处于首次接触、初步识别、可回忆、可应用，还是长期保持
- 学习者这一轮应该先学新词，还是先纠错，还是先巩固
- 一组词是否属于同一主题、同一教材单元、同一认知负荷

### 2.2 题是围绕词型资源生成，不是围绕学习阶段生成

当前是一个词来了，如果有图片、音频、例句，就多出几道题；资源少，就少出几道题。

这种做法的问题是：

- 有资源的词会被过度练习。
- 没资源的词会被低频练习。
- 学习节奏被资源完整度绑架，而不是被教学目标驱动。

### 2.3 新词与复习词只是简单配比，不是完整计划

现在只是固定拿 10 个复习词和 5 个新词，但没有继续定义：

- 这 5 个新词本轮应该经历哪些步骤
- 10 个复习词中哪些是高危遗忘词
- 哪些词应该只做识别题，哪些词应该进入拼写和口语输出

所以虽然有配比，但没有路径。

### 2.4 抽题粒度错误，导致学的是题，不是词

当前 learned 统计维度是 question_index，也就是词和题型的组合。结果是：

- 词义选择做对了，不代表会读
- 朗读做对了，不代表会拼写
- 句子拼写做错了，也不会直接拉低该词在识别阶段的掌握度

这会让系统形成碎片化记忆画像，无法对单词做真正的教学决策。

### 2.5 回合结束条件是做满题，不是学会词

当前一轮默认 12 题，结束条件是：

- 题目数达到 pass_target_questions
- 正确率至少 80%
- 分数至少 60

实现位置：

- 回合长度见 [main/eteacher/apps/word_practice/word_practice.h](main/eteacher/apps/word_practice/word_practice.h#L167)
- 过关判定见 [main/eteacher/apps/word_practice/word_practice_result_module.cc](main/eteacher/apps/word_practice/word_practice_result_module.cc#L271)

这套规则更像轻量游戏关卡，不像单词学习闭环。学习者可能答完 12 题就结束，但并不知道自己这轮真正掌握了哪些词、卡在哪一步、下一轮目标是什么。

## 3. 最合理的出题目标

一套合理的单词学习系统，不应该以题型为核心，而应该以词汇掌握路径为核心。

建议把目标定义为：

1. 让学习者每轮明确知道自己在学哪一组词。
2. 让每个词按照固定学习链路推进，而不是随机刷题。
3. 让新词、薄弱词、应复习词分别进入不同训练轨道。
4. 让系统能判断一个词当前处于哪个学习阶段。
5. 让题型只服务于阶段目标，而不是反过来控制学习节奏。

## 4. 建议的最优出题架构

建议把现有系统重构为四层架构：

1. 学习计划层
2. 词状态层
3. 出题编排层
4. 题目渲染层

现有 QuizModule 基本可以保留在题目渲染层，重点要重构前面三层。

### 4.1 学习计划层 Learning Plan

职责：决定这一轮要学什么，不决定具体出哪道题。

建议新增概念：

- learning_batch：本轮学习批次
- batch_goal：本轮目标，例如 新学 4 词、巩固 6 词、纠错 3 词
- word_bucket：把候选词分为 new、weak、due_review、mastered 四类

本层输出不是题池，而是一个结构化学习计划，例如：

- 新词 4 个：每个词需要完成 识别 -> 回忆 -> 输出
- 复习词 6 个：其中 3 个做快速确认，3 个做强化纠错

推荐回合配比：

- 新手阶段：新词 30%，复习 50%，纠错强化 20%
- 正常阶段：新词 20%，复习 60%，纠错强化 20%
- 考前冲刺：新词 10%，复习 50%，输出训练 40%

### 4.2 词状态层 Word Mastery State

这是整个系统最关键的一层。必须把统计粒度从 question_index 升级为 word_id。

建议为每个词维护以下状态：

- recognition_score：识别能力，看词识义、听音识词
- recall_score：回忆能力，看义回忆词形
- spelling_score：拼写能力，输入和拼句
- pronunciation_score：发音能力，朗读单词和例句
- usage_score：应用能力，例句理解和输出
- memory_strength：长期记忆强度
- next_review_at：下次复习时间
- lapse_count：遗忘次数
- consecutive_success：连续成功次数
- current_stage：当前教学阶段

建议 current_stage 定义为：

- 0 未学习
- 1 初识别
- 2 可识别
- 3 可回忆
- 4 可输出
- 5 已巩固

这样系统做决策时，就可以基于词本身，而不是基于某道题的历史记录。

### 4.3 出题编排层 Learning Scheduler

这一层负责把学习计划转成题目序列。

核心原则：

1. 先定词，再定该词当前该练什么能力。
2. 同一个词不要在短时间内机械重复同一题型。
3. 一轮内必须形成完整学习闭环。
4. 优先安排能推动阶段升级的题，而不是平均轮转题型。

建议每个词采用任务链，而不是固定展开 12 题。

#### 新词任务链

适用于 current_stage = 0 或 1：

1. 感知建立：看图或听音认识词
2. 语义绑定：看词选义 / 看义选词
3. 轻回忆：隐藏提示回忆词形
4. 初输出：拼写或朗读单词
5. 小结确认：同词换一种题再验证一次

一个新词一轮建议只出 3 到 5 题，不要一次把 12 种题都出完。

#### 复习词任务链

适用于 current_stage = 2 到 4：

1. 先用低成本题快速探测
2. 如果答对，直接拉长复习间隔
3. 如果答错，立即进入纠错链

纠错链建议：

1. 展示正确词义绑定
2. 出一题同能力修正题
3. 再出一题跨能力迁移题

例如：

- 看义选词错了
- 先展示词形和发音
- 再来一题看义选词
- 再来一题听音选词或拼写

#### 高危遗忘词任务链

适用于 due_review 且 lapse_count 高的词：

1. 不要直接上高难输出题
2. 先用识别题恢复痕迹
3. 再推进到回忆题
4. 最后才做输出题

### 4.4 题目渲染层 Quiz Renderer

这一层就是当前 QuizModule 和 UI 相关模块。

建议保留当前题型资源，但改变调用方式：

- 题型不再作为全局轮转主轴
- 题型变成某个学习目标下的可选模板

例如：

- 学习目标是 recognition，可选题型是 1、2、11、12
- 学习目标是 recall，可选题型是 3、8
- 学习目标是 spelling，可选题型是 5、6
- 学习目标是 pronunciation，可选题型是 7、9、10

## 5. 最合理的出题策略

下面给出一套可以直接落地的推荐策略。

### 5.1 总体策略：以词为单位，以阶段推进为目标

每次出题先回答三个问题：

1. 现在最该练哪个词。
2. 这个词最该练哪种能力。
3. 用哪种题型最适合训练这项能力。

而不是先问下一题轮到哪种题型。

### 5.2 单轮结构建议

建议每轮固定为一个带目标的学习批次，而不是单纯 12 题通关。

推荐默认结构：

- 4 个新词
- 6 个复习词
- 2 个纠错强化词

每轮总题量建议 14 到 18 题，但题量不是硬条件，完成词任务链才是结束条件。

更推荐的结束条件：

- 新词中至少 80% 完成 初识别到初输出
- 复习词中至少 70% 通过本轮探测
- 高危词至少完成一次纠错闭环

### 5.3 单词推进策略

建议按下面的升级规则推进词状态：

- 连续两次识别成功，才能进入可识别
- 连续两次回忆成功，才能进入可回忆
- 回忆成功后再完成一次拼写或朗读成功，才能进入可输出
- 多轮稳定成功，才进入已巩固

如果失败：

- 识别失败，降回更低感知题
- 回忆失败，退回识别加提示题
- 拼写失败，保留当前阶段，但插入纠错链
- 连续失败两次，提高复习优先级并缩短 next_review_at

### 5.4 题型分工建议

当前 12 种题型不是都应该同权使用。建议重新分工：

- 核心识别题：2、11、12
- 核心回忆题：3、8
- 核心输出题：5、6、7、9、10
- 辅助感知题：1
- 辅助联结题：4

建议默认频率：

- 识别题 35%
- 回忆题 30%
- 输出题 25%
- 联结和感知题 10%

原因很直接：

- 单词学习的关键瓶颈通常在回忆，而不是看见以后认出来
- 输出题成本高，应该在词已形成基础表征后使用
- 图片题和配对题适合启蒙和调剂，不适合长期作为主轴

### 5.5 同词连续出题规则

建议加入明确约束：

1. 同一个词连续最多 2 题。
2. 同一个题型不能在最近 3 题里重复超过 2 次。
3. 同一个词若刚答错，下一题优先进入纠错链，不要随机切走。
4. 同一个词若刚完成纠错，不要立刻再出完全一样的题型。

## 6. 可落地的模块设计

### 6.1 在现有结构上的最小改造方案

不建议一口气推翻全部实现。建议按下面方式演进。

#### 第一步：保留现有题型生成逻辑，但改掉调度逻辑

现阶段可以继续保留：

- BuildVocabularyQuestionPool
- QuizModule
- UI 场景

但不要再直接把整个题池当作随机抽题源。

应该新增一个调度器，例如：

- WordLearningPlanner
- QuestionScheduler

其中：

- WordLearningPlanner 负责产出本轮 learning_batch
- QuestionScheduler 负责根据词状态和任务链，从题池中筛选出最合适的一题

#### 第二步：把统计从 question 维度升级到 word 维度

建议新增表：

### word_learning_profile

- user_id
- word_id
- textbook_name
- recognition_score
- recall_score
- spelling_score
- pronunciation_score
- usage_score
- memory_strength
- current_stage
- next_review_at
- lapse_count
- consecutive_success
- last_practiced_at

### word_question_history

- user_id
- word_id
- question_type
- correct
- response_time_ms
- practiced_at

其中：

- word_learning_profile 用于教学决策
- word_question_history 用于分析和追踪细节

不要再让 learned 表承担全部职责。

#### 第三步：引入学习批次和任务链

建议新增内存结构：

```text
LearningBatch {
  vector<WordTaskPlan> new_words;
  vector<WordTaskPlan> review_words;
  vector<WordTaskPlan> weak_words;
}

WordTaskPlan {
  int word_id;
  WordStage current_stage;
  vector<LearningObjective> objectives;
  int progress_cursor;
}
```

LearningObjective 可以是：

- BuildRecognition
- BuildRecall
- BuildSpelling
- BuildPronunciation
- RepairMistake
- VerifyRetention

QuestionScheduler 每次只需要做一件事：

- 从仍未完成 objective 的词里选优先级最高的一个
- 为它找到最匹配 objective 的题型
- 再从该题型候选题里挑一题

### 6.2 建议新增的类

建议新加下面几个模块：

1. WordMasteryDao
2. LearningPlanModule
3. QuestionSchedulerModule
4. MistakeRecoveryModule

职责建议：

#### WordMasteryDao

- 读写词级别掌握状态
- 更新 next_review_at 和记忆强度
- 计算词所属 bucket

#### LearningPlanModule

- 根据用户当前状态生成本轮学习批次
- 决定本轮新词、复习词、弱词的比例

#### QuestionSchedulerModule

- 根据当前批次和词状态选择下一题
- 维护短期去重、题型冷却、同词纠错链

#### MistakeRecoveryModule

- 根据错误类型生成补救链
- 控制错题后 1 到 3 题的走向

## 7. 推荐的调度算法

建议使用两级优先级。

### 7.1 一级：选词优先级

每个候选词计算一个 priority_score：

$$
priority = bucket_weight + overdue_weight + weakness_weight + mistake_chain_weight + novelty_weight - recent_penalty
$$

建议取值方向：

- 正在纠错链中的词最高
- 到期且遗忘风险高的词次高
- 本轮新词再次之
- 已稳定掌握词最低

### 7.2 二级：选学习目标优先级

对已选中的词，再判断当前最应该训练什么：

- 如果 recognition_score 低，先出识别题
- 如果 recognition 已稳定但 recall 低，出回忆题
- 如果 recall 已稳定但 spelling 或 pronunciation 低，出输出题
- 如果刚做错，强制进入 RepairMistake

### 7.3 三级：选题型模板

同一个学习目标可以映射多个题型模板，按资源和冷却状态选择：

- recognition: 2, 11, 12, 1
- recall: 3, 8
- spelling: 5, 6
- pronunciation: 7, 9, 10
- association: 4

题型选择建议权重化而不是固定轮转。

## 8. 推荐的学习者体验

为了让学习者有目的性和方法性，前端提示也要改。

每轮开始时应该明确显示：

- 本轮目标：新学 4 个词，复习 6 个词，纠错 2 个词
- 当前词：这是新词学习 / 复习确认 / 错题强化
- 当前任务：请先认词义 / 请回忆词形 / 请拼写 / 请朗读

每个词完成后应给出词级反馈：

- 这个词已从 初识别 升级到 可回忆
- 这个词发音还不稳定，下轮会继续练
- 这个词已进入 3 天后复习

这样学习者知道自己不是在盲刷题，而是在沿着路径推进。

## 9. 结合当前仓库的实施顺序

建议按三个阶段落地。

### 阶段一：低风险重构

目标：不改 UI 资源，只改策略。

内容：

1. 保留题型生成逻辑。
2. 新增词级状态表。
3. 新增批次计划和调度器。
4. 用调度器替代 TypeCycleRandom。
5. 调整回合结束条件，从做满题改成完成批次目标。

### 阶段二：引入错题补救链

目标：让系统具备教学闭环。

内容：

1. 区分识别错、回忆错、输出错。
2. 为不同错误生成不同补救链。
3. 把错题后续两题绑定到同词修正上。

### 阶段三：引入长期记忆调度

目标：真正形成 spaced repetition。

内容：

1. 按词状态调整 next_review_at。
2. 根据 memory_strength 动态决定复习间隔。
3. 结合 lapse_count 管理遗忘回退。

## 10. 最终结论

当前系统的问题，不是题型不够多，而是出题主轴错了。

现在的主轴是：

- 选一批词
- 为每个词展开固定题型
- 按题型轮转抽题

更合理的主轴应该是：

- 先判断这轮要完成什么学习目标
- 再判断哪些词最该学
- 再判断这些词当前最该练哪种能力
- 最后才选择最合适的题型承载这个目标

一句话总结：

当前是题型驱动学习，建议改成词掌握状态驱动出题。

这才是真正能够落地、能持续优化、也能让学习者有目的性和方法性的方案。