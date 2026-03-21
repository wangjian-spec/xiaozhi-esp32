#include "eteacher/apps/word_snake/word_snake.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Adafruit_GFX.h>
#include <SD.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <sqlite3.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_service/app_service.h"
#include "eteacher/database_manager/sqlite_db_api.h"
#include "eteacher/epd_manager/epd_manager.h"
#include <memory>

#include "display.h"

namespace {

constexpr const char* kTag = "WordSnakeApp";
constexpr int kGridCols = 20;
constexpr int kGridRows = 15;
constexpr int kCellSize = 20;
constexpr int kScreenW = 400;
constexpr int kScreenH = 300;
constexpr int64_t kDispatchTimerPeriodUs = 20 * 1000;
constexpr int64_t kDecisionWindowUs = 200 * 1000;
constexpr const char* kFont = "wenquanyi_11pt";
constexpr const char* kQuestionAudioDir = "/resource/audio/wrods/";

bool IsClickLike(const ButtonEvent& event) {
	return event.action == ButtonAction::Click || event.action == ButtonAction::PressDown ||
		   event.action == ButtonAction::LongPress;
}

std::string Trim(const std::string& value) {
	size_t start = 0;
	while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
		++start;
	}
	size_t end = value.size();
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
		--end;
	}
	return value.substr(start, end - start);
}

std::string JsonString(cJSON* obj, const char* key) {
	if (!obj || !key) {
		return {};
	}
	cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (cJSON_IsString(item) && item->valuestring) {
		return item->valuestring;
	}
	if (cJSON_IsNumber(item)) {
		return std::to_string(item->valueint);
	}
	return {};
}

std::string NormalizeLettersOnlyLower(const std::string& value) {
	std::string out;
	out.reserve(value.size());
	for (unsigned char ch : value) {
		if (std::isalpha(ch) != 0) {
			out.push_back(static_cast<char>(std::tolower(ch)));
		}
	}
	return out;
}

std::vector<std::string> SplitSentenceWordsLower(const std::string& value) {
	std::vector<std::string> words;
	std::string current;
	for (unsigned char ch : value) {
		if (std::isalnum(ch) != 0) {
			current.push_back(static_cast<char>(std::tolower(ch)));
		} else if (!current.empty()) {
			words.push_back(current);
			current.clear();
		}
	}
	if (!current.empty()) {
		words.push_back(current);
	}
	return words;
}

std::string BuildQuestionAudioPath(const std::string& audio_filename) {
	std::string name = Trim(audio_filename);
	if (name.empty()) {
		return {};
	}
	auto lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".mp3") == 0) {
		name.replace(name.size() - 4, 4, ".ogg");
	} else if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".opus") == 0) {
		name.replace(name.size() - 5, 5, ".ogg");
	}
	if (!name.empty() && name[0] == '/') {
		return name;
	}
	return std::string(kQuestionAudioDir) + name;
}

size_t Utf8CharLength(unsigned char first_byte) {
	if ((first_byte & 0x80u) == 0x00u) {
		return 1;
	}
	if ((first_byte & 0xE0u) == 0xC0u) {
		return 2;
	}
	if ((first_byte & 0xF0u) == 0xE0u) {
		return 3;
	}
	if ((first_byte & 0xF8u) == 0xF0u) {
		return 4;
	}
	return 1;
}

std::vector<std::string> WrapUtf8Text(const std::string& text,
							 int max_width,
							 CustomEpdDisplay* epd,
							 const char* font_name) {
	std::vector<std::string> lines;
	if (!epd) {
		if (!text.empty()) {
			lines.push_back(text);
		}
		return lines;
	}

	size_t paragraph_start = 0;
	while (paragraph_start <= text.size()) {
		size_t paragraph_end = text.find('\n', paragraph_start);
		if (paragraph_end == std::string::npos) {
			paragraph_end = text.size();
		}
		const std::string paragraph = text.substr(paragraph_start, paragraph_end - paragraph_start);
		if (paragraph.empty()) {
			lines.emplace_back("");
		} else {
			std::string line;
			size_t idx = 0;
			while (idx < paragraph.size()) {
				const size_t len = Utf8CharLength(static_cast<unsigned char>(paragraph[idx]));
				const size_t next = std::min(paragraph.size(), idx + len);
				const std::string ch = paragraph.substr(idx, next - idx);
				const std::string candidate = line + ch;
				if (!line.empty() && epd->MeasureUtf8Width(candidate, font_name) > max_width) {
					lines.push_back(line);
					line = ch;
				} else {
					line = candidate;
				}
				idx = next;
			}
			if (!line.empty()) {
				lines.push_back(line);
			}
		}

		if (paragraph_end == text.size()) {
			break;
		}
		paragraph_start = paragraph_end + 1;
	}

	return lines;
}

std::string NormalizeAnswerToken(std::string value) {
	value = Trim(value);
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
		return static_cast<char>(std::toupper(ch));
	});
	if (value.rfind("OPTION_", 0) == 0 && value.size() >= 8) {
		value = std::string(1, value[7]);
	}
	if (value == "UP") return "A";
	if (value == "LEFT") return "B";
	if (value == "DOWN") return "C";
	if (value == "RIGHT") return "D";
	return value;
}

void AddUniqueWord(std::vector<std::string>& words, const std::string& candidate) {
	const std::string token = NormalizeLettersOnlyLower(candidate);
	if (token.size() < 2) {
		return;
	}
	if (std::find(words.begin(), words.end(), token) == words.end()) {
		words.push_back(token);
	}
}

enum class Dir : uint8_t { Up = 0, Down = 1, Left = 2, Right = 3 };

struct Cell {
	int row = 0;
	int col = 0;
};

bool operator==(const Cell& a, const Cell& b) {
	return a.row == b.row && a.col == b.col;
}

struct LevelConfig {
	int question_type = 2;
	int words_per_round = 1;
	int distractor_count = 0;
};

LevelConfig BuildLevelConfig(int level) {
	level = std::max(1, std::min(12, level));
	if (level <= 6) {
		if (level <= 3) {
			return LevelConfig{2, level, 0};
		}
		return LevelConfig{5, level - 3, 0};
	}
	if (level == 7) {
		return LevelConfig{5, 3, 0};
	}
	const int base = level - 7;
	if (base <= 3) {
		return LevelConfig{2, base, 5};
	}
	return LevelConfig{5, base - 3, 5};
}

class WordSnakeApp : public AppBase {
public:
	MenuMeta GetMenuMeta() const override {
		return MenuMeta{"word_snake", "单词蛇", "单词接龙贪吃蛇"};
	}

	void OnEnter(AppContext& ctx) override {
		std::lock_guard<std::mutex> lock(mutex_);
		ctx_ = &ctx;
		epd_ = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());
		if (!epd_) {
			ctx.board.GetDisplay()->SetChatMessage("system", "WordSnake: EPD不可用");
			return;
		}

		if (!LoadQuestionPool()) {
			ctx.board.GetDisplay()->SetChatMessage("system", "WordSnake: 题库加载失败");
		}

		saved_partial_force_fast_every_n_ = EpdManager::GetInstance().GetPartialForceFastEveryN();
		EpdManager::GetInstance().SetPartialForceFastEveryN(0xFFFFFFFFu);

		phase_ = Phase::Dialog;
		tick_started_ = true;
		PrepareDialogTextLocked();
		EnsureTimerCreatedLocked();
		StartTimerLocked();
		ScheduleDrawLocked(EpdManager::TaskType::kPartial, 0);
	}

	void OnExit(AppContext& ctx) override {
		(void)ctx;
		std::lock_guard<std::mutex> lock(mutex_);
		StopTimerLocked();
		DeleteTimerLocked();
		waiting_refresh_done_ = false;
		draw_pending_ = false;
		tick_started_ = false;
		epd_ = nullptr;
		ctx_ = nullptr;
		EpdManager::GetInstance().SetPartialForceFastEveryN(saved_partial_force_fast_every_n_);
	}

	void OnButton(AppContext& ctx, const ButtonEvent& event) override {
		if (!IsClickLike(event)) {
			return;
		}

		std::lock_guard<std::mutex> lock(mutex_);
		if (event.id == AppButton::Start) {
			if (event.action != ButtonAction::Click) {
				return;
			}
			if (phase_ == Phase::Dialog || phase_ == Phase::Failed) {
				StartRoundLocked();
				return;
			}
			if (phase_ == Phase::Cleared) {
				level_ = (level_ % 12) + 1;
				round_words_.clear();
				round_word_audio_paths_.clear();
				round_dialog_prompt_.clear();
				target_words_.clear();
				PrepareDialogTextLocked();
				phase_ = Phase::Dialog;
				ScheduleDrawLocked(EpdManager::TaskType::kPartial, 0);
				ctx.board.GetDisplay()->SetChatMessage("system", "WordSnake: 下一关");
				return;
			}
		}

		if (phase_ != Phase::Running) {
			return;
		}

		Dir candidate{};
		if (!ButtonToDir(event.id, candidate)) {
			return;
		}
		if (candidate == current_dir_) {
			++fast_forward_steps_;
			return;
		}
		cached_input_valid_ = true;
		cached_input_dir_ = candidate;
	}

private:
	enum class Phase : uint8_t {
		Dialog,
		Running,
		Failed,
		Cleared,
	};

	struct QuestionRow {
		int id = 0;
		int type = 0;
		std::string content_json;
		std::string answer;
		std::string prompt;
		std::string audio_path;
		std::vector<std::string> hints;
	};

	struct LetterTile {
		char ch = 'a';
		int global_order = -1;
		int word_idx = -1;
		int char_idx = -1;
		bool distractor = false;
	};

	struct DrawTaskCtx {
		WordSnakeApp* app = nullptr;
	};

	struct Type5WordLayout {
		Cell head_anchor;
		Cell first_letter;
		Dir dir_from_head = Dir::Right;
		bool valid = false;
	};

	static int KeyOfCell(const Cell& c) {
		return c.row * kGridCols + c.col;
	}

	static Cell CellFromKey(int key) {
		Cell c;
		c.row = key / kGridCols;
		c.col = key % kGridCols;
		return c;
	}

	static bool InBounds(const Cell& c) {
		return c.row >= 0 && c.row < kGridRows && c.col >= 0 && c.col < kGridCols;
	}

	static int Manhattan(const Cell& a, const Cell& b) {
		return std::abs(a.row - b.row) + std::abs(a.col - b.col);
	}

	static Cell NextCell(const Cell& c, Dir dir) {
		Cell out = c;
		switch (dir) {
			case Dir::Up:
				--out.row;
				break;
			case Dir::Down:
				++out.row;
				break;
			case Dir::Left:
				--out.col;
				break;
			case Dir::Right:
				++out.col;
				break;
		}
		if (out.row < 0) {
			out.row = kGridRows - 1;
		} else if (out.row >= kGridRows) {
			out.row = 0;
		}
		if (out.col < 0) {
			out.col = kGridCols - 1;
		} else if (out.col >= kGridCols) {
			out.col = 0;
		}
		return out;
	}

	static bool NextCellNoWrap(const Cell& c, Dir dir, Cell& out) {
		out = c;
		switch (dir) {
			case Dir::Up:
				--out.row;
				break;
			case Dir::Down:
				++out.row;
				break;
			case Dir::Left:
				--out.col;
				break;
			case Dir::Right:
				++out.col;
				break;
		}
		return InBounds(out);
	}

	static Dir DirBetweenAdjacent(const Cell& from, const Cell& to) {
		if (to.row == from.row - 1 && to.col == from.col) {
			return Dir::Up;
		}
		if (to.row == from.row + 1 && to.col == from.col) {
			return Dir::Down;
		}
		if (to.row == from.row && to.col == from.col - 1) {
			return Dir::Left;
		}
		return Dir::Right;
	}

	static bool IsOpposite(Dir a, Dir b) {
		return (a == Dir::Up && b == Dir::Down) || (a == Dir::Down && b == Dir::Up) ||
			   (a == Dir::Left && b == Dir::Right) || (a == Dir::Right && b == Dir::Left);
	}

	static bool ButtonToDir(AppButton btn, Dir& out) {
		switch (btn) {
			case AppButton::Up:
				out = Dir::Up;
				return true;
			case AppButton::Down:
				out = Dir::Down;
				return true;
			case AppButton::Left:
				out = Dir::Left;
				return true;
			case AppButton::Right:
				out = Dir::Right;
				return true;
			default:
				return false;
		}
	}

	std::string DiscoverQuestionDbPath() const {
		static const std::array<const char*, 3> kCandidates = {
			"/sdcard/resource/database/question.db",
			"/sdcard/resources/database/question.db",
			"/sdcard/resource/db/question.db",
		};

		for (const char* path : kCandidates) {
			sqlite3* db = nullptr;
			const int rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, nullptr);
			if (rc != SQLITE_OK || !db) {
				if (db) {
					sqlite3_close(db);
				}
				continue;
			}
			sqlite3_stmt* stmt = nullptr;
			const char* sql = "SELECT 1 FROM question_bank LIMIT 1;";
			const int pr = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
			if (pr == SQLITE_OK && stmt) {
				sqlite3_finalize(stmt);
				sqlite3_close(db);
				return path;
			}
			if (stmt) {
				sqlite3_finalize(stmt);
			}
			sqlite3_close(db);
		}
		return {};
	}

	std::vector<std::string> ExtractHints(cJSON* root) const {
		std::vector<std::string> out;
		if (!root) {
			return out;
		}
		cJSON* hints_obj = cJSON_GetObjectItemCaseSensitive(root, "hints");
		if (cJSON_IsArray(hints_obj)) {
			for (int i = 0; i < cJSON_GetArraySize(hints_obj); ++i) {
				cJSON* item = cJSON_GetArrayItem(hints_obj, i);
				if (cJSON_IsString(item) && item->valuestring) {
					std::string token = NormalizeLettersOnlyLower(item->valuestring);
					if (!token.empty()) {
						out.push_back(std::move(token));
					}
				}
			}
		}
		return out;
	}

	bool LoadQuestionPool() {
		question_type2_pool_.clear();
		question_type5_pool_.clear();

		if (!eteacher::database_manager::EnsureSqliteRuntimeReady(kTag)) {
			return false;
		}
		(void)eteacher::database_manager::EnsureSqliteSdMounted(kTag);

		const std::string db_path = DiscoverQuestionDbPath();
		if (db_path.empty()) {
			return false;
		}

		sqlite3* db = nullptr;
		if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK || !db) {
			if (db) {
				sqlite3_close(db);
			}
			return false;
		}

		const char* sql = "SELECT id, question_type, content_json, answer FROM question_bank;";
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
			if (stmt) {
				sqlite3_finalize(stmt);
			}
			sqlite3_close(db);
			return false;
		}

		while (sqlite3_step(stmt) == SQLITE_ROW) {
			QuestionRow row;
			row.id = sqlite3_column_int(stmt, 0);
			row.type = sqlite3_column_int(stmt, 1);
			row.content_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
							   ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
							   : "";
			row.answer = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
					 ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
					 : "";

			if (!row.content_json.empty()) {
				cJSON* root = cJSON_Parse(row.content_json.c_str());
				if (root) {
					row.prompt = JsonString(root, "question");
					if (row.prompt.empty()) {
						row.prompt = JsonString(root, "prompt");
					}
					if (row.prompt.empty()) {
						row.prompt = JsonString(root, "text");
					}
					row.audio_path = BuildQuestionAudioPath(JsonString(root, "audio"));
					row.hints = ExtractHints(root);
					cJSON_Delete(root);
				}
			}

			if (row.type == 2) {
				question_type2_pool_.push_back(std::move(row));
			} else if (row.type == 5) {
				question_type5_pool_.push_back(std::move(row));
			}
		}

		sqlite3_finalize(stmt);
		sqlite3_close(db);

		ESP_LOGI(kTag,
			 "question loaded: type2=%d type5=%d",
			 static_cast<int>(question_type2_pool_.size()),
			 static_cast<int>(question_type5_pool_.size()));
		return !question_type2_pool_.empty() || !question_type5_pool_.empty();
	}

	std::vector<std::string> ExtractWordsFromType2Question(const QuestionRow& row) const {
		std::vector<std::string> words;
		for (const auto& h : row.hints) {
			AddUniqueWord(words, h);
		}

		AddUniqueWord(words, row.answer);
		AddUniqueWord(words, row.prompt);
		for (const auto& token : SplitSentenceWordsLower(row.prompt)) {
			AddUniqueWord(words, token);
		}

		if (!row.content_json.empty()) {
			cJSON* root = cJSON_Parse(row.content_json.c_str());
			if (root) {
				AddUniqueWord(words, JsonString(root, "question"));
				AddUniqueWord(words, JsonString(root, "prompt"));
				AddUniqueWord(words, JsonString(root, "text"));

				const std::string expected_key = NormalizeAnswerToken(row.answer);
				std::array<std::string, 4> option_text = {"", "", "", ""};

				cJSON* options_obj = cJSON_GetObjectItemCaseSensitive(root, "options");
				if (!options_obj) {
					options_obj = cJSON_GetObjectItemCaseSensitive(root, "option");
				}

				if (cJSON_IsObject(options_obj)) {
					std::array<const char*, 4> keys = {"A", "B", "C", "D"};
					for (size_t i = 0; i < keys.size(); ++i) {
						cJSON* entry = cJSON_GetObjectItemCaseSensitive(options_obj, keys[i]);
						if (cJSON_IsString(entry) && entry->valuestring) {
							option_text[i] = entry->valuestring;
						} else if (cJSON_IsObject(entry)) {
							std::string text = JsonString(entry, "text");
							if (text.empty()) {
								text = JsonString(entry, "tex");
							}
							option_text[i] = text;
						}
					}
				} else if (cJSON_IsArray(options_obj)) {
					for (int i = 0; i < cJSON_GetArraySize(options_obj) && i < 4; ++i) {
						cJSON* item = cJSON_GetArrayItem(options_obj, i);
						if (cJSON_IsString(item) && item->valuestring) {
							option_text[static_cast<size_t>(i)] = item->valuestring;
						} else if (cJSON_IsObject(item)) {
							std::string text = JsonString(item, "text");
							if (text.empty()) {
								text = JsonString(item, "tex");
							}
							option_text[static_cast<size_t>(i)] = text;
							std::string key = NormalizeAnswerToken(JsonString(item, "key"));
							if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'D') {
								option_text[static_cast<size_t>(key[0] - 'A')] = text;
							}
						}
					}
				}

				for (const auto& opt : option_text) {
					AddUniqueWord(words, opt);
				}
				if (expected_key.size() == 1 && expected_key[0] >= 'A' && expected_key[0] <= 'D') {
					AddUniqueWord(words, option_text[static_cast<size_t>(expected_key[0] - 'A')]);
				}

				cJSON_Delete(root);
			}
		}

		if (words.empty()) {
			std::string raw;
			raw.reserve(row.content_json.size());
			for (unsigned char ch : row.content_json) {
				if (std::isalpha(ch) != 0) {
					raw.push_back(static_cast<char>(std::tolower(ch)));
				} else {
					raw.push_back(' ');
				}
			}
			for (const auto& token : SplitSentenceWordsLower(raw)) {
				AddUniqueWord(words, token);
			}
		}
		return words;
	}

	bool PickRoundWordsLocked(const LevelConfig& cfg) {
		round_words_.clear();
		round_word_audio_paths_.clear();
		round_dialog_prompt_.clear();
		round_question_type_ = cfg.question_type;
		type5_word_layouts_.clear();
		pending_type5_word_jump_ = false;

		std::mt19937 rng(static_cast<uint32_t>(esp_random()));
		if (cfg.question_type == 2) {
			if (question_type2_pool_.empty()) {
				return false;
			}
			std::vector<int> indices(question_type2_pool_.size());
			for (size_t i = 0; i < indices.size(); ++i) {
				indices[i] = static_cast<int>(i);
			}
			std::shuffle(indices.begin(), indices.end(), rng);

			for (int idx : indices) {
				if (static_cast<int>(round_words_.size()) >= cfg.words_per_round) {
					break;
				}
				const auto& row = question_type2_pool_[static_cast<size_t>(idx)];
				auto candidates = ExtractWordsFromType2Question(row);
				ESP_LOGI(kTag,
					 "type2 row id=%d prompt='%s' answer='%s' candidates=%d",
					 row.id,
					 row.prompt.c_str(),
					 row.answer.c_str(),
					 static_cast<int>(candidates.size()));
				if (candidates.empty()) {
					continue;
				}
				std::shuffle(candidates.begin(), candidates.end(), rng);
				for (const auto& w : candidates) {
					if (static_cast<int>(round_words_.size()) >= cfg.words_per_round) {
						break;
					}
					if (w.size() < 2) {
						continue;
					}
					round_words_.push_back(w);
					round_word_audio_paths_.push_back(row.audio_path);
				}
			}
			round_dialog_prompt_ = "要拼写的单词: ";
			for (size_t i = 0; i < round_words_.size(); ++i) {
				if (i > 0) {
					round_dialog_prompt_ += "  ->  ";
				}
				round_dialog_prompt_ += round_words_[i];
			}
		} else {
			if (question_type5_pool_.empty()) {
				return false;
			}
			std::uniform_int_distribution<size_t> pick_row(0, question_type5_pool_.size() - 1);
			const auto& row = question_type5_pool_[pick_row(rng)];
			auto sentence_words = SplitSentenceWordsLower(!row.prompt.empty() ? row.prompt : row.answer);
			std::vector<std::string> filtered;
			for (const auto& item : sentence_words) {
				if (item.size() >= 2) {
					filtered.push_back(item);
				}
			}
			if (filtered.empty()) {
				return false;
			}
			std::shuffle(filtered.begin(), filtered.end(), rng);
			const int count = std::min<int>(cfg.words_per_round, static_cast<int>(filtered.size()));
			for (int i = 0; i < count; ++i) {
				round_words_.push_back(filtered[static_cast<size_t>(i)]);
				round_word_audio_paths_.push_back(row.audio_path);
			}
			round_dialog_prompt_ = "要拼写的句子: ";
			round_dialog_prompt_ += row.prompt.empty() ? row.answer : row.prompt;
		}

		if (static_cast<int>(round_words_.size()) < cfg.words_per_round) {
			ESP_LOGW(kTag,
				 "pick words failed level=%d type=%d need=%d got=%d",
				 level_,
				 cfg.question_type,
				 cfg.words_per_round,
				 static_cast<int>(round_words_.size()));
			return false;
		}

		ESP_LOGI(kTag,
			 "pick words ok level=%d type=%d words=%d first='%s'",
			 level_,
			 cfg.question_type,
			 static_cast<int>(round_words_.size()),
			 round_words_.empty() ? "" : round_words_.front().c_str());

		target_words_ = round_words_;
		current_word_idx_ = 0;
		current_char_idx_ = 0;
		next_expected_global_order_ = 0;
		need_gap_growth_before_next_word_ = false;
		pending_type5_word_jump_ = false;
		consumed_tokens_phrase_order_.clear();
		tokens_by_snake_order_.clear();
		return true;
	}

	bool BuildType5PathDfs(const Cell& current,
					 int remain_steps,
					 std::vector<uint8_t>& blocked,
					 std::vector<Cell>& path,
					 std::mt19937& rng) {
		if (remain_steps <= 0) {
			return true;
		}
		std::array<Dir, 4> dirs = {Dir::Up, Dir::Down, Dir::Left, Dir::Right};
		std::shuffle(dirs.begin(), dirs.end(), rng);
		for (Dir dir : dirs) {
			Cell next;
			if (!NextCellNoWrap(current, dir, next)) {
				continue;
			}
			const int next_key = KeyOfCell(next);
			if (blocked[static_cast<size_t>(next_key)] != 0) {
				continue;
			}
			blocked[static_cast<size_t>(next_key)] = 1;
			path.push_back(next);
			if (BuildType5PathDfs(next, remain_steps - 1, blocked, path, rng)) {
				return true;
			}
			path.pop_back();
			blocked[static_cast<size_t>(next_key)] = 0;
		}
		return false;
	}

	bool GenerateType5WordPathLocked(int total_cells,
					 const std::vector<uint8_t>& occupied,
					 std::vector<Cell>& out_path,
					 std::mt19937& rng) {
		out_path.clear();
		if (total_cells < 2) {
			return false;
		}

		std::vector<int> candidates;
		candidates.reserve(kGridCols * kGridRows);
		for (int key = 0; key < kGridCols * kGridRows; ++key) {
			if (occupied[static_cast<size_t>(key)] == 0) {
				candidates.push_back(key);
			}
		}
		if (candidates.empty()) {
			return false;
		}
		std::shuffle(candidates.begin(), candidates.end(), rng);

		for (int start_key : candidates) {
			std::vector<uint8_t> blocked = occupied;
			blocked[static_cast<size_t>(start_key)] = 1;
			out_path.clear();
			const Cell start = CellFromKey(start_key);
			out_path.push_back(start);
			if (BuildType5PathDfs(start, total_cells - 1, blocked, out_path, rng)) {
				return true;
			}
		}
		return false;
	}

	bool PlaceType5LettersLocked(const LevelConfig& cfg) {
		tiles_.clear();
		type5_word_layouts_.clear();
		type5_word_layouts_.resize(target_words_.size());

		std::vector<uint8_t> occupied(static_cast<size_t>(kGridCols * kGridRows), 0);
		std::mt19937 rng(static_cast<uint32_t>(esp_random()));
		std::uniform_int_distribution<int> pick_head_side(0, 1);
		int global_order = 0;

		for (size_t wi = 0; wi < target_words_.size(); ++wi) {
			const std::string original_word = target_words_[wi];
			if (original_word.size() < 2) {
				return false;
			}

			std::vector<Cell> path;
			if (!GenerateType5WordPathLocked(static_cast<int>(original_word.size()) + 1, occupied, path, rng)) {
				return false;
			}

			const bool head_before_first = (pick_head_side(rng) == 0);
			std::string oriented_word = original_word;
			if (!head_before_first) {
				std::reverse(oriented_word.begin(), oriented_word.end());
			}

			Type5WordLayout layout;
			if (head_before_first) {
				layout.head_anchor = path.front();
				layout.first_letter = path[1];
				layout.dir_from_head = DirBetweenAdjacent(layout.head_anchor, layout.first_letter);
				for (size_t ci = 0; ci < oriented_word.size(); ++ci) {
					const Cell cell = path[ci + 1];
					LetterTile t;
					t.ch = oriented_word[ci];
					t.global_order = global_order;
					t.word_idx = static_cast<int>(wi);
					t.char_idx = static_cast<int>(ci);
					t.distractor = false;
					tiles_[KeyOfCell(cell)] = t;
					++global_order;
				}
			} else {
				layout.head_anchor = path.back();
				layout.first_letter = path[path.size() - 2];
				layout.dir_from_head = DirBetweenAdjacent(layout.head_anchor, layout.first_letter);
				for (size_t ci = 0; ci < oriented_word.size(); ++ci) {
					const Cell cell = path[path.size() - 2 - ci];
					LetterTile t;
					t.ch = oriented_word[ci];
					t.global_order = global_order;
					t.word_idx = static_cast<int>(wi);
					t.char_idx = static_cast<int>(ci);
					t.distractor = false;
					tiles_[KeyOfCell(cell)] = t;
					++global_order;
				}
			}
			layout.valid = true;
			type5_word_layouts_[wi] = layout;
			target_words_[wi] = oriented_word;

			for (const Cell& c : path) {
				occupied[static_cast<size_t>(KeyOfCell(c))] = 1;
			}
		}

		std::uniform_int_distribution<int> random_char('a', 'z');
		std::vector<int> free_keys;
		free_keys.reserve(kGridCols * kGridRows);
		for (int key = 0; key < kGridCols * kGridRows; ++key) {
			if (occupied[static_cast<size_t>(key)] == 0) {
				free_keys.push_back(key);
			}
		}
		std::shuffle(free_keys.begin(), free_keys.end(), rng);
		const int distractor_need = std::min<int>(cfg.distractor_count, static_cast<int>(free_keys.size()));
		for (int i = 0; i < distractor_need; ++i) {
			const int key = free_keys[static_cast<size_t>(i)];
			LetterTile t;
			t.ch = static_cast<char>(random_char(rng));
			t.distractor = true;
			tiles_[key] = t;
		}

		return true;
	}

	bool PlaceLettersLocked(const LevelConfig& cfg) {
		if (cfg.question_type == 5) {
			return PlaceType5LettersLocked(cfg);
		}

		tiles_.clear();
		std::vector<int> all_keys;
		all_keys.reserve(kGridCols * kGridRows);
		for (int r = 0; r < kGridRows; ++r) {
			for (int c = 0; c < kGridCols; ++c) {
				all_keys.push_back(r * kGridCols + c);
			}
		}

		std::mt19937 rng(static_cast<uint32_t>(esp_random()));
		std::shuffle(all_keys.begin(), all_keys.end(), rng);

		const Cell snake_start{7, 10};
		Cell prev_word_last{-10, -10};
		int global_order = 0;

		for (size_t wi = 0; wi < target_words_.size(); ++wi) {
			const std::string& word = target_words_[wi];
			for (size_t ci = 0; ci < word.size(); ++ci) {
				bool placed = false;
				for (int key : all_keys) {
					if (tiles_.find(key) != tiles_.end()) {
						continue;
					}
					const Cell cell = CellFromKey(key);
					if (cell == snake_start) {
						continue;
					}
					if (ci == 0 && wi > 0 && Manhattan(prev_word_last, cell) < 2) {
						continue;
					}

					LetterTile t;
					t.ch = word[ci];
					t.global_order = global_order;
					t.word_idx = static_cast<int>(wi);
					t.char_idx = static_cast<int>(ci);
					t.distractor = false;
					tiles_[key] = t;
					++global_order;
					placed = true;
					if (ci + 1 == word.size()) {
						prev_word_last = cell;
					}
					break;
				}
				if (!placed) {
					return false;
				}
			}
		}

		std::uniform_int_distribution<int> random_char('a', 'z');
		int added = 0;
		for (int key : all_keys) {
			if (added >= cfg.distractor_count) {
				break;
			}
			if (tiles_.find(key) != tiles_.end()) {
				continue;
			}
			const Cell cell = CellFromKey(key);
			if (cell == snake_start) {
				continue;
			}
			LetterTile t;
			t.ch = static_cast<char>(random_char(rng));
			t.distractor = true;
			tiles_[key] = t;
			++added;
		}
		return true;
	}

	bool PlayAudioFromSd(const std::string& audio_path) {
		if (audio_path.empty()) {
			return false;
		}
		File file = SD.open(audio_path.c_str(), FILE_READ);
		if (!file) {
			return false;
		}
		const size_t file_size = static_cast<size_t>(file.size());
		if (file_size == 0) {
			file.close();
			return false;
		}
		std::string ogg_data(file_size, '\0');
		const size_t read_size = file.readBytes(ogg_data.data(), static_cast<int>(file_size));
		file.close();
		if (read_size != file_size) {
			return false;
		}
		AppService::GetInstance().PlaySound(ogg_data);
		return true;
	}

	void SpeakCurrentWordLocked(int word_idx) {
		if (word_idx < 0 || word_idx >= static_cast<int>(round_word_audio_paths_.size())) {
			return;
		}
		const std::string& path = round_word_audio_paths_[static_cast<size_t>(word_idx)];
		if (path.empty()) {
			return;
		}
		(void)PlayAudioFromSd(path);
	}

	void BuildFallbackRoundLocked(const LevelConfig& cfg) {
		static const std::array<const char*, 10> kFallbackWords = {
			"apple", "dog", "cat", "book", "pen", "sun", "moon", "car", "fish", "milk"};
		round_question_type_ = cfg.question_type;
		round_words_.clear();
		round_word_audio_paths_.clear();
		for (int i = 0; i < cfg.words_per_round && i < static_cast<int>(kFallbackWords.size()); ++i) {
			round_words_.push_back(kFallbackWords[static_cast<size_t>(i)]);
			round_word_audio_paths_.push_back("");
		}
		round_dialog_prompt_ = "要拼写的单词[fallback]: ";
		for (size_t i = 0; i < round_words_.size(); ++i) {
			if (i > 0) {
				round_dialog_prompt_ += " -> ";
			}
			round_dialog_prompt_ += round_words_[i];
		}
		target_words_ = round_words_;
		current_word_idx_ = 0;
		current_char_idx_ = 0;
		next_expected_global_order_ = 0;
		need_gap_growth_before_next_word_ = false;
		pending_type5_word_jump_ = false;
		consumed_tokens_phrase_order_.clear();
		tokens_by_snake_order_.clear();
		ESP_LOGW(kTag, "use fallback words level=%d count=%d", level_, static_cast<int>(round_words_.size()));
	}

	void PrepareDialogTextLocked() {
		dialog_title_ = "Word Snake";
		const LevelConfig cfg = BuildLevelConfig(level_);
		if (round_dialog_prompt_.empty() || round_words_.empty()) {
			if (!PickRoundWordsLocked(cfg)) {
				BuildFallbackRoundLocked(cfg);
				if (ctx_) {
					ctx_->board.GetDisplay()->SetChatMessage("system", "WordSnake: 使用fallback词表");
				}
			}
		}
		char buf[64] = {0};
		std::snprintf(buf,
				  sizeof(buf),
				  "关卡%d  type=%d  词数=%d  干扰=%d",
				  level_,
				  cfg.question_type,
				  cfg.words_per_round,
				  cfg.distractor_count);
		dialog_text_ = buf;
		if (!round_dialog_prompt_.empty()) {
			dialog_text_ += "\n";
			dialog_text_ += round_dialog_prompt_;
		}
	}

	void StartRoundLocked() {
		const LevelConfig cfg = BuildLevelConfig(level_);
		round_question_type_ = cfg.question_type;
		if (round_words_.empty()) {
			if (!PickRoundWordsLocked(cfg)) {
				BuildFallbackRoundLocked(cfg);
				if (ctx_) {
					ctx_->board.GetDisplay()->SetChatMessage("system", "WordSnake: 使用fallback词表");
				}
			}
		} else {
			target_words_ = round_words_;
			current_word_idx_ = 0;
			current_char_idx_ = 0;
			next_expected_global_order_ = 0;
			need_gap_growth_before_next_word_ = false;
			pending_type5_word_jump_ = false;
			type5_word_layouts_.clear();
			consumed_tokens_phrase_order_.clear();
			tokens_by_snake_order_.clear();
		}

		snake_cells_.clear();
		current_dir_ = Dir::Right;
		cached_input_valid_ = false;
		fast_forward_steps_ = 0;
		if (!PlaceLettersLocked(cfg)) {
			phase_ = Phase::Dialog;
			dialog_text_ = "字母摆放失败，请重试";
			ScheduleDrawLocked(EpdManager::TaskType::kPartial, 0);
			return;
		}

		if (round_question_type_ == 5 && !type5_word_layouts_.empty() && type5_word_layouts_[0].valid) {
			snake_cells_.push_front(type5_word_layouts_[0].head_anchor);
			current_dir_ = type5_word_layouts_[0].dir_from_head;
		} else {
			snake_cells_.push_front(Cell{7, 10});
		}

		phase_ = Phase::Running;
		status_text_ = "运行中";
		ScheduleDrawLocked(EpdManager::TaskType::kFast, 0);
	}

	void QueueType5NextWordJumpLocked(int word_idx) {
		if (word_idx < 0 || word_idx >= static_cast<int>(type5_word_layouts_.size())) {
			pending_type5_word_jump_ = false;
			return;
		}
		const Type5WordLayout& layout = type5_word_layouts_[static_cast<size_t>(word_idx)];
		if (!layout.valid) {
			pending_type5_word_jump_ = false;
			return;
		}
		pending_type5_word_jump_ = true;
		pending_type5_jump_head_ = layout.head_anchor;
		pending_type5_jump_dir_ = layout.dir_from_head;
	}

	void ApplyCachedInputLocked() {
		if (!cached_input_valid_) {
			return;
		}
		if (!IsOpposite(current_dir_, cached_input_dir_)) {
			current_dir_ = cached_input_dir_;
		}
		cached_input_valid_ = false;
	}

	bool SnakeOccupiesCellLocked(const Cell& cell, bool ignore_tail_if_moving) const {
		if (snake_cells_.empty()) {
			return false;
		}
		const size_t limit = ignore_tail_if_moving && !snake_cells_.empty() ? snake_cells_.size() - 1 : snake_cells_.size();
		size_t idx = 0;
		for (const auto& c : snake_cells_) {
			if (idx >= limit) {
				break;
			}
			if (c == cell) {
				return true;
			}
			++idx;
		}
		return false;
	}

	bool HasNextExpected() const {
		return current_word_idx_ < static_cast<int>(target_words_.size()) &&
			   current_char_idx_ < static_cast<int>(target_words_[static_cast<size_t>(current_word_idx_)].size());
	}

	void FailRoundLocked(const std::string& reason) {
		phase_ = Phase::Failed;
		dialog_title_ = "失败";
		dialog_text_ = "第" + std::to_string(level_) + "关失败\n原因: " + reason;
		if (ctx_) {
			ctx_->board.GetDisplay()->SetChatMessage("system", "WordSnake: 失败");
		}
	}

	void ClearRoundLocked() {
		phase_ = Phase::Cleared;
		dialog_title_ = "过关";
		dialog_text_ = "第" + std::to_string(level_) + "关完成\n按Start进入下一关";
		if (ctx_) {
			ctx_->board.GetDisplay()->SetChatMessage("system", "WordSnake: 过关");
		}
	}

	void StepMoveLocked() {
		if (phase_ != Phase::Running || snake_cells_.empty()) {
			return;
		}

		const int extra_steps = fast_forward_steps_;
		fast_forward_steps_ = 0;
		int steps_left = 1 + std::max(0, extra_steps);

		while (steps_left > 0 && phase_ == Phase::Running) {
			if (round_question_type_ == 5 && pending_type5_word_jump_) {
				if (SnakeOccupiesCellLocked(pending_type5_jump_head_, false)) {
					FailRoundLocked("接续新单词蛇头失败");
					return;
				}
				snake_cells_.push_front(pending_type5_jump_head_);
				tokens_by_snake_order_.insert(tokens_by_snake_order_.begin(), " ");
				consumed_tokens_phrase_order_.push_back(" ");
				current_dir_ = pending_type5_jump_dir_;
				pending_type5_word_jump_ = false;
				--steps_left;
				continue;
			}

			const Cell old_head = snake_cells_.front();
			const Cell new_head = NextCell(old_head, current_dir_);

			std::string growth_token;
			bool consumed_letter = false;

			const int key = KeyOfCell(new_head);
			auto tile_it = tiles_.find(key);
			if (tile_it != tiles_.end()) {
				LetterTile tile = tile_it->second;
				if (tile.distractor) {
					FailRoundLocked("吃到干扰字母");
					return;
				}
				if (tile.global_order != next_expected_global_order_) {
					FailRoundLocked("字母顺序错误");
					return;
				}
				if (!HasNextExpected()) {
					FailRoundLocked("状态错误");
					return;
				}
				const char expected =
					target_words_[static_cast<size_t>(current_word_idx_)][static_cast<size_t>(current_char_idx_)];
				if (std::tolower(static_cast<unsigned char>(tile.ch)) !=
					std::tolower(static_cast<unsigned char>(expected))) {
					FailRoundLocked("目标字母不匹配");
					return;
				}

				growth_token =
					std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(expected))));
				consumed_letter = true;
				tiles_.erase(tile_it);
				SpeakCurrentWordLocked(current_word_idx_);

				++current_char_idx_;
				++next_expected_global_order_;
				if (current_char_idx_ >=
					static_cast<int>(target_words_[static_cast<size_t>(current_word_idx_)].size())) {
					++current_word_idx_;
					current_char_idx_ = 0;
					if (current_word_idx_ < static_cast<int>(target_words_.size())) {
						if (round_question_type_ == 5) {
							QueueType5NextWordJumpLocked(current_word_idx_);
						} else {
							need_gap_growth_before_next_word_ = true;
						}
					}
				}
			}

			if (!consumed_letter && need_gap_growth_before_next_word_) {
				growth_token = " ";
				need_gap_growth_before_next_word_ = false;
			}

			const bool grows = !growth_token.empty();
			if (SnakeOccupiesCellLocked(new_head, !grows)) {
				FailRoundLocked("撞到自己");
				return;
			}

			snake_cells_.push_front(new_head);
			if (grows) {
				tokens_by_snake_order_.insert(tokens_by_snake_order_.begin(), growth_token);
				consumed_tokens_phrase_order_.push_back(growth_token);
			} else {
				snake_cells_.pop_back();
			}

			if (!grows && !tokens_by_snake_order_.empty()) {
				if (tokens_by_snake_order_.size() > snake_cells_.size() - 1) {
					tokens_by_snake_order_.resize(snake_cells_.size() - 1);
				}
			}

			if (current_word_idx_ >= static_cast<int>(target_words_.size())) {
				ClearRoundLocked();
				return;
			}

			--steps_left;
		}
	}

	void EnsureTimerCreatedLocked() {
		if (tick_timer_ != nullptr) {
			return;
		}
		esp_timer_create_args_t args = {
			.callback = &WordSnakeApp::DispatchTimerCallback,
			.arg = this,
			.dispatch_method = ESP_TIMER_TASK,
			.name = "word_snake_tick",
			.skip_unhandled_events = true,
		};
		if (esp_timer_create(&args, &tick_timer_) != ESP_OK) {
			tick_timer_ = nullptr;
		}
	}

	void StartTimerLocked() {
		if (tick_timer_ == nullptr) {
			return;
		}
		(void)esp_timer_stop(tick_timer_);
		(void)esp_timer_start_periodic(tick_timer_, kDispatchTimerPeriodUs);
	}

	void StopTimerLocked() {
		if (tick_timer_) {
			(void)esp_timer_stop(tick_timer_);
		}
	}

	void DeleteTimerLocked() {
		if (tick_timer_) {
			(void)esp_timer_delete(tick_timer_);
			tick_timer_ = nullptr;
		}
	}

	void ScheduleDrawLocked(EpdManager::TaskType type, int64_t due_us) {
		if (!epd_) {
			return;
		}
		draw_due_us_ = due_us;
		draw_pending_ = true;
		next_draw_task_type_ = type;
	}

	static void DispatchTimerCallback(void* arg) {
		auto* self = static_cast<WordSnakeApp*>(arg);
		if (self) {
			self->OnDispatchTimer();
		}
	}

	void OnDispatchTimer() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (!tick_started_ || !epd_) {
			return;
		}
		if (waiting_refresh_done_ || !draw_pending_) {
			return;
		}
		const int64_t now = esp_timer_get_time();
		if (now < draw_due_us_) {
			return;
		}

		DrawTaskCtx* task_ctx = new DrawTaskCtx();
		if (!task_ctx) {
			return;
		}
		task_ctx->app = this;

		const bool queued = EpdManager::GetInstance().Schedule(
			next_draw_task_type_,
			&WordSnakeApp::DrawCallback,
			task_ctx,
			&WordSnakeApp::DeleteTaskCtx,
			EpdManager::Rect(0, 0, kScreenW, kScreenH),
			&WordSnakeApp::RefreshDoneCallback,
			task_ctx);
		if (!queued) {
			delete task_ctx;
			return;
		}
		waiting_refresh_done_ = true;
		draw_pending_ = false;
	}

	static void DeleteTaskCtx(void* ctx) {
		auto* task_ctx = static_cast<DrawTaskCtx*>(ctx);
		delete task_ctx;
	}

	static void DrawCallback(Adafruit_GFX& gfx, void* ctx) {
		auto* task_ctx = static_cast<DrawTaskCtx*>(ctx);
		if (!task_ctx || !task_ctx->app) {
			return;
		}
		task_ctx->app->Draw(gfx);
	}

	void DrawSnakeHead(Adafruit_GFX& gfx, const Cell& head, Dir dir) {
		const int x = head.col * kCellSize;
		const int y = head.row * kCellSize;
		const int cx = x + kCellSize / 2;
		const int cy = y + kCellSize / 2;
		const int radius = (kCellSize / 2) - 1;
		gfx.fillCircle(cx, cy, radius, GxEPD_WHITE);
		gfx.drawCircle(cx, cy, radius, GxEPD_BLACK);
		switch (dir) {
			case Dir::Right:
				gfx.fillRect(x, y, kCellSize / 2, kCellSize, GxEPD_WHITE);
				gfx.drawFastVLine(x + kCellSize / 2, y, kCellSize, GxEPD_BLACK);
				break;
			case Dir::Left:
				gfx.fillRect(x + kCellSize / 2, y, kCellSize / 2, kCellSize, GxEPD_WHITE);
				gfx.drawFastVLine(x + kCellSize / 2, y, kCellSize, GxEPD_BLACK);
				break;
			case Dir::Down:
				gfx.fillRect(x, y, kCellSize, kCellSize / 2, GxEPD_WHITE);
				gfx.drawFastHLine(x, y + kCellSize / 2, kCellSize, GxEPD_BLACK);
				break;
			case Dir::Up:
				gfx.fillRect(x, y + kCellSize / 2, kCellSize, kCellSize / 2, GxEPD_WHITE);
				gfx.drawFastHLine(x, y + kCellSize / 2, kCellSize, GxEPD_BLACK);
				break;
		}
	}

	void DrawBodyText(Adafruit_GFX& gfx) {
		if (!epd_ || snake_cells_.size() <= 1) {
			return;
		}

		std::vector<std::pair<int, Cell>> body_cells;
		body_cells.reserve(snake_cells_.size() - 1);
		for (size_t i = 1; i < snake_cells_.size(); ++i) {
			const Cell& c = snake_cells_[i];
			body_cells.push_back({c.row * kGridCols + c.col + 1, c});
		}

		std::vector<std::string> tokens(body_cells.size(), "");
		std::sort(body_cells.begin(), body_cells.end(), [](const auto& a, const auto& b) {
			return a.first < b.first;
		});
		for (size_t i = 0; i < body_cells.size() && i < consumed_tokens_phrase_order_.size(); ++i) {
			tokens[i] = consumed_tokens_phrase_order_[i];
		}

		for (size_t i = 0; i < body_cells.size(); ++i) {
			const Cell c = body_cells[i].second;
			const int x = c.col * kCellSize;
			const int y = c.row * kCellSize;
			gfx.drawRect(x, y, kCellSize, kCellSize, GxEPD_BLACK);

			std::string text = tokens[i];
			if (text.empty()) {
				continue;
			}
			if (text == " ") {
				text = "_";
			}
			const int tw = static_cast<int>(epd_->MeasureUtf8Width(text, kFont));
			const int tx = x + std::max(0, (kCellSize - tw) / 2);
			const int ty = y + 14;
			epd_->DrawUtf8(tx, ty, text, kFont, GxEPD_BLACK);
		}
	}

	void Draw(Adafruit_GFX& gfx) {
		std::lock_guard<std::mutex> lock(mutex_);
		gfx.fillScreen(GxEPD_WHITE);

		if (phase_ != Phase::Running) {
			const int w = 200;
			const int h = 150;
			const int x = (kScreenW - w) / 2;
			const int y = (kScreenH - h) / 2;
			gfx.drawRect(x, y, w, h, GxEPD_BLACK);
			epd_->DrawUtf8(x + 10, y + 20, dialog_title_, kFont, GxEPD_BLACK);

			auto lines = WrapUtf8Text(dialog_text_, w - 20, epd_, kFont);
			int line_y = y + 40;
			for (const auto& line : lines) {
				if (line_y > y + h - 35) {
					break;
				}
				epd_->DrawUtf8(x + 10, line_y, line, kFont, GxEPD_BLACK);
				line_y += 14;
			}

			const char* footer = "Press Start";
			if (phase_ == Phase::Failed) {
				footer = "Press Start Retry";
			} else if (phase_ == Phase::Cleared) {
				footer = "Press Start Next";
			}
			epd_->DrawUtf8(x + 20, y + h - 16, footer, kFont, GxEPD_BLACK);
			return;
		}

		for (const auto& pair : tiles_) {
			const Cell c = CellFromKey(pair.first);
			const LetterTile& tile = pair.second;
			const int x = c.col * kCellSize;
			const int y = c.row * kCellSize;
			gfx.drawRect(x, y, kCellSize, kCellSize, GxEPD_BLACK);
			const std::string letter(1, static_cast<char>(std::toupper(static_cast<unsigned char>(tile.ch))));
			const int tw = static_cast<int>(epd_->MeasureUtf8Width(letter, kFont));
			epd_->DrawUtf8(x + std::max(0, (kCellSize - tw) / 2), y + 14, letter, kFont, GxEPD_BLACK);
		}

		if (round_question_type_ == 5) {
			for (const auto& layout : type5_word_layouts_) {
				if (!layout.valid) {
					continue;
				}
				DrawSnakeHead(gfx, layout.head_anchor, layout.dir_from_head);
			}
		}

		if (!snake_cells_.empty()) {
			DrawSnakeHead(gfx, snake_cells_.front(), current_dir_);
			DrawBodyText(gfx);
		}

		return;
	}

	static void RefreshDoneCallback(EpdManager::TaskType requested,
							EpdManager::TaskType effective,
							int64_t start_us,
							int64_t end_us,
							bool ok,
							void* done_ctx) {
		(void)requested;
		(void)effective;
		(void)start_us;
		(void)ok;
		auto* task_ctx = static_cast<DrawTaskCtx*>(done_ctx);
		if (!task_ctx || !task_ctx->app) {
			return;
		}
		task_ctx->app->HandleRefreshDone(end_us);
	}

	void HandleRefreshDone(int64_t end_us) {
		std::lock_guard<std::mutex> lock(mutex_);
		waiting_refresh_done_ = false;
		if (phase_ == Phase::Running) {
			ApplyCachedInputLocked();
			StepMoveLocked();
			ScheduleDrawLocked(EpdManager::TaskType::kPartial, end_us + kDecisionWindowUs);
		} else {
			ScheduleDrawLocked(EpdManager::TaskType::kPartial, end_us + kDecisionWindowUs);
		}
	}

private:
	std::mutex mutex_;
	AppContext* ctx_ = nullptr;
	CustomEpdDisplay* epd_ = nullptr;
	Phase phase_ = Phase::Dialog;

	int level_ = 1;
	bool display_by_grid_position_ = false;
	std::string dialog_title_;
	std::string dialog_text_;
	std::string round_dialog_prompt_;
	std::string status_text_ = "按Start开始";

	std::vector<QuestionRow> question_type2_pool_;
	std::vector<QuestionRow> question_type5_pool_;
	std::vector<std::string> round_words_;
	std::vector<std::string> round_word_audio_paths_;

	std::vector<std::string> target_words_;
	int round_question_type_ = 2;
	int current_word_idx_ = 0;
	int current_char_idx_ = 0;
	int next_expected_global_order_ = 0;
	bool need_gap_growth_before_next_word_ = false;
	std::vector<Type5WordLayout> type5_word_layouts_;
	bool pending_type5_word_jump_ = false;
	Cell pending_type5_jump_head_{0, 0};
	Dir pending_type5_jump_dir_ = Dir::Right;

	std::deque<Cell> snake_cells_;
	Dir current_dir_ = Dir::Right;
	bool cached_input_valid_ = false;
	Dir cached_input_dir_ = Dir::Right;
	int fast_forward_steps_ = 0;

	std::vector<std::string> tokens_by_snake_order_;
	std::vector<std::string> consumed_tokens_phrase_order_;

	std::unordered_map<int, LetterTile> tiles_;

	esp_timer_handle_t tick_timer_ = nullptr;
	bool tick_started_ = false;
	bool waiting_refresh_done_ = false;
	bool draw_pending_ = false;
	int64_t draw_due_us_ = 0;
	EpdManager::TaskType next_draw_task_type_ = EpdManager::TaskType::kPartial;
	uint32_t saved_partial_force_fast_every_n_ = 30;
};

} // namespace

std::unique_ptr<AppBase> MakeWordSnakeApp() {
	return std::make_unique<WordSnakeApp>();
}
