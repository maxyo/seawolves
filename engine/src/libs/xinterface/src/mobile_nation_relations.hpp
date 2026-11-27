#pragma once
#include "core.h"
#include "mobile_action_frame.hpp"
#include "mobile_image_bridge.hpp"
#include "mobile_widget.hpp"
#include "shared/interface/messages.h"
#include "string_compare.hpp"
#include "xdefines.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace storm::mobile
{
// The hero's standing with a nation as the game keeps it (interface adapter android_nations.c): the number behind the
// reputation word and that word's tone ("" neutral, "hostile", "good").
struct NationStanding
{
    std::string score, tone;
};

// Source-only dynamic CreateImage records. Positions associate an actual relation
// with its actual labelled axes; they never identify a country or relation value.
struct NationImage
{
    std::string node, caption;
    int x = 0, y = 0;
    ImageRef image;
};
inline int NationPictureNumber(const std::string &name)
{
    if (name.size() < 4 || (name[0] != 'p' && name[0] != 'P') || (name[1] != 'i' && name[1] != 'I') ||
        (name[2] != 'c' && name[2] != 'C'))
        return -1;
    int n = 0;
    for (size_t i = 3; i < name.size(); ++i)
    {
        if (name[i] < '0' || name[i] > '9' || n > 1000)
            return -1;
        n = n * 10 + name[i] - '0';
    }
    return n;
}
inline bool CollectNationMatrix(const std::vector<NationImage> &pictures, std::vector<MobileWidget> &out,
                                std::string *reason = nullptr)
{
    const auto reject = [&](const std::string &message) {
        if (reason)
            *reason = "matrix:" + message;
        return false;
    };
    std::vector<NationImage> axes, relations;
    for (const auto &p : pictures)
    {
        if (NationPictureNumber(p.node) < 1)
            continue;
        if (p.image.source.group == "NATIONS")
            axes.push_back(p);
        else if (p.image.source.group == "relations")
            relations.push_back(p);
        else
            return reject("unexpected-group=" + p.image.source.group + " node=" + p.node);
    }
    std::sort(axes.begin(), axes.end(),
              [](const auto &a, const auto &b) { return NationPictureNumber(a.node) < NationPictureNumber(b.node); });
    if (axes.size() < 4 || axes.size() % 2 || axes.size() > 32)
        return reject("axis-image-count=" + std::to_string(axes.size()));
    const size_t count = axes.size() / 2;
    // Beta adds main-character portrait axes and relations. Do not discard those.
    if (relations.size() != count * (count - 1))
        return reject("pair-count=" + std::to_string(relations.size()) + " expected=" +
                      std::to_string(count * (count - 1)) + " (additional beta content must be retained)");
    std::set<std::string> columns, rows;
    for (size_t i = 0; i < count; ++i)
    {
        if (NationPictureNumber(axes[i].node) != int(i + 1) ||
            NationPictureNumber(axes[count + i].node) != int(count + i + 1) || axes[i].caption.empty() ||
            axes[count + i].caption.empty() || axes[i].y != axes[0].y || axes[count + i].x != axes[count].x ||
            !columns.insert(axes[i].image.source.image).second ||
            !rows.insert(axes[count + i].image.source.image).second)
            return reject("axis-schema-or-caption index=" + std::to_string(i) + " column=" + axes[i].node +
                          " row=" + axes[count + i].node);
    }
    if (columns != rows)
        return reject("row-column-identities-differ");
    std::vector<MobileWidget> result;
    MobileWidget header{"ANDROID_NATION_COLUMNS", "nation_columns", "", "1", -1, 0, false};
    header.headerCells.emplace();
    for (size_t i = 0; i < count; ++i)
    {
        MobileCell c;
        c.column = int(i);
        c.text = axes[i].caption;
        c.images.push_back(axes[i].image);
        header.headerCells->push_back(std::move(c));
    }
    result.push_back(std::move(header));
    std::set<std::string> used;
    for (size_t row = 0; row < count; ++row)
    {
        const auto &axis = axes[count + row];
        MobileWidget w{"ANDROID_NATION_ROW", "nation_row", axis.caption, "", int(row), 0, false};
        w.rowId = axis.image.source.image;
        w.images.push_back(axis.image);
        for (size_t col = 0; col < count; ++col)
        {
            MobileCell c;
            c.column = int(col);
            if (axis.image.source.image == axes[col].image.source.image)
                c.text = "self";
            else
            {
                const NationImage *found = nullptr;
                for (const auto &p : relations)
                    if (p.x == axes[col].x && p.y == axis.y)
                    {
                        if (found)
                            return reject("duplicate-pair from=" + axis.image.source.image +
                                          " to=" + axes[col].image.source.image);
                        found = &p;
                    }
                if (!found || !used.insert(found->node).second)
                    return reject("missing-or-reused-pair from=" + axis.image.source.image +
                                  " to=" + axes[col].image.source.image);
                c.text = found->caption.empty() ? found->image.source.image : found->caption;
                c.images.push_back(found->image);
            }
            w.cells.push_back(std::move(c));
        }
        result.push_back(std::move(w));
    }
    out.insert(out.end(), result.begin(), result.end());
    return true;
}

// The nation relations screen's widgets (XINTERFACE `xi`, the screen's opening `opening`): the relation matrix of the
// script's dynamic images, the reputations, the flag candidate, the right-click help, and the actions the screen's
// buttons take now, "nation@<opening>:<generation>:press|help|close" (and ":pick" on RIGHTCHANGE_NATION). Any change of
// the widgets or a new opening starts a new generation, which revokes the actions handed out before.
template <class Interface> void CollectNations(Interface &xi, uint64_t opening, std::vector<MobileWidget> &widgets)
{
    std::vector<NationImage> pictures;
    const auto caption = [&](const char *key) -> std::string {
        if (!key)
            return {};
        const char *text = xi.pStringService ? xi.pStringService->GetString(key) : nullptr;
        return text ? text : "";
    };
    for (auto *p = xi.m_imgLists; p; p = p->next)
    {
        if (!p->sImageName || !p->sImageListName || !p->sPicture)
            continue;
        auto image = AtlasReference(xi.pPictureService, p->sImageListName, p->sPicture);
        // Source identity exists independently of asynchronous bitmap decoding.
        image.source = {"atlas", p->sImageListName, p->sPicture, {}, {}, -1};
        pictures.push_back({p->sImageName, caption(p->sPicture), p->position.left, p->position.top, image});
    }
    std::string failure;
    const bool matrix = CollectNationMatrix(pictures, widgets, &failure);
    auto *strings = core.Entity_GetAttributeClass(g_idInterface, "strings");
    const auto text = [&](const char *key) -> std::string {
        const char *s = strings ? strings->GetAttribute(key) : nullptr;
        return s ? s : "";
    };
    static constexpr struct
    {
        const char *image, *caption, *value;
    } rep[] = {{"Eng", "EngH", "ENG_TEXT"},
               {"Fra", "FraH", "FRA_TEXT"},
               {"Spa", "SpaH", "SPA_TEXT"},
               {"Hol", "HolH", "HOL_TEXT"}};
    bool complete = matrix;
    for (const auto &r : rep)
    {
        const auto p = std::find_if(pictures.begin(), pictures.end(),
                                    [&](const auto &v) { return storm::iEquals(v.node, r.image); });
        const auto w = std::find_if(widgets.begin(), widgets.end(),
                                    [&](const auto &v) { return storm::iEquals(v.node, r.value) && v.type == "text"; });
        if (p == pictures.end() || w == widgets.end() || text(r.caption).empty())
        {
            complete = false;
            failure += " reputation:" + std::string(r.image) + (p == pictures.end() ? ":image-missing" : "") +
                       (w == widgets.end() ? ":text-missing" : "") +
                       (text(r.caption).empty() ? ":caption-missing" : "");
            continue;
        }
        MobileWidget row{"ANDROID_NATION_REPUTATION", "nation_reputation", text(r.caption), w->label, -1, 0, false};
        row.rowId = p->image.source.image;
        row.images.push_back(p->image);
        if (const auto standing = xi.MobileNationStanding(r.image))
        {
            row.context["score"] = standing->score;
            row.context["tone"] = standing->tone;
        }
        widgets.push_back(std::move(row));
    }
    bool candidateSeen = false;
    for (auto &w : widgets)
        if (storm::iEquals(w.node, "FlagPic"))
        {
            candidateSeen = true;
            if (w.images.size() != 1)
            {
                complete = false;
                failure += " candidate:image-count=" + std::to_string(w.images.size());
                continue;
            }
            w.label = caption(w.images.front().source.image.c_str());
            if (w.label.empty())
            {
                complete = false;
                failure += " candidate:caption-missing source=" + w.images.front().source.image;
            }
        }
    complete = complete && candidateSeen;
    if (!candidateSeen)
        failure += " candidate:widget-missing";
    // Export the real right-click commands; the script chooses Nation_hint or Hunter_hint.
    for (const char *name : {"FlagPic", "INFO_CLICK"})
    {
        auto *node = xi.FindNode(name, nullptr);
        if (!node || !node->m_bUse || !node->CheckCommandUsed(ACTION_MOUSERCLICK))
        {
            complete = false;
            failure += " help:" + std::string(name) + ":rclick-unavailable";
            continue;
        }
        MobileWidget help{name, "nation_help", name, "", -2, 0, node->m_bClickable};
        help.actions.push_back("help");
        widgets.push_back(std::move(help));
    }
    if (auto *fader = xi.FindNode("tooltip_fader", nullptr); fader && fader->m_bUse)
    {
        MobileWidget close{"tooltip_fader", "nation_help_close", "", "", -3, 0, !fader->m_bLockedNode};
        widgets.push_back(std::move(close));
    }
    MobileWidget marker{
        "ANDROID_NATION_RELATIONS", "nation_relations", text("titul"), complete ? "1" : "0", -1, 0, false};
    if (!complete)
        marker.unavailableReason = failure;
    if (!complete)
        for (size_t i = 0; i < pictures.size() && i < 64; ++i)
        {
            const auto &p = pictures[i];
            MobileWidget source{"ANDROID_NATION_SOURCE",
                                "nation_source",
                                p.caption,
                                "x=" + std::to_string(p.x) + " y=" + std::to_string(p.y),
                                int(i),
                                0,
                                false};
            source.rowId = p.node;
            source.images.push_back(p.image);
            widgets.push_back(std::move(source));
        }
    widgets.push_back(std::move(marker));
    // A change of the widgets revokes the actions handed out before; a new opening does too, by its stamp.
    if (xi.m_mobileNationSignature != widgets)
    {
        xi.m_mobileNationSignature = widgets;
        ++xi.m_mobileNationGeneration;
    }
    for (auto &w : widgets)
    {
        auto *node = xi.FindNode(w.node.c_str(), nullptr);
        if (!node || !node->m_bUse)
            continue;
        if (w.type == "button" || w.type == "nation_help" || w.type == "nation_help_close")
        {
            w.enabled = w.enabled && (w.type == "nation_help_close" || node->m_bSelected) && !node->m_bLockedNode &&
                        !node->m_bLockStatus && node->m_nDoDelay == 0;
            w.actions.clear();
            const std::string stamp =
                "nation@" + std::to_string(opening) + ":" + std::to_string(xi.m_mobileNationGeneration) + ":";
            if (w.enabled)
                w.actions.push_back(stamp + (w.type == "nation_help"         ? "help"
                                             : w.type == "nation_help_close" ? "close"
                                                                             : "press"));
            // One player action picks a flag directly: "<stamp>pick=<NATIONS picture>" (see ApplyNationAction).
            if (w.enabled && storm::iEquals(w.node, "RIGHTCHANGE_NATION"))
                w.actions.push_back(stamp + "pick");
        }
    }
}

// A nation screen action ("nation@…", handed out by CollectNations) on `node`: run only while the interface's widgets
// still grant it; it is used up before any script callback runs.
template <class Interface, class Node, class Action>
ActionOutcome ApplyNationAction(Interface &xi, Node *node, const Action &action)
{
    const auto refused = [](const char *reason) { return ActionOutcome{ActionOutcome::Refused, reason}; };
    const ActionOutcome applied{};
    // "…:pick=<picture>" carries the flag the player tapped; the capability is "…:pick".
    const auto pick = action.value.find(":pick=");
    const std::string token = pick == std::string::npos ? action.value : action.value.substr(0, pick + 5);
    const std::string target = pick == std::string::npos ? std::string() : action.value.substr(pick + 6);
    std::vector<MobileWidget> live;
    xi.MobileCollectWidgets(live);
    const auto granted = std::find_if(live.begin(), live.end(), [&](const auto &w) {
        return w.node == action.node && w.index == action.index && w.enabled &&
               std::find(w.actions.begin(), w.actions.end(), token) != w.actions.end();
    });
    // A granted action's node was idle and selected when the widgets above were collected (CollectNations).
    if (granted == live.end())
        return refused("the nation offer is not current");
    ++xi.m_mobileNationGeneration; // Consume this frame capability before callbacks or delayed native events.
    if (pick != std::string::npos)
    {
        // The script cycles the candidate one nation per press over the flags it allows (NationRelation.c
        // SetNewNation): press › until the candidate is the tapped flag, within one frame; back at the start the
        // game does not offer that flag, and the candidate is the one it was.
        const auto candidate = [&xi] {
            std::vector<MobileWidget> now;
            xi.MobileCollectWidgets(now);
            for (const auto &w : now)
                if (storm::iEquals(w.node, "FlagPic") && w.type == "image" && w.images.size() == 1)
                    return w.images.front().source.image;
            return std::string();
        };
        const int code = node->CheckCommandUsed(ACTION_MOUSECLICK) ? ACTION_MOUSECLICK : ACTION_ACTIVATE;
        const std::string start = candidate();
        for (int step = 0; step < 8 && !target.empty() && !storm::iEquals(candidate(), target) && node->m_bUse &&
                           node->CheckCommandUsed(code) && (step == 0 || !storm::iEquals(candidate(), start));
             ++step)
        {
            xi.SetCurNode(node);
            bool stop = false;
            node->DoAction(code, stop, step == 0);
        }
        if (!storm::iEquals(candidate(), target))
        {
            xi.MobileNationPickFailed(target);
            return refused("the flag was not reached");
        }
        return applied;
    }
    if (granted->type == "nation_help_close")
    {
        core.Event("MouseRClickUP");
        return applied;
    }
    const int code = granted->type == "nation_help"              ? ACTION_MOUSERCLICK
                     : node->CheckCommandUsed(ACTION_MOUSECLICK) ? ACTION_MOUSECLICK
                                                                 : ACTION_ACTIVATE;
    if (!node->CheckCommandUsed(code))
        return refused("no command");
    xi.SetCurNode(node);
    bool stop = false;
    if (auto *next = node->DoAction(code, stop, true))
        xi.SetCurNode(next);
    return applied;
}
} // namespace storm::mobile
