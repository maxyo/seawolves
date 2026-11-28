#pragma once
// The game's picture lists, the groups of named pictures its interfaces draw: every ini of the folder
// resource\ini\interfaces\pictures where a game splits them, else resource\ini\interfaces\pictures.ini. The engine's
// interfaces (xinterface's picture service) and the touch interface's game pictures read the same lists.

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace storm
{
inline constexpr char kPictureListFolder[] = "resource\\ini\\interfaces\\pictures";
inline constexpr char kPictureListFile[] = "resource\\ini\\interfaces\\pictures.ini";

// The lists to read, as paths under the game's root, in their order: the folder's ini files (`folderInis`, their
// names) by name, else the one file when the game has it; empty when it has neither.
inline std::vector<std::string> PictureListPaths(std::vector<std::string> folderInis, bool hasFile)
{
    std::ranges::sort(folderInis);
    std::vector<std::string> paths;
    for (const std::string &name : folderInis)
        paths.push_back(std::string(kPictureListFolder) + "\\" + name);
    if (paths.empty() && hasFile)
        paths.emplace_back(kPictureListFile);
    return paths;
}
// The game's portrait `face` as the touch interface's game pictures name it: the picture "face" of its lists' group
// FACE128_<face>, wherever the game keeps the file.
inline std::string PortraitPicture(std::string_view face)
{
    return "atlas:FACE128_" + std::string(face) + "/face";
}
} // namespace storm
