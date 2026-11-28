// Loading script globals the program cannot take back as a whole, with the production reader (save_state.hpp).
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include "data.h"
#include "platform/platform.hpp"
#include "s_vartab.h"
#include "save_references.hpp"
#include "save_state.hpp"

namespace
{
void require(bool condition, const std::string &why)
{
    if (!condition)
        throw std::runtime_error(why);
}

class Codec final : public VSTRING_CODEC
{
    std::vector<std::string> names;

  public:
    uint32_t GetNum() override
    {
        return names.size();
    }
    uint32_t Convert(const char *name) override
    {
        if (!name)
            return 0xffffffff;
        auto found = std::find(names.begin(), names.end(), name);
        if (found != names.end())
            return found - names.begin();
        names.emplace_back(name);
        return names.size() - 1;
    }
    uint32_t Convert(const char *name, int32_t length) override
    {
        const std::string value(name, length);
        return Convert(value.c_str());
    }
    const char *Convert(uint32_t code) override
    {
        return names.at(code).c_str();
    }
    std::optional<uint32_t> Find(std::string_view name) const override
    {
        const auto found = std::find(names.begin(), names.end(), name);
        return found == names.end() ? std::nullopt : std::optional<uint32_t>(found - names.begin());
    }
    void VariableChanged() override
    {
    }
    // The names in code order, as STRING_CODEC walks its own for a save.
    const char *Get()
    {
        next = 0;
        return GetNext();
    }
    const char *GetNext()
    {
        return next < names.size() ? names[next++].c_str() : nullptr;
    }

  private:
    size_t next = 0;
};
} // namespace

// A bounded compiler fixture: the variable table, codec and errors; the state is written and read with
// save_state.hpp, as COMPILER::SerializeState and LoadState do.
class COMPILER final : public VIRTUAL_COMPILER
{
  public:
    Codec SCodec;
    VarTable VarTab;
    std::deque<DATA> newArrays; // COMPILER::newArrays_: the arrays of `new type[n]`
    std::vector<std::string> errors;
    COMPILER()
    {
        VarTab.SetVCompiler(this);
        SCodec.Convert("code-zero");
    }
    VSTRING_CODEC *GetVSC() override
    {
        return &SCodec;
    }
    void SetError(const char *format, ...) override
    {
        char text[512];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        errors.emplace_back(text);
    }

    DATA &add(const char *name, S_TOKEN_TYPE type, uint32_t count = 1)
    {
        VarInfo info;
        info.name = name;
        info.type = type;
        info.elements = count;
        info.segment_id = 0;
        return *VarTab.GetVarX(VarTab.AddVar(info))->value;
    }
    // COMPILER::SerializeState: the head, then the variables.
    std::vector<char> save()
    {
        const storm::save::ReferenceIndex references(VarTab, {}, &newArrays);
        storm::save::StateWriter out(*this);
        storm::save::writeHead(out, "PROGRAM\\", SCodec, "seadogs.c",
                               static_cast<uint32_t>(VarTab.GetVarNum() + references.NewArrays().size()));
        for (size_t n = 0; n < VarTab.GetVarNum(); ++n)
        {
            const VarInfo *variable = VarTab.GetVar(n);
            out.string(variable->name.c_str());
            out.variable(variable->value.get(), variable->name.c_str(), references);
        }
        storm::save::writeNewArrays(out, references);
        out.attributeReferences();
        return out.take();
    }
    // CoreImpl::ProcessStateLoading and COMPILER::LoadState: the whole state parses before
    // anything is touched, then the variables are applied.
    void load(const std::vector<char> &bytes)
    {
        storm::save::ParsedState state;
        std::string error;
        const bool parsed = storm::save::parseState(bytes.data(), bytes.size(), state, error);
        require(parsed, "the state does not parse: " + error);
        require(state.segments.size() == 1 && state.segments[0] == "seadogs.c", "the segment list was misread");
        for (const auto &name : state.codec)
            SCodec.Convert(name.c_str());
        const auto report = [this](const std::string &line) { errors.push_back(line); };
        const std::vector<DATA *> loaded =
            storm::save::applyVariables(bytes.data(), bytes.size(), state.variables, VarTab, &SCodec, report, [this] {
                DATA &array = newArrays.emplace_back();
                array.SetVCompiler(this);
                return &array;
            });
        storm::save::applyAttributeReferences(state.attributeReferences, VarTab, loaded, report);
    }
};

namespace
{
int32_t integer(DATA &value)
{
    int32_t result = 0;
    require(value.GetType() == VAR_INTEGER && value.Get(result), "not an int");
    return result;
}
std::string text(DATA &value)
{
    const char *result = nullptr;
    require(value.GetType() == VAR_STRING && value.Get(result) && result, "not a string");
    return result;
}
float real(DATA &value)
{
    float result = 0;
    require(value.GetType() == VAR_FLOAT && value.Get(result), "not a float");
    return result;
}
bool reported(const COMPILER &compiler, const char *part)
{
    return std::any_of(compiler.errors.begin(), compiler.errors.end(),
                       [&](const std::string &line) { return line.find(part) != std::string::npos; });
}

// `iMoney += 0.5` on a global int (DATA::Plus converts the variable), `sName += ...` on an int:
// the save writes the run-time type. The variables after them must load, and the drifted
// ones come back as they were saved.
void typeDrift()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &money = game.add("iMoney", VAR_INTEGER);
        DATA &label = game.add("iLabel", VAR_INTEGER);
        DATA &name = game.add("sName", VAR_STRING);
        DATA &day = game.add("iDay", VAR_INTEGER);
        money.Set(static_cast<int32_t>(100));
        DATA half;
        half.SetVCompiler(&game);
        half.Set(0.5f);
        money.Plus(&half);
        require(money.GetType() == VAR_FLOAT, "fixture: += float no longer changes the variable's type");
        label.Set("north");
        name.Set("Charles");
        day.Set(static_cast<int32_t>(7));
        image = game.save();
    }
    COMPILER loaded;
    DATA &money = loaded.add("iMoney", VAR_INTEGER);
    DATA &label = loaded.add("iLabel", VAR_INTEGER);
    DATA &name = loaded.add("sName", VAR_STRING);
    DATA &day = loaded.add("iDay", VAR_INTEGER);
    loaded.load(image);
    require(loaded.errors.empty(),
            "a drifted scalar was reported as lost: " + (loaded.errors.empty() ? "" : loaded.errors[0]));
    require(real(money) == 100.5f && text(label) == "north", "a drifted scalar was not restored as saved");
    require(text(name) == "Charles" && integer(day) == 7, "the variables after a drifted one were misread");
}

// A global ref declared before the array it points into, the array resized at run time
// (SetArraySize) beyond its declared size: the ref must be bound to the resized array.
void referenceBeforeResize()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &current = game.add("rCurrent", VAR_REFERENCE);
        DATA &attribute = game.add("arCurrent", VAR_AREFERENCE);
        DATA &table = game.add("Table", VAR_INTEGER, 4);
        DATA &objects = game.add("Objects", VAR_OBJECT, 4);
        table.SetElementsNum(8);
        game.VarTab.SetElementsNum(game.VarTab.FindVar("Table"), 8);
        objects.SetElementsNum(8);
        game.VarTab.SetElementsNum(game.VarTab.FindVar("Objects"), 8);
        table.GetArrayElement(6)->Set(static_cast<int32_t>(42));
        current.SetReference(table.GetArrayElement(6));
        ATTRIBUTES *root = objects.GetArrayElement(6)->GetAClass();
        root->CreateSubAClass(root, "ship.name")->SetValue("Black Pearl");
        attribute.SetAReference(root->FindAClass(root, "ship.name"));
        image = game.save();
    }
    COMPILER loaded;
    DATA &current = loaded.add("rCurrent", VAR_REFERENCE);
    DATA &attribute = loaded.add("arCurrent", VAR_AREFERENCE);
    DATA &table = loaded.add("Table", VAR_INTEGER, 4);
    DATA &objects = loaded.add("Objects", VAR_OBJECT, 4);
    loaded.load(image);
    require(loaded.errors.empty(),
            "the resized arrays were reported: " + (loaded.errors.empty() ? "" : loaded.errors[0]));
    require(table.GetElementsNum() == 8 && objects.GetElementsNum() == 8, "the arrays did not take their saved size");
    require(current.GetReference() == table.GetArrayElement(6),
            "the ref does not name the element of the resized array");
    int32_t value = 0;
    require(current.Get(value) && value == 42, "the ref reads a wrong value");
    ATTRIBUTES *root = objects.GetArrayElement(6)->GetAClass();
    require(attribute.GetAClass() == root->FindAClass(root, "ship.name") &&
                attribute.GetAClass()->GetValue() == "Black Pearl",
            "the aref does not name the attribute of the resized array's element");
}

// Saved variables the program no longer declares, of every type including arrays of refs,
// arefs and pointers (a dynamically loaded segment's globals), and variables the program
// declares differently: each is reported and left, every other variable loads.
void variablesTheProgramCannotTake()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &first = game.add("iFirst", VAR_INTEGER);
        DATA &target = game.add("Target", VAR_OBJECT);
        DATA &refs = game.add("goneRefs", VAR_REFERENCE, 3);
        DATA &arefs = game.add("goneArefs", VAR_AREFERENCE, 2);
        DATA &pointers = game.add("gonePointers", VAR_PTR, 2);
        DATA &strings = game.add("goneStrings", VAR_STRING, 2);
        DATA &object = game.add("nowInt", VAR_OBJECT);
        DATA &array = game.add("nowSingle", VAR_INTEGER, 3);
        DATA &last = game.add("sLast", VAR_STRING);
        first.Set(static_cast<int32_t>(1));
        ATTRIBUTES *root = target.GetAClass();
        root->CreateSubAClass(root, "a.b")->SetValue("kept");
        refs.GetArrayElement(1)->SetReference(&first);
        arefs.GetArrayElement(0)->SetAReference(root->FindAClass(root, "a.b"));
        pointers.GetArrayElement(1)->SetPtr(0x1234);
        strings.GetArrayElement(0)->Set("gone");
        object.GetAClass()->SetAttribute("x", "y");
        array.GetArrayElement(2)->Set(static_cast<int32_t>(5));
        last.Set("end");
        image = game.save();
    }
    COMPILER loaded;
    DATA &first = loaded.add("iFirst", VAR_INTEGER);
    DATA &target = loaded.add("Target", VAR_OBJECT);
    DATA &object = loaded.add("nowInt", VAR_INTEGER);
    DATA &array = loaded.add("nowSingle", VAR_INTEGER);
    DATA &last = loaded.add("sLast", VAR_STRING);
    object.Set(static_cast<int32_t>(-1));
    array.Set(static_cast<int32_t>(-2));
    loaded.load(image);
    for (const char *name : {"'goneRefs'", "'goneArefs'", "'gonePointers'", "'goneStrings'", "'nowInt'", "'nowSingle'"})
        require(reported(loaded, name), std::string("a variable the program cannot take was not reported: ") + name);
    require(loaded.errors.size() == 6, "unexpected load reports");
    require(integer(first) == 1 && text(last) == "end", "the variables the program can take were not all loaded");
    require(integer(object) == -1 && integer(array) == -2, "a variable the program cannot take was changed");
    ATTRIBUTES *root = target.GetAClass();
    require(root->FindAClass(root, "a.b") && root->FindAClass(root, "a.b")->GetValue() == "kept", "an object was lost");
}

// References to what the program does not have: a variable index beyond the table, an
// element beyond the array. Reported and left empty.
void referencesBeyondTheProgram()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &ref = game.add("rFar", VAR_REFERENCE);
        DATA &aref = game.add("arFar", VAR_AREFERENCE);
        DATA &table = game.add("Table", VAR_INTEGER, 2);
        DATA &objects = game.add("Objects", VAR_OBJECT, 2);
        DATA &extra = game.add("Extra", VAR_INTEGER);
        ref.SetReference(&extra);
        ATTRIBUTES *root = objects.GetArrayElement(1)->GetAClass();
        aref.SetAReference(root->CreateSubAClass(root, "deep"));
        (void)table;
        image = game.save();
    }
    COMPILER loaded;
    DATA &ref = loaded.add("rFar", VAR_REFERENCE);
    DATA &aref = loaded.add("arFar", VAR_AREFERENCE);
    loaded.add("Table", VAR_INTEGER, 2);
    loaded.add("Objects", VAR_OBJECT, 3); // the saved Objects of 2 resizes it; Extra is gone
    loaded.load(image);
    require(reported(loaded, "'Extra' is not declared") && reported(loaded, "reference 'rFar' names variable #4"),
            "a reference to a variable the program does not have was not reported");
    require(ref.GetReference() == nullptr, "a reference to nothing was bound");
    require(aref.GetAClass() != nullptr && loaded.errors.size() == 2, "the aref into the resized array was lost");
}

// An element whose type changed at run time (`Table[1] += 0.5`, `Table[2] = "x" + ...` in an int
// array): elements carry no type in the save, so it is written converted, loudly.
void arrayElementDrift()
{
    std::vector<char> image;
    std::vector<std::string> saveErrors;
    {
        COMPILER game;
        DATA &table = game.add("Table", VAR_INTEGER, 3);
        DATA &after = game.add("sAfter", VAR_STRING);
        table.GetArrayElement(0)->Set(static_cast<int32_t>(3));
        table.GetArrayElement(1)->Set(2.5f);
        table.GetArrayElement(2)->Set("17");
        after.Set("after");
        image = game.save();
        saveErrors = game.errors;
    }
    require(saveErrors.size() == 2 &&
                saveErrors[0].find("element 1 of 'Table' is float in an array of int") != std::string::npos,
            "a drifted array element was not reported on save");
    COMPILER loaded;
    DATA &table = loaded.add("Table", VAR_INTEGER, 3);
    DATA &after = loaded.add("sAfter", VAR_STRING);
    loaded.load(image);
    require(loaded.errors.empty(), "the converted elements were reported on load");
    require(integer(*table.GetArrayElement(0)) == 3 && integer(*table.GetArrayElement(1)) == 2 &&
                integer(*table.GetArrayElement(2)) == 17,
            "the drifted elements were not saved converted to the array's type");
    require(text(after) == "after", "the variable after a drifted array was misread");
}

// An aref to an object's root attribute is saved with no path: it names the root again.
void rootAttributeReference()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &hero = game.add("Hero", VAR_OBJECT);
        DATA &root = game.add("arHero", VAR_AREFERENCE);
        hero.GetAClass()->SetAttribute("name", "Blaze");
        root.SetAReference(hero.GetAClass());
        image = game.save();
    }
    COMPILER loaded;
    DATA &hero = loaded.add("Hero", VAR_OBJECT);
    DATA &root = loaded.add("arHero", VAR_AREFERENCE);
    loaded.load(image);
    require(root.GetAClass() == hero.GetAClass() && hero.GetAClass()->GetAttributeClass("name") &&
                hero.GetAClass()->GetAttributeClass("name")->GetValue() == "Blaze",
            "an aref to an object's root was not restored");
}

// A seated character's session (CL's dynamic objects): a global object's attribute holds a script reference to an
// element of the characters array (DynamicObject_SetActorNative). It comes back as that element; a reference whose
// target is gone at save time is saved empty and reported; an engine without the section refuses the state; a state
// without any reference keeps the original layout.
void attributeReferences()
{
    const auto build = [](COMPILER &game) -> DATA & {
        DATA &characters = game.add("Characters", VAR_OBJECT, 3);
        characters.GetAClass(2)->SetAttribute("id", "Bartender");
        game.add("iAfter", VAR_INTEGER).Set(static_cast<int32_t>(7));
        return game.add("Sessions", VAR_OBJECT);
    };
    std::vector<char> plain, image;
    {
        COMPILER game;
        DATA &sessions = build(game);
        ATTRIBUTES *session = sessions.GetAClass()->CreateSubAClass(sessions.GetAClass(), "Bartender");
        session->SetAttribute("seatId", "left");
        session->CreateAttribute("actor"); // the same tree without the reference: the attribute has no text
        plain = game.save();
        session->GetAttributeClass("actor")->SetReference(game.VarTab.GetVar(0)->value->GetArrayElement(2));
        image = game.save();
        require(game.errors.empty(), "a live attribute reference was reported");
        storm::save::ParsedState state;
        std::string error;
        require(storm::save::parseState(plain.data(), plain.size(), state, error) && state.attributeReferences.empty(),
                "a state without attribute references has the section");
        // The variables end where they ended without the reference: an engine without the section meets bytes after
        // the last variable and refuses the state.
        require(image.size() > plain.size() && std::equal(plain.begin(), plain.end(), image.begin()),
                "the attribute reference changed the state's layout before its section");
        DATA ghost(VAR_OBJECT);
        session->GetAttributeClass("actor")->SetReference(&ghost);
        game.save();
        require(reported(game, "Ghost reference: attribute 'Sessions.Bartender.actor'"),
                "a reference to no global variable was not reported");
    }
    for (int pass = 0; pass < 2; ++pass)
    {
        COMPILER loaded;
        DATA &sessions = build(loaded);
        loaded.load(image);
        require(loaded.errors.empty(), "loading the attribute reference reported errors");
        ATTRIBUTES *actor = sessions.GetAClass()->FindAClass(sessions.GetAClass(), "Bartender.actor");
        require(actor && actor->GetReference() == loaded.VarTab.GetVar(0)->value->GetArrayElement(2),
                "the attribute reference does not name the saved element");
        require(!actor->HasValue(), "the attribute reference came back with text");
        require(integer(*loaded.VarTab.GetVar(1)->value) == 7, "a variable before the section was lost");
        require(loaded.save() == image, "the loaded state saves differently");
    }
}

// Damaged states are refused by parseState, before the caller erases the running game.
void damagedStates()
{
    COMPILER game;
    game.add("iFirst", VAR_INTEGER).Set(static_cast<int32_t>(1));
    DATA &hero = game.add("Hero", VAR_OBJECT);
    hero.GetAClass()->CreateSubAClass(hero.GetAClass(), "a.b.c")->SetValue("v");
    game.add("sLast", VAR_STRING).Set("end");
    const std::vector<char> image = game.save();
    storm::save::ParsedState state;
    std::string error;
    require(storm::save::parseState(image.data(), image.size(), state, error), "the intact state does not parse");
    require(state.variables.size() == 3 && state.programDirectory == "PROGRAM\\", "the intact state was misread");
    for (size_t size = 0; size < image.size(); ++size)
        require(!storm::save::parseState(image.data(), size, state, error), "a truncated state parsed");
    std::vector<char> longer = image;
    longer.push_back(0);
    require(!storm::save::parseState(longer.data(), longer.size(), state, error) &&
                error.find("after the last variable") != std::string::npos,
            "a state with trailing bytes parsed");
    std::vector<char> unknownType = image;
    // The first variable's type follows its name.
    const char name[] = "iFirst";
    const size_t type =
        std::search(image.begin(), image.end(), name, name + sizeof(name)) - image.begin() + sizeof(name);
    require(unknownType[type] == VAR_INTEGER, "fixture: the type offset moved");
    unknownType[type] = 99;
    const bool parsed = storm::save::parseState(unknownType.data(), unknownType.size(), state, error);
    require(!parsed && error.find("unknown value type 99") != std::string::npos,
            "a state with an unknown type parsed: " + error);
}
} // namespace

// Caribbean Legend's weather: `ref WeatherState[]`, sized at run time (`@WeatherState = 24`), each element bound to an
// array of `new string[16]`, which belongs to no variable; a reference to an element of a global array grown with `@`;
// an array emptied with `@a = 0`. All of it comes back, the references bound to the loaded arrays.
void newArraysAndRunTimeSizes()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &states = game.add("WeatherState", VAR_REFERENCE, 0);
        DATA &grown = game.add("iGrown", VAR_INTEGER, 0);
        DATA &emptied = game.add("iEmptied", VAR_INTEGER, 3);
        DATA &cell = game.add("rCell", VAR_REFERENCE);
        DATA &refs = game.add("rRefs", VAR_REFERENCE);
        states.SetElementsNum(2); // `@`: the declaration keeps its size
        for (uint32_t hour = 0; hour < 2; ++hour)
        {
            DATA &row = game.newArrays.emplace_back();
            row.SetVCompiler(&game);
            row.SetType(VAR_STRING);
            row.SetArraySize(3);
            row.GetArrayElement(2)->Set(hour == 0 ? "calm" : "storm");
            states.GetArrayElement(hour)->SetReference(&row);
        }
        DATA &garbage = game.newArrays.emplace_back(); // no reference reaches it: not saved
        garbage.SetVCompiler(&game);
        garbage.SetType(VAR_INTEGER);
        garbage.SetArraySize(4);
        grown.SetElementsNum(5);
        grown.GetArrayElement(4)->Set(static_cast<int32_t>(42));
        cell.SetReference(grown.GetArrayElement(4));
        DATA &links = game.newArrays.emplace_back(); // `new ref[1]`, its element bound to a global
        links.SetVCompiler(&game);
        links.SetType(VAR_REFERENCE);
        links.SetArraySize(1);
        links.GetArrayElement(0)->SetReference(grown.GetArrayElement(4));
        refs.SetReference(&links);
        emptied.SetElementsNum(0);
        image = game.save();
        require(game.errors.empty(), "the save reported: " + (game.errors.empty() ? "" : game.errors[0]));
    }
    COMPILER loaded;
    DATA &states = loaded.add("WeatherState", VAR_REFERENCE, 0);
    DATA &grown = loaded.add("iGrown", VAR_INTEGER, 0);
    DATA &emptied = loaded.add("iEmptied", VAR_INTEGER, 3);
    DATA &cell = loaded.add("rCell", VAR_REFERENCE);
    DATA &refs = loaded.add("rRefs", VAR_REFERENCE);
    loaded.load(image);
    require(loaded.errors.empty(), "the load reported: " + (loaded.errors.empty() ? "" : loaded.errors[0]));
    require(loaded.newArrays.size() == 3, "the unreachable new array was saved, or a reached one was not");
    require(states.GetArraySize() == 2, "the run-time size of an array of references was lost");
    for (uint32_t hour = 0; hour < 2; ++hour)
    {
        DATA *row = states.GetArrayElement(hour)->GetReference();
        require(row && row->IsArray() && row->GetArraySize() == 3, "a reference to a new array was not bound");
        require(text(*row->GetArrayElement(2)) == (hour == 0 ? "calm" : "storm"), "a new array's value was lost");
    }
    require(cell.GetReference() == grown.GetArrayElement(4) && integer(*grown.GetArrayElement(4)) == 42,
            "a reference to an element of an array grown with @ was not bound");
    DATA *links = refs.GetReference();
    require(links && links->IsArray() && links->GetArrayElement(0)->GetReference() == grown.GetArrayElement(4),
            "a new array of references was not bound, or its element");
    require(emptied.IsArray() && emptied.GetArraySize() == 0, "an emptied array did not stay empty");
}

// A state from elsewhere: an array of no elements still has a known type; a "#new" entry out of order and a reference
// to a new array the state does not have are reported and left, the rest loads.
void untrustedNewArrays()
{
    {
        COMPILER game;
        storm::save::StateWriter out(game);
        storm::save::writeHead(out, "PROGRAM\\", game.SCodec, "seadogs.c", 1);
        out.string("#new 0");
        const uint32_t header[2] = {99, 0};
        out.data(header, sizeof(header));
        const std::vector<char> image = out.take();
        storm::save::ParsedState state;
        std::string error;
        require(!storm::save::parseState(image.data(), image.size(), state, error) &&
                    error.find("unknown value type 99") != std::string::npos,
                "an empty array of an unknown type parsed: " + error);
    }
    std::vector<char> image;
    {
        COMPILER game;
        DATA &rows = game.add("rRows", VAR_REFERENCE, 2);
        game.add("iAfter", VAR_INTEGER).Set(static_cast<int32_t>(5));
        for (uint32_t n = 0; n < 2; ++n)
        {
            DATA &row = game.newArrays.emplace_back();
            row.SetVCompiler(&game);
            row.SetType(VAR_INTEGER);
            row.SetArraySize(1);
            rows.GetArrayElement(n)->SetReference(&row);
        }
        image = game.save();
    }
    const auto replace = [](std::vector<char> bytes, const std::string &from, const std::string &to) {
        const auto at = std::search(bytes.begin(), bytes.end(), from.begin(), from.end());
        require(at != bytes.end(), "fixture: '" + from + "' is not in the state");
        std::copy(to.begin(), to.end(), at);
        return bytes;
    };
    const auto load = [](const std::vector<char> &bytes, const char *problem) {
        COMPILER loaded;
        DATA &rows = loaded.add("rRows", VAR_REFERENCE, 2);
        DATA &after = loaded.add("iAfter", VAR_INTEGER);
        loaded.load(bytes);
        require(reported(loaded, problem), std::string("not reported: ") + problem);
        require(integer(after) == 5, "a variable after the damaged entry was lost");
        return rows.GetArrayElement(1)->GetReference() != nullptr;
    };
    require(!load(replace(image, "#new 1", "#new 7"), "is no saved new array"),
            "a reference to a new array saved out of order was bound");
    const char place1[] = {char(0xff), 1, 0, 0, 0x40};
    const char place9[] = {char(0xff), 9, 0, 0, 0x40};
    require(!load(replace(image, std::string(place1, 5), std::string(place9, 5)), "names variable #"),
            "a reference to a new array the state does not have was bound");
}

// A state whose two references name each other: the second is left unset and reported, so that no chain of references
// loops (every reader follows a chain to its end).
void referencesNamingEachOther()
{
    COMPILER game;
    storm::save::StateWriter out(game);
    storm::save::writeHead(out, "PROGRAM\\", game.SCodec, "seadogs.c", 2);
    for (const uint32_t other : {1u, 0u})
    {
        out.string(other == 1 ? "rFirst" : "rSecond");
        const uint32_t header[2] = {VAR_REFERENCE, 1};
        out.data(header, sizeof(header));
        out.vdword(other);
        out.vdword(storm::save::kNoElement);
    }
    COMPILER loaded;
    DATA &first = loaded.add("rFirst", VAR_REFERENCE);
    DATA &second = loaded.add("rSecond", VAR_REFERENCE);
    loaded.load(out.take());
    require(reported(loaded, "refers back to itself"), "a loop of references was not reported");
    require(first.GetReference() == &second && second.IsNull(), "the loop was not cut at its second reference");
}

// The layout the original engine's saves have: a compact number is one byte below 0xfe, 0xfe and a 16-bit number below
// 0xffff, else 0xff and a 32-bit one; a variable is its type and element count, at least 1 (an unset reference too).
void compactLayout()
{
    COMPILER compiler;
    storm::save::StateWriter out(compiler);
    for (const uint32_t value : {0xfdu, 0xfeu, 0xfffeu, 0xffffu})
        out.vdword(value);
    DATA unset(VAR_REFERENCE);
    const storm::save::ReferenceIndex references(compiler.VarTab);
    out.variable(&unset, "unset", references);
    const auto bytes = out.take();
    const std::vector<uint8_t> expected{0xfd, 0xfe, 0xfe, 0x00, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00};
    require(bytes.size() == expected.size() + 2 * sizeof(uint32_t) + 5,
            "compact numbers or a variable's header resized");
    require(std::equal(expected.begin(), expected.end(), reinterpret_cast<const uint8_t *>(bytes.data())),
            "compact numbers laid out otherwise");
    uint32_t header[2];
    std::memcpy(header, bytes.data() + expected.size(), sizeof(header));
    require(header[0] == VAR_REFERENCE && header[1] == 1, "an unset reference's header");
    require(static_cast<uint8_t>(bytes[expected.size() + sizeof(header)]) == 0xff, "an unset reference is not empty");
}

int main()
{
    compactLayout();
    typeDrift();
    referenceBeforeResize();
    variablesTheProgramCannotTake();
    referencesBeyondTheProgram();
    arrayElementDrift();
    rootAttributeReference();
    damagedStates();
    newArraysAndRunTimeSizes();
    untrustedNewArrays();
    referencesNamingEachOther();
    attributeReferences();
    std::cout << "PASS: a state is parsed whole before it is applied; drifted scalars and elements, resized arrays, "
                 "references into them and variables the program cannot take load without desynchronising the rest\n";
}
