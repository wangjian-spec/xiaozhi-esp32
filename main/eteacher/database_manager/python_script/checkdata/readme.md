# 词库数据校验工具

入口脚本：`word_data_checker_ui.py`

默认参考数据库：

- `main/eteacher/database_manager/python_script/words.db`

## 启动方式

```powershell
python word_data_checker_ui.py
```

如果当前环境缺少依赖，脚本会尝试自动安装：

- `PySide6`
- `openpyxl`
- `xlrd`
- `xlwt`
- `wordfreq`（可选，用于拼写校验）

## 支持的数据源

### 1. `words.db`

可直接校验数据库中的：

- `word`
- `word_meaning`

### 2. 多个 Excel 文件

支持一次选择多个 `.xls` / `.xlsx` 文件。

Excel 读取规则与 `DatabaseCreate.py` 保持一致：

- 只扫描第一个工作表
- 前 5 行内自动识别表头
- 表头名必须与脚本字段名完全一致
- 支持字段：

```text
word, phonetic, word_type, stage, pos, meaning_en, meaning_zh,
image, source, word_tag, form_type, form, example_en, example_zh,
difficulty, example_tag, audio_path, image_path
```

## 已实现校验

### 原有校验

- `word_empty`
- `word_invalid_word_chars`
- `word_word_spacing`
- `word_word_uppercase`
- `word_invalid_phrase_chars`
- `word_phrase_spacing`
- `word_phrase_uppercase`
- `word_duplicate_word`
- `word_duplicate_phrase`
- `phonetic_phrase_should_be_empty`
- `phonetic_word_empty`
- `meaning_zh_empty`
- `meaning_zh_invalid_chars`

### 扩展校验

- `word_spelling_invalid`
  - 使用参考词典和 `wordfreq` 常用词表检测疑似拼写错误
  - 例如：`techer -> teacher`

- `phonetic_not_standard_ipa`
  - 只检查音标首尾是否带 `[]` 或 `//`
  - 例如：`[test]`、`/test/` 通过，`test` 不通过

- `meaning_semantic_mismatch`
  - 优先使用参考数据库的中文释义词元做离线匹配
  - 也支持同目录下的 `word_data_checker_semantic_cache.json` 作为补充缓存

- `word_type_mismatch`
  - 校验 `word.word_type` 与 `pos / meaning_zh` 中词性前缀是否一致

- `word_without_meaning`
  - 只有单词，没有有效释义

- `meaning_without_word`
  - 只有释义，没有有效单词

- `too_many_meanings`
  - 单词关联释义数量超过阈值时提示脏数据风险
  - 阈值可在界面中调整，默认 `8`

## 自动修复

当前仅对 Excel 模式提供自动修复写回。确认修复后，脚本会直接修改原始 Excel 文件，并在同目录生成 `.bak.xls` 或 `.bak.xlsx` 备份文件。

已支持的自动修复项：

- 单词去首尾空格
- 单词转小写
- 删除单词中的非法字符
- 词组去多余空格
- 词组转小写
- 删除词组中的非法字符
- 清空词组的 `phonetic`
- 为 `phonetic` 自动补齐方括号
- 删除 `meaning_zh` 中的非法字符

## 界面说明

- 左侧按问题类型分组显示数量
- 右侧展示来源文件、行号或记录 ID、原因、建议和自动修复值
- 底部日志显示表头识别、校验过程和写回信息

## 注意事项

- `.xls` 写回会重建工作簿内容，原始格式样式不会保留
- `.xlsx` 写回会直接保存到原文件，同时生成同目录备份文件
- `meaning_semantic_mismatch` 属于离线启发式校验，依赖参考数据库和缓存质量
- 校验数据库时，如果参考库就是当前数据库，语义和音标一致性校验的判定力度会比 Excel 模式弱