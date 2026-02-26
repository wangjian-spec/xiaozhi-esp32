
# 🟢 题型1–10出题规则与 JSON 示例

---

## **题型1：选择正确图片**

**规则**：

* 题目类型：图片选择题
* 内容：给出英文单词或短语
* 提示：显示4张图片，学生选择正确的
* `answer`：正确图片对应的编号或描述
* `content_json` 示例字段：

  * `word`：单词
  * `options`：图片列表或描述
  * `audio`：音频路径

**JSON 示例**：

```json
{
  "id": 1001,
  "question_type": "1",
  "stage": "primary",
  "difficulty": 1,
  "content_json": {
    "audio": "audio/example.ogg",
    "word": "apple",
    "options": ["苹果", "香蕉", "猫", "狗"]
  },
  "answer": "苹果"
}
```

---

## **题型2：选择正确的英译中**

**规则**：

* 给出英文单词或短语
* 选项为中文，选择正确翻译
* `answer`：正确中文翻译
* `content_json` 示例字段：

  * `word`：英文单词
  * `options`：中文翻译列表
  * `audio`：音频路径

**JSON 示例**：

```json
{
  "id": 2001,
  "question_type": "2",
  "stage": "primary",
  "difficulty": 1,
  "content_json": {
    "audio": "audio/example.ogg",
    "word": "dog",
    "options": ["狗", "猫", "鸟", "鱼"]
  },
  "answer": "狗"
}
```

---

## **题型3：选择正确的中译英**

**规则**：

* 给出中文词或短语
* 选项为英文单词或短语
* `answer`：正确英文翻译
* `content_json` 示例字段：

  * `word`：中文
  * `audio`：音频路径
  * `options`：英文列表

**JSON 示例**：

```json
{
  "id": 3001,
  "question_type": "3",
  "stage": "primary",
  "difficulty": 1,
  "content_json": {
    "audio": "audio/example.ogg",
    "word": "猫",
    "options": ["cat", "dog", "fish", "bird"]
  },
  "answer": "cat"
}
```

---

## **题型4：配对题**

**规则**：

* 左侧英文单词列表，右侧中文单词列表
* 学生将正确的英文单词与中文单词配对
* `answer`：配对关系（英文:中文）
* `content_json` 示例字段：

  * `left_words`：英文列表
  * `right_words`：中文列表
  * `audio`：音频路径

**JSON 示例**：

```json
{
  "id": 4001,
  "question_type": "4",
  "stage": "primary",
  "difficulty": 1,
  "content_json": {
    "audio": "audio/example.ogg",
    "left_words": ["apple", "dog", "cat", "book"],
    "right_words": ["书", "狗", "苹果", "猫"]
  },
  "answer": {"apple": "苹果", "dog": "狗", "cat": "猫", "book": "书"}
}
```

---

## **题型5：翻译句子 英译中（带提示词）**

**规则**：

* 给出英文句子
* 提示中文关键字（打乱顺序，可含干扰词，总数≤10）
* `answer`：完整中文句子
* `content_json` 示例字段：

  * `prompt`：英文句子
  * `hints`：中文提示词
  * `audio`：音频路径

**JSON 示例**：

```json
{
  "id": 5001,
  "question_type": "5",
  "stage": "primary",
  "difficulty": 2,
  "content_json": {
    "audio": "audio/example.ogg",
    "prompt": "I have a dog.",
    "hints": ["狗", "我有", "一只", "猫", "鸟", "我"]
  },
  "answer": "我有一只狗。"
}
```

---

## **题型6：翻译句子 中译英（带提示词）**

**规则**：

* 给出中文句子
* 提示英文关键字（打乱顺序，可含干扰词，总数≤10）
* `answer`：完整英文句子
* `content_json` 示例字段：

  * `prompt`：中文句子
  * `hints`：英文提示词
  * `audio`：音频路径

**JSON 示例**：

```json
{
  "id": 6001,
  "question_type": "6",
  "stage": "primary",
  "difficulty": 2,
  "content_json": {
    "audio": "audio/example.ogg",
    "prompt": "我有一只狗。",
    "hints": ["I", "have", "a", "dog", "cat", "see"]
  },
  "answer": "I have a dog."
}
```

---

## **题型7：朗读单词 中文（答英文）**

**规则**：

* 给出中文单词
* 学生朗读中文，系统识别英文对应词
* `answer`：英文单词
* `content_json` 示例字段：

  * `word`：中文
  * `audio`：音频路径

**JSON 示例**：

```json
{
  "id": 7001,
  "question_type": "7",
  "stage": "primary",
  "difficulty": 1,
  "content_json": {"audio": "audio/example.ogg", "word": "苹果"},
  "answer": "apple"
}
```

---

## **题型8：朗读单词 英文**

**规则**：

* 给出英文单词
* 学生朗读英文，系统可评分
* `answer`：英文单词
* `content_json` 示例字段：

  * `word`：英文
  * `audio`：音频路径

**JSON 示例**：

```json
{
  "id": 8001,
  "question_type": "8",
  "stage": "primary",
  "difficulty": 1,
  "content_json": {"audio": "audio/example.ogg", "word": "dog"},
  "answer": "dog"
}
```

---

## **题型9：朗读句子 中文（答英文）**

**规则**：

* 给出中文句子
* 学生朗读中文，系统识别英文对应句子
* `answer`：英文句子
* `content_json` 示例字段：

  * `sentence`：中文
  * `audio`：音频路径

**JSON 示例**：

```json
{
  "id": 9001,
  "question_type": "9",
  "stage": "primary",
  "difficulty": 2,
  "content_json": {"audio": "audio/example.ogg", "sentence": "我有一只狗。"},
  "answer": "I have a dog."
}
```

---

## **题型10：朗读句子 英文**

**规则**：

* 给出英文句子
* 学生朗读英文，系统可评分
* `answer`：英文句子
* `content_json` 示例字段：

  * `sentence`：英文
  * `audio`：音频路径

**JSON 示例**：

```json
{
  "id": 10001,
  "question_type": "10",
  "stage": "primary",
  "difficulty": 2,
  "content_json": {"audio": "audio/example.ogg", "sentence": "I have a dog."},
  "answer": "I have a dog."
}
```
