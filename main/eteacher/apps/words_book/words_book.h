#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "eteacher/app_manager/app_base.h"

class WordsBookApp : public AppBase {
public:
    WordsBookApp() {}

    MenuMeta GetMenuMeta() const override;

    void OnEnter(AppContext &ctx) override;
    void OnExit(AppContext &ctx) override;
    void OnButton(AppContext &ctx, const ButtonEvent &event) override;
    bool ShouldInterceptSelectExit() const override;

private:
    struct EntryData {
        bool found = false;
        std::vector<std::pair<std::string, std::string>> fields;
    };

    void LoadWordsFromBook();
    void BuildRows();
    void MoveSelectionUp();
    void MoveSelectionDown();
    void MoveSelectionLeft();
    void MoveSelectionRight();
    void EnsureSelectionVisible();
    void ShowSelectedWordDetail();
    bool RemoveWordFromBook(const std::string &word) const;
    void RenderList(AppContext &ctx) const;
    void RenderDetail(AppContext &ctx) const;

    EntryData QueryByWord(const std::string &word) const;
    std::string FormatEntryText(const EntryData &entry) const;

    std::vector<std::string> words_{};
    std::vector<std::vector<size_t>> rows_{};
    std::vector<std::pair<int, int>> word_pos_{};

    size_t selected_word_index_ = 0;
    size_t top_row_ = 0;
    bool detail_mode_ = false;
    std::string detail_text_{};
    std::string detail_word_{};
};

std::unique_ptr<AppBase> MakeWordsBookApp();
