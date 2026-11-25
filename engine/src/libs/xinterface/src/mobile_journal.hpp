#pragma once
#include "attributes.h"
#include "mobile_widget.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The journal's quest list for the touch UI (CXI_QUESTTITLE). The native rows hold a screenful of wrapped titles; the
// touch UI scrolls the whole list, by the scripts' own attribute index and quest id.
class MobileQuestList
{
    std::vector<MobileWidget> rows_;
    int top_ = 0;

  public:
    // From the scripts' quest attributes, the first one the native list shows at `top`; title(log, id, out): the quest
    // file reader's title (false: none, the quest is left out).
    template <class Title> void refresh(const char *node, int order, ATTRIBUTES *quests, int top, Title &&title)
    {
        rows_.clear();
        top_ = top;
        if (!quests)
            return;
        for (int i = 0; i < static_cast<int>(quests->GetAttributesNum()); ++i)
        {
            auto *quest = quests->GetAttributeClass(i);
            if (!quest)
                continue;
            const char *log = quest->GetAttribute("LogName");
            std::string text;
            if (!title(log ? log : quest->GetThisName(), quest->GetThisName(), text))
                continue;
            // The reader's string carries an explicit C terminator.
            MobileWidget row{node, "choice",  text.c_str(), quest->GetAttributeAsDword("Complete", 0) ? "1" : "0",
                             i,    order + i, true};
            row.rowId = quest->GetThisName();
            row.actions.push_back("open");
            rows_.push_back(std::move(row));
        }
    }
    // The node is released: no rows until the next refresh.
    void clear()
    {
        rows_.clear();
    }
    void collect(const char *node, int order, bool enabled, std::vector<MobileWidget> &widgets) const
    {
        if (rows_.empty())
            widgets.push_back({node, "text", "", "", -1, order, false});
        for (auto row : rows_)
        {
            row.enabled = enabled;
            if (!enabled)
                row.actions.clear();
            widgets.push_back(std::move(row));
        }
    }
    // The quest an "open" action of the screen revision asks for: its row relative to the native list's top, which
    // the scripts' QuestActivate takes; nullopt for an action of another revision or row.
    std::optional<int> opened(int index, const std::string &value, uint64_t revision) const
    {
        for (const auto &row : rows_)
            if (row.index == index && value == "open@" + std::to_string(revision) + ":" + row.rowId)
                return index - top_;
        return std::nullopt;
    }
};

// A journal record's paragraphs appended to the whole text the touch UI shows (CXI_QUESTTEXTS): a blank line between
// records, one line per paragraph, independent of the native line wrapping.
inline void MobileJournalAppend(std::string &text, const std::vector<std::string> &paragraphs)
{
    if (!text.empty() && !paragraphs.empty())
        text += "\n\n";
    for (const auto &paragraph : paragraphs)
    {
        if (!text.empty() && text.back() != '\n')
            text += '\n';
        text += paragraph.c_str();
    }
}
