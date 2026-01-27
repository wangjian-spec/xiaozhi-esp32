#include "eteacher/app_service/tool/nav_sequence.h"

namespace eteacher::nav {

std::vector<AppButton> ToAppButtonSequence(const seq_event_t* evt) {
	std::vector<AppButton> seq;
	if (!evt || evt->len == 0) {
		return seq;
	}
	seq.reserve(evt->len);
	for (size_t i = 0; i < evt->len; ++i) {
		switch (evt->buttons[i]) {
			case SEQ_BTN_UP:
				seq.push_back(AppButton::Up);
				break;
			case SEQ_BTN_DOWN:
				seq.push_back(AppButton::Down);
				break;
			case SEQ_BTN_LEFT:
				seq.push_back(AppButton::Left);
				break;
			case SEQ_BTN_RIGHT:
				seq.push_back(AppButton::Right);
				break;
			default:
				break;
		}
	}
	return seq;
}

void ApplyGridMoveSequence(int& row, int& col, int rows, int cols, const std::vector<AppButton>& seq) {
	if (rows <= 0 || cols <= 0 || seq.empty()) {
		return;
	}
	for (auto id : seq) {
		switch (id) {
			case AppButton::Up:
				row = (row - 1 + rows) % rows;
				break;
			case AppButton::Down:
				row = (row + 1) % rows;
				break;
			case AppButton::Left:
				col = (col - 1 + cols) % cols;
				break;
			case AppButton::Right:
				col = (col + 1) % cols;
				break;
			default:
				break;
		}
	}
}

} // namespace eteacher::nav
