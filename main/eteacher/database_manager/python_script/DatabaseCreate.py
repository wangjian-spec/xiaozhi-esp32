from __future__ import annotations

import json
import random
import sqlite3
from dataclasses import dataclass
from datetime import date, timedelta
from pathlib import Path


ROW_COUNT = 100
RANDOM_SEED = 20260225


@dataclass(frozen=True)
class DbPaths:
	words_db: Path
	question_db: Path
	user_data_db: Path


def resolve_paths() -> DbPaths:
	script_dir = Path(__file__).resolve().parent
	script_dir.mkdir(parents=True, exist_ok=True)
	return DbPaths(
		words_db=script_dir / "words.db",
		question_db=script_dir / "question.db",
		user_data_db=script_dir / "user_data.db",
	)


def reset_db(path: Path) -> sqlite3.Connection:
	if path.exists():
		path.unlink()
	conn = sqlite3.connect(path)
	conn.execute("PRAGMA journal_mode = DELETE")
	conn.execute("PRAGMA synchronous = NORMAL")
	return conn


def create_words_schema(conn: sqlite3.Connection) -> None:
	conn.execute(
		"""
		CREATE TABLE word_dictionary (
			id INTEGER PRIMARY KEY,
			word TEXT NOT NULL,
			phonetic TEXT,
			meaning_zh TEXT,
			meaning_en TEXT,
			tags TEXT,
			forms TEXT,
			example1 TEXT,
			example2 TEXT,
			example3 TEXT
		)
		"""
	)


def create_question_schema(conn: sqlite3.Connection) -> None:
	conn.execute(
		"""
		CREATE TABLE question_bank (
			id INTEGER PRIMARY KEY,
			question_type TEXT,
			stage TEXT,
			difficulty INTEGER,
			content_json TEXT,
			answer TEXT,
			audio_path TEXT,
			image_path TEXT
		)
		"""
	)


def create_user_schema(conn: sqlite3.Connection) -> None:
	conn.executescript(
		"""
		CREATE TABLE users (
			id INTEGER PRIMARY KEY,
			nickname TEXT,
			avatar TEXT,
			level INTEGER DEFAULT 1,
			created_at INTEGER,
			last_active_at INTEGER
		);

		CREATE TABLE devices (
			id TEXT PRIMARY KEY,
			name TEXT,
			model TEXT,
			firmware_ver TEXT,
			last_seen_at INTEGER
		);

		CREATE TABLE user_device_bindings (
			user_id INTEGER,
			device_id TEXT,
			bind_at INTEGER,
			PRIMARY KEY (user_id, device_id)
		);

		CREATE TABLE vocab_items (
			id INTEGER PRIMARY KEY,
			user_id INTEGER,
			entry_id INTEGER,
			added_at INTEGER,
			source TEXT,
			is_favorite INTEGER DEFAULT 0,
			is_difficult INTEGER DEFAULT 0,
			is_mastered INTEGER DEFAULT 0,
			is_deleted INTEGER DEFAULT 0
		);

		CREATE TABLE vocab_learning_state (
			user_id INTEGER,
			vocab_id INTEGER,
			familiarity REAL DEFAULT 0,
			ease_factor REAL DEFAULT 2.5,
			interval_days INTEGER DEFAULT 1,
			repetition INTEGER DEFAULT 0,
			last_review_at INTEGER,
			next_review_at INTEGER,
			lapses INTEGER DEFAULT 0,
			stability REAL DEFAULT 0,
			PRIMARY KEY(user_id, vocab_id)
		);

		CREATE TABLE vocab_review_log (
			id INTEGER PRIMARY KEY,
			user_id INTEGER,
			vocab_id INTEGER,
			review_type TEXT,
			rating INTEGER,
			response_time INTEGER,
			is_correct INTEGER,
			created_at INTEGER
		);

		CREATE TABLE ai_sessions (
			id INTEGER PRIMARY KEY,
			user_id INTEGER,
			session_type TEXT,
			topic TEXT,
			started_at INTEGER,
			ended_at INTEGER,
			total_duration INTEGER,
			created_at INTEGER
		);

		CREATE TABLE ai_messages (
			id INTEGER PRIMARY KEY,
			session_id INTEGER,
			role TEXT,
			content TEXT,
			audio_path TEXT,
			created_at INTEGER
		);

		CREATE TABLE learning_stats_daily (
			user_id INTEGER,
			date TEXT,
			reviews INTEGER DEFAULT 0,
			correct INTEGER DEFAULT 0,
			wrong INTEGER DEFAULT 0,
			new_words INTEGER DEFAULT 0,
			study_time_sec INTEGER DEFAULT 0,
			PRIMARY KEY (user_id, date)
		);

		CREATE TABLE ai_speech_evaluations (
			id INTEGER PRIMARY KEY,
			user_id INTEGER,
			session_id INTEGER,
			message_id INTEGER,
			pronunciation_score REAL,
			fluency_score REAL,
			grammar_score REAL,
			overall_score REAL,
			feedback_text TEXT,
			created_at INTEGER
		);

		CREATE TABLE ai_detected_errors (
			id INTEGER PRIMARY KEY,
			user_id INTEGER,
			session_id INTEGER,
			message_id INTEGER,
			entry_id INTEGER,
			error_type TEXT,
			severity INTEGER,
			created_at INTEGER
		);

		CREATE TABLE tasks (
			id INTEGER PRIMARY KEY,
			user_id INTEGER,
			task_type TEXT,
			target_id INTEGER,
			title TEXT,
			description TEXT,
			start_at INTEGER,
			due_at INTEGER,
			is_completed INTEGER DEFAULT 0,
			is_deleted INTEGER DEFAULT 0,
			created_at INTEGER
		);

		CREATE TABLE game_profile (
			user_id INTEGER PRIMARY KEY,
			level INTEGER DEFAULT 1,
			exp INTEGER DEFAULT 0,
			coins INTEGER DEFAULT 0,
			streak_days INTEGER DEFAULT 0,
			last_play_at INTEGER
		);

		CREATE TABLE game_rewards_log (
			id INTEGER PRIMARY KEY,
			user_id INTEGER,
			reward_type TEXT,
			value INTEGER,
			reason TEXT,
			created_at INTEGER
		);

		CREATE TABLE sync_state (
			table_name TEXT PRIMARY KEY,
			last_sync_at INTEGER,
			last_row_id INTEGER
		);
		"""
	)


def seed_words(conn: sqlite3.Connection) -> None:
	tags_pool = ["小学", "中考", "高考", "四级", "六级", "考研", "托福", "雅思", "GRE"]
	word_bases = [
		"apple",
		"bridge",
		"create",
		"discover",
		"energy",
		"future",
		"global",
		"honest",
		"improve",
		"journey",
	]
	rows = []
	for i in range(1, ROW_COUNT + 1):
		base = word_bases[(i - 1) % len(word_bases)]
		word = f"{base}{i}"
		phonetic = f"/{base[:3]}{i % 7}/" if i % 11 else None
		pos = ["n.", "v.", "adj.", "adv."][(i - 1) % 4]
		meaning_zh = f"{pos} 示例中文含义{i}"
		meaning_en = f"{pos} sample English meaning {i}"
		tags = ",".join(random.sample(tags_pool, k=(i % 3) + 1))
		forms = (
			json.dumps(
				{
					"plural": f"{word}s",
					"past": f"{word}ed",
					"ing": f"{word}ing",
				},
				ensure_ascii=False,
			)
			if i % 9
			else None
		)
		example1 = f"I use {word} in a simple sentence."
		example2 = f"{word.capitalize()} appears in reading practice {i % 12}." if i % 5 else None
		example3 = f"Can you remember {word} tomorrow?" if i % 7 else None
		rows.append((i, word, phonetic, meaning_zh, meaning_en, tags, forms, example1, example2, example3))

	conn.executemany(
		"""
		INSERT INTO word_dictionary
		(id, word, phonetic, meaning_zh, meaning_en, tags, forms, example1, example2, example3)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
		""",
		rows,
	)


def build_question_content(question_type: str, idx: int) -> tuple[str, str]:
	if question_type == "choice":
		prompt = f"Choose the best meaning for word_{idx}."
		options = {
			"A": f"option_A_{idx}",
			"B": f"option_B_{idx}",
			"C": f"option_C_{idx}",
			"D": f"option_D_{idx}",
		}
		answer = random.choice(list(options.keys()))
		payload = {"question": prompt, "options": options}
		return json.dumps(payload, ensure_ascii=False), answer
	if question_type == "fill":
		answer = f"keyword_{idx}"
		payload = {
			"question": f"Fill in the blank: Learning English is ____ ({idx}).",
			"hint": "Use one adjective",
		}
		return json.dumps(payload, ensure_ascii=False), answer
	if question_type == "listen":
		answer = f"listen_answer_{idx % 10}"
		payload = {
			"question": "Listen to audio and write what you hear.",
			"length": f"{2 + idx % 5}s",
		}
		return json.dumps(payload, ensure_ascii=False), answer

	answer = f"speak_answer_{idx % 8}"
	payload = {
		"question": "Read the sentence aloud clearly.",
		"text": f"Speaking drill sentence {idx}.",
	}
	return json.dumps(payload, ensure_ascii=False), answer


def seed_questions(conn: sqlite3.Connection) -> None:
	types = ["choice", "fill", "listen", "speak"]
	stages = ["primary", "middle", "high", "cet4", "cet6"]
	rows = []
	for i in range(1, ROW_COUNT + 1):
		question_type = types[(i - 1) % len(types)]
		stage = stages[(i - 1) % len(stages)]
		difficulty = ((i - 1) % 6) + 1
		content_json, answer = build_question_content(question_type, i)
		audio_path = f"audio/question_{i:03d}.ogg" if question_type in {"listen", "speak"} else None
		image_path = f"image/question_{i:03d}.png" if i % 4 == 0 else None
		rows.append((i, question_type, stage, difficulty, content_json, answer, audio_path, image_path))

	conn.executemany(
		"""
		INSERT INTO question_bank
		(id, question_type, stage, difficulty, content_json, answer, audio_path, image_path)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?)
		""",
		rows,
	)


def seed_user_data(conn: sqlite3.Connection) -> None:
	ts_base = 1735689600
	users = []
	devices = []
	bindings = []
	vocab_items = []
	learning_states = []
	review_logs = []
	ai_sessions = []
	ai_messages = []
	stats_daily = []
	speech_evals = []
	detected_errors = []
	tasks = []
	game_profiles = []
	rewards = []
	sync_states = []

	nick_prefix = ["Tom", "Lucy", "Alex", "Mia", "Noah", "Emma", "Leo", "Lily"]
	model_pool = ["EnglishTeacher v1", "EnglishTeacher v1 Pro", "EnglishTeacher Lite"]
	firmware_pool = ["1.0.0", "1.1.2", "1.2.0", "1.3.5-beta"]
	sources = ["lesson", "ai_chat", "manual", "game_reward", "review_wrong"]
	review_types = ["flashcard", "spelling", "dictation", "game"]
	session_types = ["free_talk", "lesson", "roleplay"]
	task_types = ["review", "new_words", "custom"]
	reward_types = ["exp", "coins", "item"]
	reward_reasons = ["review", "streak", "task"]
	error_types = ["pronunciation", "grammar", "usage"]

	for i in range(1, ROW_COUNT + 1):
		created_at = ts_base + i * 3600
		last_active_at = created_at + random.randint(600, 600000)
		users.append(
			(
				i,
				f"{nick_prefix[(i - 1) % len(nick_prefix)]}_{i:03d}",
				f"avatar_{(i % 20) + 1}.png" if i % 13 else None,
				1 + (i % 20),
				created_at,
				last_active_at,
			)
		)

		device_id = f"ESP32S3_{i:06d}"
		devices.append(
			(
				device_id,
				f"Device-{i:03d}" if i % 17 else "Study Room Device",
				model_pool[(i - 1) % len(model_pool)],
				firmware_pool[(i - 1) % len(firmware_pool)],
				last_active_at + random.randint(60, 30000),
			)
		)
		bindings.append((i, device_id, created_at + random.randint(60, 3600)))

		is_favorite = 1 if i % 6 == 0 else 0
		is_difficult = 1 if i % 7 in (0, 1) else 0
		is_mastered = 1 if i % 10 == 0 else 0
		is_deleted = 1 if i % 33 == 0 else 0
		vocab_items.append(
			(
				i,
				i,
				i,
				created_at + random.randint(300, 7200),
				sources[(i - 1) % len(sources)],
				is_favorite,
				is_difficult,
				is_mastered,
				is_deleted,
			)
		)

		familiarity = round(min(1.0, (i % 11) / 10 + random.random() * 0.05), 2)
		ease_factor = round(1.3 + (i % 15) * 0.11, 2)
		interval_days = 1 + (i % 30)
		repetition = i % 12
		last_review_at = created_at + random.randint(1200, 90000)
		next_review_at = last_review_at + interval_days * 86400
		lapses = 1 if i % 9 == 0 else 0
		stability = round(interval_days * (ease_factor / 2.5), 2)
		learning_states.append(
			(
				i,
				i,
				familiarity,
				ease_factor,
				interval_days,
				repetition,
				last_review_at,
				next_review_at,
				lapses,
				stability,
			)
		)

		rating = i % 6
		is_correct = 1 if rating >= 3 else 0
		review_logs.append(
			(
				i,
				i,
				i,
				review_types[(i - 1) % len(review_types)],
				rating,
				500 + (i * 137) % 6000,
				is_correct,
				created_at + random.randint(2000, 150000),
			)
		)

		started_at = created_at + random.randint(4000, 180000)
		duration = 60 + (i * 83) % 1800
		ended_at = started_at + duration
		ai_sessions.append(
			(
				i,
				i,
				session_types[(i - 1) % len(session_types)],
				["daily life", "school", "travel", "science", "sports"][i % 5],
				started_at,
				ended_at,
				duration,
				started_at,
			)
		)

		role = "user" if i % 2 else "ai"
		ai_messages.append(
			(
				i,
				((i - 1) % ROW_COUNT) + 1,
				role,
				f"{role} message sample content #{i}",
				f"audio/session_{((i - 1) % ROW_COUNT) + 1:03d}_msg_{i:03d}.ogg" if role == "user" and i % 3 == 0 else None,
				started_at + random.randint(5, max(6, duration - 1)),
			)
		)

		log_date = date(2025, 1, 1) + timedelta(days=(i - 1) % 45)
		reviews = 5 + (i * 3) % 60
		correct = max(0, reviews - (i % 8))
		wrong = reviews - correct
		stats_daily.append(
			(
				i,
				log_date.isoformat(),
				reviews,
				correct,
				wrong,
				i % 15,
				300 + (i * 95) % 5400,
			)
		)

		pronunciation = round(45 + (i * 1.7) % 55, 1)
		fluency = round(40 + (i * 1.9) % 60, 1)
		grammar = round(42 + (i * 1.5) % 58, 1)
		overall = round((pronunciation + fluency + grammar) / 3, 1)
		speech_evals.append(
			(
				i,
				i,
				((i - 1) % ROW_COUNT) + 1,
				((i - 1) % ROW_COUNT) + 1,
				pronunciation,
				fluency,
				grammar,
				overall,
				[
					"发音清晰，继续保持。",
					"语速偏快，注意停顿。",
					"语法结构基本正确。",
					"重音位置需要优化。",
				][i % 4],
				ended_at,
			)
		)

		detected_errors.append(
			(
				i,
				i,
				((i - 1) % ROW_COUNT) + 1,
				((i - 1) % ROW_COUNT) + 1,
				((i - 1) % ROW_COUNT) + 1,
				error_types[(i - 1) % len(error_types)],
				1 + (i % 5),
				ended_at + random.randint(1, 120),
			)
		)

		start_at = created_at + random.randint(1000, 120000)
		due_at = start_at + random.randint(3600, 7 * 86400)
		tasks.append(
			(
				i,
				i,
				task_types[(i - 1) % len(task_types)],
				((i - 1) % ROW_COUNT) + 1,
				f"Task #{i}: {task_types[(i - 1) % len(task_types)]}",
				[
					"完成今日复习并提交结果",
					"学习5个新单词并造句",
					"自定义口语训练任务",
				][(i - 1) % 3],
				start_at,
				due_at,
				1 if i % 4 == 0 else 0,
				1 if i % 29 == 0 else 0,
				created_at,
			)
		)

		game_profiles.append(
			(
				i,
				1 + (i % 30),
				(i * 120) % 12000,
				(i * 35) % 5000,
				i % 21,
				last_active_at,
			)
		)

		rewards.append(
			(
				i,
				i,
				reward_types[(i - 1) % len(reward_types)],
				5 + (i * 7) % 200,
				reward_reasons[(i - 1) % len(reward_reasons)],
				created_at + random.randint(120, 160000),
			)
		)

		sync_states.append(
			(
				f"logical_table_{i:03d}",
				ts_base + i * 1800,
				i * 10,
			)
		)

	conn.executemany(
		"INSERT INTO users (id, nickname, avatar, level, created_at, last_active_at) VALUES (?, ?, ?, ?, ?, ?)",
		users,
	)
	conn.executemany(
		"INSERT INTO devices (id, name, model, firmware_ver, last_seen_at) VALUES (?, ?, ?, ?, ?)",
		devices,
	)
	conn.executemany(
		"INSERT INTO user_device_bindings (user_id, device_id, bind_at) VALUES (?, ?, ?)",
		bindings,
	)
	conn.executemany(
		"""
		INSERT INTO vocab_items
		(id, user_id, entry_id, added_at, source, is_favorite, is_difficult, is_mastered, is_deleted)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
		""",
		vocab_items,
	)
	conn.executemany(
		"""
		INSERT INTO vocab_learning_state
		(user_id, vocab_id, familiarity, ease_factor, interval_days, repetition, last_review_at, next_review_at, lapses, stability)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
		""",
		learning_states,
	)
	conn.executemany(
		"""
		INSERT INTO vocab_review_log
		(id, user_id, vocab_id, review_type, rating, response_time, is_correct, created_at)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?)
		""",
		review_logs,
	)
	conn.executemany(
		"""
		INSERT INTO ai_sessions
		(id, user_id, session_type, topic, started_at, ended_at, total_duration, created_at)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?)
		""",
		ai_sessions,
	)
	conn.executemany(
		"""
		INSERT INTO ai_messages
		(id, session_id, role, content, audio_path, created_at)
		VALUES (?, ?, ?, ?, ?, ?)
		""",
		ai_messages,
	)
	conn.executemany(
		"""
		INSERT INTO learning_stats_daily
		(user_id, date, reviews, correct, wrong, new_words, study_time_sec)
		VALUES (?, ?, ?, ?, ?, ?, ?)
		""",
		stats_daily,
	)
	conn.executemany(
		"""
		INSERT INTO ai_speech_evaluations
		(id, user_id, session_id, message_id, pronunciation_score, fluency_score, grammar_score, overall_score, feedback_text, created_at)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
		""",
		speech_evals,
	)
	conn.executemany(
		"""
		INSERT INTO ai_detected_errors
		(id, user_id, session_id, message_id, entry_id, error_type, severity, created_at)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?)
		""",
		detected_errors,
	)
	conn.executemany(
		"""
		INSERT INTO tasks
		(id, user_id, task_type, target_id, title, description, start_at, due_at, is_completed, is_deleted, created_at)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
		""",
		tasks,
	)
	conn.executemany(
		"INSERT INTO game_profile (user_id, level, exp, coins, streak_days, last_play_at) VALUES (?, ?, ?, ?, ?, ?)",
		game_profiles,
	)
	conn.executemany(
		"INSERT INTO game_rewards_log (id, user_id, reward_type, value, reason, created_at) VALUES (?, ?, ?, ?, ?, ?)",
		rewards,
	)
	conn.executemany(
		"INSERT INTO sync_state (table_name, last_sync_at, last_row_id) VALUES (?, ?, ?)",
		sync_states,
	)


def generate_databases() -> DbPaths:
	random.seed(RANDOM_SEED)
	paths = resolve_paths()

	words_conn = reset_db(paths.words_db)
	question_conn = reset_db(paths.question_db)
	user_conn = reset_db(paths.user_data_db)

	try:
		create_words_schema(words_conn)
		seed_words(words_conn)
		words_conn.commit()

		create_question_schema(question_conn)
		seed_questions(question_conn)
		question_conn.commit()

		create_user_schema(user_conn)
		seed_user_data(user_conn)
		user_conn.commit()
	finally:
		words_conn.close()
		question_conn.close()
		user_conn.close()

	return paths


def main() -> None:
	paths = generate_databases()
	print("Databases generated:")
	print(f"- {paths.words_db}")
	print(f"- {paths.question_db}")
	print(f"- {paths.user_data_db}")
	print("Each table contains 100 records.")


if __name__ == "__main__":
	main()
