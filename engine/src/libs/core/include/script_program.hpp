#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace storm::script
{
// A function the running program can call: one of its scripts' or a native.
struct Function
{
    std::string_view name;
    uint32_t arguments = 0;
};

// What became of a segment the engine attached: anything but Yes leaves nothing of it loaded.
enum class Attached
{
    Yes,
    NoFile,      // its source file is not there (and no cache holds it)
    NotCompiled, // it did not compile: the compile log says why
};

// The running script program as engine code that attaches script segments of its own to it sees it.
class Program
{
  public:
    // Compiles `segment` (a path below the program directory) as an attached segment: its globals never enter a save.
    virtual Attached Attach(const char *segment) = 0;
    // A script is running now: engine code called from inside it must not raise script events.
    virtual bool Executing() const = 0;
    // The file of the segment whose function runs now ("" outside scripts): engine code a script calls learns which
    // segment called it.
    virtual std::string RunningSegment() const = 0;
    // From a native the running script called: every script frame of the call returns at once, no further statement
    // runs.
    virtual void Abort() = 0;
    virtual void ForEachFunction(const std::function<void(const Function &)> &visit) const = 0;
    virtual void ForEachGlobal(const std::function<void(std::string_view name)> &visit) const = 0;
    virtual void ForEachEvent(const std::function<void(std::string_view name)> &visit) const = 0;

  protected:
    ~Program() = default;
};

// Engine code that attaches segments of its own to every program the game runs (the touch interface's scripts).
class Attacher
{
  public:
    virtual ~Attacher() = default;
    // After the program's Main or OnLoad returned, outside any script. A throw stops the start (a fatal error).
    virtual void Attach(Program &program) = 0;
    // The program and its segments are gone (a save is loaded, the game ends): `program` must not be used again.
    virtual void Released() = 0;
    // A script unloaded `segment` (UnloadSegment, inside that script): its functions, globals and events are gone.
    virtual void Unloaded(const std::string &segment) = 0;
};

// Set before the game's program starts, by the module that attaches; nullptr removes it.
void SetAttacher(Attacher *attacher);
} // namespace storm::script
