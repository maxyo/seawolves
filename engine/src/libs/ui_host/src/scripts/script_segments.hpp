// The touch interface's script segments: the list the pack gives (scripts.json), what the game's program offers
// (GameSurface), and attaching the segments to every program the game runs. See ../../README.md.
#pragma once

#include "script_program.hpp"
#include "storm_ui/script_screens.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace storm::ui_scripts
{
// Something a segment needs from the game: "fn:Name/arguments", "var:Name", "ev:Name", "interface:id", "file:path",
// "node:interface#NODE" (the interface's ini has a section for the node).
struct Requirement
{
    enum class Kind
    {
        Function,
        Global,
        Event,
        Interface,
        File,
        Node
    };
    Kind kind = Kind::Function;
    std::string name; // of a Node: the interface
    std::string node;
    uint32_t arguments = 0;
    std::string text; // as the list writes it, for log lines
};

struct Segment
{
    std::string file; // below the program directory
    // Without it the touch interface cannot work: a failure stops the start. Any other segment that fails is left
    // out with its screens, loudly.
    bool core = false;
    std::vector<Requirement> needs;
    // Contracts ("adapter:<family>"): of the segments providing one, the first attached in list order is chosen and
    // the later ones are not attached; a segment using one is attached only once it is provided.
    std::vector<std::string> provides, uses;
    // Contracts a feature of the segment's screens needs and the screen does without: each binding's data gets
    // optional.<family> "1" when one is provided, "0" (and a log line) when not; the segment attaches either way.
    std::vector<std::string> optional;
    // An adapter's side of a screen: the interface, the nodes and the data of capability's screen, joined with the
    // screen entry (no component) of a segment attached after it. A binding whose interface or required nodes the
    // game lacks is off alone.
    std::vector<storm::ui::ScriptScreen> binds;
    // A screen with no interface takes it and its nodes from the attached binding of its capability.
    std::vector<storm::ui::ScriptScreen> screens;
};

// The pack's segment list, checked once; a defect throws naming the entry.
std::vector<Segment> ParseSegments(std::string_view json);

// What the running program offers, read once per program: its functions (with their declared arguments), globals and
// event handlers, the interfaces of its Interfaces[] table (ids: the ini file names in lower case), and the files of
// the game.
struct GameSurface
{
    std::unordered_map<std::string, uint32_t> functions; // keys in lower case, as the VM's names compare
    std::unordered_set<std::string> globals, events;
    std::unordered_map<std::string, std::string> interfaces; // id -> the ini file of the Interfaces[] row
    std::function<bool(const std::string &path)> file_exists;
};
GameSurface Probe(const storm::script::Program &program, std::function<bool(const std::string &)> file_exists);
// Why the game does not meet `requirement`; empty when it does.
std::string Unmet(const GameSurface &surface, const Requirement &requirement);

// Attaches the listed segments to every program (core's storm::script::Attacher), each on its own and in order.
class Attacher final : public storm::script::Attacher
{
  public:
    struct Hooks
    {
        std::function<std::string()> read_list;                                     // scripts.json of the pack
        std::function<bool(const std::string &)> file_exists;                       // a game file (file: requirements)
        std::function<void(const std::vector<storm::ui::ScriptScreen> &)> attached; // the screens of this program
        std::function<void()> released;                    // the program and its screens are gone
        std::function<void(const std::string &)> unloaded; // a script unloaded a segment of the game's
    };
    explicit Attacher(Hooks hooks) : hooks_(std::move(hooks))
    {
    }

    void Attach(storm::script::Program &program) override;
    void Released() override;
    void Unloaded(const std::string &segment) override
    {
        hooks_.unloaded(segment);
    }
    // The program the segments are attached to, nullptr between programs.
    storm::script::Program *Running() const
    {
        return program_;
    }

  private:
    Hooks hooks_;
    std::vector<Segment> segments_; // read at the first Attach
    bool listed_ = false;
    storm::script::Program *program_ = nullptr;
};
} // namespace storm::ui_scripts
