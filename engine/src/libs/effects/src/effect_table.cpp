#include "effect_table.hpp"

#include "istring.hpp"

#include <nlohmann/json.hpp>

namespace storm::effects
{
namespace
{
bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && AsciiLower(std::string(text.substr(text.size() - suffix.size()))) == suffix;
}
} // namespace

const EffectEntry *EffectTable::Find(std::string_view id) const
{
    const auto it = entries.find(AsciiLower(std::string(id)));
    return it == entries.end() ? nullptr : &it->second;
}

EffectTable ParseEffectTable(std::string_view json)
{
    EffectTable table;
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (!root.is_object())
    {
        table.errors.push_back("not a JSON object");
        return table;
    }
    for (const auto &[id, value] : root.items())
    {
        if (value.is_string() && value.get_ref<const std::string &>().empty())
            continue;
        const auto file = value.is_object() ? value.find("file") : value.end();
        const auto speed = value.is_object() ? value.find("speed") : value.end();
        if (!value.is_object() || value.size() != 2 || file == value.end() || !file->is_string() ||
            speed == value.end() || !speed->is_number())
        {
            table.errors.push_back("'" + id + "' is not {\"file\": name, \"speed\": number}");
            continue;
        }
        EffectEntry entry;
        entry.file = file->get<std::string>();
        entry.speed = speed->get<float>();
        if (ends_with(entry.file, ".efkefc"))
            entry.kind = EffectEntry::Kind::Effekseer;
        else if (ends_with(entry.file, ".xps"))
            entry.kind = EffectEntry::Kind::ParticleSystem;
        else
        {
            table.errors.push_back("'" + id + "' names " + entry.file + ", neither .efkefc nor .xps");
            continue;
        }
        if (!table.entries.emplace(AsciiLower(id), std::move(entry)).second)
            table.errors.push_back("'" + id + "' repeats an id that differs only in case; the first one is used");
    }
    return table;
}

} // namespace storm::effects
