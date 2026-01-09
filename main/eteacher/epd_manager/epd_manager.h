#pragma once

#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "boards/EnglishTeacher/custom_epd_display.h"

class EpdManager {
public:
	enum class TaskType : uint8_t {
		kPartial = 0,
		kFast = 1,
		kFull = 2,
	};

	struct Rect {
		int16_t x;
		int16_t y;
		int16_t w;
		int16_t h;

		constexpr Rect(int16_t x_ = 0, int16_t y_ = 0, int16_t w_ = 0, int16_t h_ = 0)
			: x(x_), y(y_), w(w_), h(h_) {}
	};

	static EpdManager& GetInstance();

	// Must be called once after the EPD display is ready.
	// Safe to call multiple times; subsequent calls are ignored.
	void Init(CustomEpdDisplay* epd);

	// Optional tuning
	void SetPartialForceFastEveryN(uint32_t n);
	uint32_t GetPartialForceFastEveryN() const { return partial_force_fast_every_n_; }

	// Schedule a refresh task.
	// - For kPartial: rect is used (partial window)
	// - For kFast/kFull: rect is ignored (full screen)
	// cb should draw the full content; it may be called multiple times (once per page)
	// inside EpdManager-managed firstPage()/nextPage() loops. Do NOT call firstPage/nextPage in cb.
	// ctx_deleter will be called (in EpdManager task) after execution.
	bool Schedule(TaskType type,
			  Epd::DrawCallback cb,
			  void* ctx = nullptr,
			  void (*ctx_deleter)(void*) = nullptr,
			  Rect rect = Rect());

private:
	EpdManager() = default;
	EpdManager(const EpdManager&) = delete;
	EpdManager& operator=(const EpdManager&) = delete;

	struct TaskItem {
		TaskType type;
		Rect rect;
		Epd::DrawCallback cb;
		void* ctx;
		void (*ctx_deleter)(void*);
	};

	static void TaskEntry(void* arg);
	void Run();

	// Returns effective type after applying partial->fast promotion rules.
	TaskType ResolveType(TaskType requested);

	CustomEpdDisplay* epd_ = nullptr;
	QueueHandle_t queue_ = nullptr;
	TaskHandle_t task_ = nullptr;

	uint32_t partial_force_fast_every_n_ = 10;
	uint32_t partial_count_since_fast_ = 0;

	int64_t last_partial_us_ = 0;
	int64_t last_fast_us_ = 0;
	int64_t last_full_us_ = 0;

	bool inited_ = false;
};
