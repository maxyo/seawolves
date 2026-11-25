#pragma once
// The file names the archive reader (android/archive) opens as one archive or installer: multi-volume archive sets
// and Inno Setup volumes. The torrent library (android/torrent) groups a torrent's files by the same rules, so it
// offers a set exactly when the reader can open it. Names compare ASCII case-insensitively, as Android storage may.
//
// Multi-volume sets of one directory:
// - split: name.001, name.002, ... (7-Zip, WinRAR and file splitters cut one archive into numbered parts of at
//   least three digits, from 001; 7-Zip goes on with .1000). The reader reads the parts as one stream, so any
//   archive split this way opens.
// - RAR parts: name.part1.rar, name.part2.rar, ... (RAR 3 and later; every part of a set has the same number of
//   digits, from 1).
// - old RAR: name.rar, name.r00, name.r01, ... name.r99 (RAR 2 naming; WinRAR's later .s00 parts are not read).
// - split ZIP: name.z01, name.z02, ..., name.zip (PKZIP spanning): libarchive cannot read it, so the reader reports
//   it and the torrent library does not offer it.
// Inno Setup keeps the payload of an installer setup.exe in setup-1.bin, setup-2.bin, ... or, with several slices per
// disk, setup-1a.bin, setup-1b.bin, ... next to it (innoextract's slice_reader::slice_filename).
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace storm_archive::volume_names
{
inline std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

enum class Kind
{
    Split,
    RarParts,
    OldRar,
    SplitZip
};
// One multi-volume set: its present volumes in order (a split ZIP's .zip first: the name it is known by) and the names
// of the volumes it lacks up to the last present one; next names the volume after the last present one (a set that
// ends early may lack it).
struct Set
{
    Kind kind;
    std::vector<std::string> volumes, missing;
    std::string next;
};

namespace detail
{
inline bool digits(std::string_view s)
{
    return !s.empty() && s.size() <= 9 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
inline bool ends_with(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}
inline std::string padded(uint32_t number, size_t width)
{
    auto text = std::to_string(number);
    return std::string(width > text.size() ? width - text.size() : 0, '0') + text;
}
// A name's place in a set: kind, the set's stem (as spelled), number and width of the number; false: no volume.
struct Volume
{
    Kind kind;
    std::string stem;
    uint32_t number;
    size_t width;
};
inline bool volume(const std::string &name, Volume &out)
{
    const auto l = lower(name);
    const auto dot = l.rfind('.');
    if (dot == l.npos || dot == 0)
        return false;
    const auto ext = std::string_view(l).substr(dot + 1);
    if (ext.size() >= 3 && digits(ext))
    {
        out = {Kind::Split, name.substr(0, dot), static_cast<uint32_t>(std::stoul(std::string(ext))), 3};
        return out.number >= 1;
    }
    if (ext.size() == 3 && ext[0] == 'r' && digits(ext.substr(1)))
    {
        // name.r00 is the second volume: name.rar is the first.
        out = {Kind::OldRar, name.substr(0, dot), static_cast<uint32_t>(std::stoul(std::string(ext.substr(1)))) + 1, 2};
        return true;
    }
    if (ext.size() == 3 && ext[0] == 'z' && digits(ext.substr(1)))
    {
        out = {Kind::SplitZip, name.substr(0, dot), static_cast<uint32_t>(std::stoul(std::string(ext.substr(1)))), 2};
        return true;
    }
    if (ext == "rar")
    {
        const auto part = l.rfind(".part", dot);
        if (part != l.npos && part + 5 < dot && digits(std::string_view(l).substr(part + 5, dot - part - 5)))
        {
            out = {Kind::RarParts, name.substr(0, part),
                   static_cast<uint32_t>(std::stoul(l.substr(part + 5, dot - part - 5))), dot - part - 5};
            return out.number >= 1;
        }
    }
    return false;
}
inline std::string name_of(Kind kind, const std::string &stem, uint32_t number, size_t width)
{
    switch (kind)
    {
    case Kind::Split:
        return stem + "." + padded(number, width);
    case Kind::RarParts:
        return stem + ".part" + padded(number, width) + ".rar";
    case Kind::OldRar:
        return number == 0 ? stem + ".rar" : stem + ".r" + padded(number - 1, 2);
    case Kind::SplitZip:
        return stem + ".z" + padded(number, 2);
    }
    return {};
}
} // namespace detail

// The multi-volume sets among the names of one directory, ordered by their first present volume. A name that is no
// volume of a set (a single archive, any other file) is in none.
inline std::vector<Set> sets(const std::vector<std::string> &names)
{
    // Keyed by kind, lower-cased stem and (for RAR parts) the number's width.
    std::map<std::tuple<Kind, std::string, size_t>, std::map<uint32_t, std::string>> found;
    std::map<std::string, std::string> by_lower;
    for (const auto &name : names)
        by_lower.emplace(lower(name), name);
    for (const auto &name : names)
    {
        detail::Volume v;
        if (detail::volume(name, v))
            found[{v.kind, lower(v.stem), v.kind == Kind::RarParts ? v.width : 0}].emplace(v.number, name);
    }
    // The first volume of an old RAR set and the last of a split ZIP carry the plain extension.
    for (auto &[key, volumes] : found)
    {
        const auto &[kind, stem, width] = key;
        const auto plain = by_lower.find(stem + (kind == Kind::OldRar ? ".rar" : ".zip"));
        if (plain == by_lower.end())
            continue;
        if (kind == Kind::OldRar)
            volumes.emplace(0, plain->second);
        else if (kind == Kind::SplitZip)
            volumes.emplace(UINT32_MAX, plain->second);
    }
    std::vector<Set> out;
    for (const auto &[key, volumes] : found)
    {
        const auto kind = std::get<0>(key);
        Set set{kind, {}, {}, {}};
        const auto &spelled = volumes.begin()->second;
        detail::Volume first;
        std::string stem;
        size_t width = std::get<2>(key);
        if (detail::volume(spelled, first))
            stem = first.stem;
        else
            stem = spelled.substr(0, spelled.rfind('.')); // name.rar of an old RAR set, name.zip of a split ZIP
        if (kind == Kind::Split)
            width = 3;
        const uint32_t from = kind == Kind::OldRar ? 0 : 1;
        uint32_t last = from;
        for (const auto &[number, name] : volumes)
            if (number != UINT32_MAX)
                last = std::max(last, number);
        for (uint32_t n = from; n <= last; ++n)
        {
            const auto present = volumes.find(n);
            if (present != volumes.end())
                set.volumes.push_back(present->second);
            else if (kind != Kind::SplitZip)
                set.missing.push_back(detail::name_of(kind, stem, n, width));
        }
        if (kind == Kind::SplitZip && volumes.count(UINT32_MAX))
            set.volumes.insert(set.volumes.begin(), volumes.at(UINT32_MAX));
        set.next = detail::name_of(kind, stem, last + 1, width);
        out.push_back(std::move(set));
    }
    std::sort(out.begin(), out.end(), [](const Set &a, const Set &b) { return a.volumes.front() < b.volumes.front(); });
    return out;
}

// The stem of the installer an Inno Setup volume belongs to: "setup" for setup-1.bin or setup-12b.bin (the installer
// is setup.exe); empty for any other name.
inline std::string inno_installer_stem(const std::string &name)
{
    const auto l = lower(name);
    if (!detail::ends_with(l, ".bin"))
        return {};
    auto end = l.size() - 4;
    if (end && l[end - 1] >= 'a' && l[end - 1] <= 'z')
        --end;
    const auto dash = l.rfind('-', end);
    if (dash == l.npos || dash == 0 || !detail::digits(std::string_view(l).substr(dash + 1, end - dash - 1)))
        return {};
    return name.substr(0, dash);
}

// A single archive the reader opens, by its extension (it reads the content by signature).
inline bool archive_extension(const std::string &name)
{
    const auto l = lower(name);
    for (const char *ext :
         {".zip", ".7z", ".rar", ".tar", ".gz", ".tgz", ".bz2", ".tbz", ".xz", ".txz", ".iso", ".cab"})
        if (detail::ends_with(l, ext))
            return true;
    return false;
}
} // namespace storm_archive::volume_names
