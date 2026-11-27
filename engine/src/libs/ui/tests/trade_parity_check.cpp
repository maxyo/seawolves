// The trade screens (store, contraband, cannons, items trade, storage, colony loot, chests, the cargo of the ship
// exchange) on recorded models, driven by the taps the pack check's crawl makes (tests/trade/parity.json): the view
// after each tap and once the game has answered, and what each tap sent, against what is recorded there. The router
// prefers the screens of the application's scripts, so a screen moved to script must give what its C++ screen gave.
// On a difference the results are written to trade-parity-actual.json in the working directory.
#include "fakes.h"
#include "../tools/script_game.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace storm::ui;
using namespace storm::ui::screens;

namespace
{
std::string models, app_pack;

Json ReadJson(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    Expect(static_cast<bool>(file), "read " + path);
    return Json::parse(std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
}

// The views a case's runs showed, one copy of each. Written as the case's first view and every other one as its JSON
// patch from the first: the views of a screen differ in a few props.
class Views
{
  public:
    size_t Index(Json view)
    {
        const auto [at, added] = index_.try_emplace(view.dump(), views_.size());
        if (added)
            views_.push_back(std::move(view));
        return at->second;
    }
    const Json &At(size_t index) const
    {
        return views_.at(index);
    }
    Json Write() const
    {
        Json out = Json::array();
        for (const Json &view : views_)
            out.push_back(out.empty() ? view : Json::diff(views_.front(), view));
        return out;
    }
    static Views Read(const Json &written)
    {
        Views views;
        for (const Json &entry : written)
            views.views_.push_back(views.views_.empty() ? entry : views.views_.front().patch(entry));
        return views;
    }

  private:
    std::vector<Json> views_;
    std::map<std::string, size_t> index_;
};

// One router with a case's model fed as the pack check feeds it; the game answers every widget action as the pack
// check's crawl does (applied, then the same model again as the next revision).
class Run
{
  public:
    explicit Run(const Json &c)
    {
        router_.SetScriptScreens(ScriptGame::Get().Entries(app_pack));
        model_ = Load(models, c.at("model"));
        router_.SetInterface(model_);
        if (c.contains("answer"))
        {
            Advance(0.05);
            model_ = Load(models, c.at("answer"));
            router_.SetInterface(model_);
        }
        Advance(1.5);
        AnswerAll();
    }

    // The events of one tap (a step of a run that is a list), a frame apart as a held press reports them; the views in
    // the frame after it and once the game has answered, by their index in `views`. In a flow the game answers in the
    // run's own frames (`answer` false): nothing is answered after the tap.
    Json Tap(const Json &events, Views &views, bool answer)
    {
        const size_t sent = surface_.commands.size(), toasts = surface_.toasts.size(), errors = surface_.errors.size();
        for (const Json &event : events)
        {
            router_.OnEvent({event[0], event[1], event[2]});
            Advance(1.0 / 60.0);
        }
        Json result = Json::object();
        result["view"] = Index(views);
        result["sent"] = Json(std::vector<std::string>(surface_.commands.begin() + sent, surface_.commands.end()));
        if (answer)
            AnswerAll();
        result["settled"] = Index(views);
        result["toasts"] = Json(std::vector<std::string>(surface_.toasts.begin() + toasts, surface_.toasts.end()));
        result["errors"] = surface_.errors.size() - errors;
        return result;
    }

    // A game frame of a flow (pack-contract.json "flows"): `wait` frames in which the game shows nothing new, then
    // the answers to every widget action not answered yet (`answer`) and the model it publishes (`model`).
    Json Frame(const Json &frame, Views &views)
    {
        const size_t sent = surface_.commands.size(), toasts = surface_.toasts.size(), errors = surface_.errors.size();
        Advance(frame.value("wait", 0) / 60.0);
        if (frame.value("answer", false))
            for (; surface_.answered < surface_.actions.size(); ++surface_.answered)
                if (const uint64_t request = surface_.actions[surface_.answered].request)
                    router_.Answer(request, true, {});
        if (frame.contains("model"))
        {
            const uint64_t opening = model_.opening, revision = model_.revision;
            model_ = Load(models + "/..", frame.at("model"));
            model_.opening = opening;
            model_.revision = revision + 1;
            router_.SetInterface(model_);
        }
        Advance(1.0 / 60.0);
        const size_t view = Index(views);
        return Json{{"view", view},
                    {"settled", view},
                    {"sent", std::vector<std::string>(surface_.commands.begin() + sent, surface_.commands.end())},
                    {"toasts", std::vector<std::string>(surface_.toasts.begin() + toasts, surface_.toasts.end())},
                    {"errors", surface_.errors.size() - errors}};
    }

    Json Opened(Views &views)
    {
        const size_t view = Index(views);
        return Json{{"view", view},
                    {"settled", view},
                    {"sent", surface_.commands},
                    {"toasts", surface_.toasts},
                    {"errors", surface_.errors.size()}};
    }

  private:
    // The clock runs `seconds`, the router ticked when it asks to be.
    void Advance(double seconds)
    {
        const double end = surface_.now + seconds;
        while (surface_.now < end)
        {
            const double due = router_.Tick();
            surface_.now = std::min(end, surface_.now + std::max(due, 1.0 / 60.0));
        }
        router_.Tick();
    }
    void AnswerAll()
    {
        bool applied = false;
        for (; surface_.answered < surface_.actions.size(); ++surface_.answered)
            if (const uint64_t request = surface_.actions[surface_.answered].request)
            {
                router_.Answer(request, true, {});
                applied = true;
            }
        if (applied)
        {
            ++model_.revision;
            router_.SetInterface(model_);
        }
        Advance(0.1);
    }
    // The view on the surface now, as an index into `views` (one copy of each).
    size_t Index(Views &views) const
    {
        return views.Index(Json{{"tag", surface_.tag}, {"props", surface_.props}});
    }

    FakeSurface surface_;
    Router router_{surface_};
    InterfaceModel model_;
};

// The first difference of two results, as a path.
std::string Difference(const Json &expected, const Json &actual, const std::string &path)
{
    // A number read back from the file is unsigned where the screen wrote an int: compared by value.
    if (expected.is_number() && actual.is_number())
        return expected == actual ? std::string() : path + ": " + expected.dump() + " became " + actual.dump();
    if (expected.type() != actual.type())
        return path + ": " + expected.dump().substr(0, 200) + " became " + actual.dump().substr(0, 200);
    if (expected.is_object())
    {
        for (const auto &item : expected.items())
            if (!actual.contains(item.key()))
                return path + "/" + item.key() + ": gone";
            else if (std::string d = Difference(item.value(), actual.at(item.key()), path + "/" + item.key());
                     !d.empty())
                return d;
        for (const auto &item : actual.items())
            if (!expected.contains(item.key()))
                return path + "/" + item.key() + ": new, " + item.value().dump().substr(0, 200);
        return {};
    }
    if (expected.is_array())
    {
        for (size_t i = 0; i < std::min(expected.size(), actual.size()); ++i)
            if (std::string d = Difference(expected[i], actual[i], path + "/" + std::to_string(i)); !d.empty())
                return d;
        if (expected.size() != actual.size())
            return path + ": " + std::to_string(expected.size()) + " items became " + std::to_string(actual.size());
        return {};
    }
    return expected == actual ? std::string() : path + ": " + expected.dump() + " became " + actual.dump();
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: storm_ui_trade_parity_unit MODELS PARITY_JSON APP_PACK\n");
        return 2;
    }
    models = argv[1];
    app_pack = argv[3];
    const Json recorded = ReadJson(argv[2]);
    Json actual{{"cases", Json::array()}};
    int differences = 0;
    size_t shown = 0;
    for (const Json &c : recorded.at("cases"))
    {
        Views views;
        const Views expected = Views::Read(c.at("views"));
        Json results = Json::array();
        for (const Json &taps : c.at("runs"))
        {
            Run run(c);
            Json one = Json::array({run.Opened(views)});
            const bool flow = std::ranges::any_of(taps, [](const Json &step) { return step.is_object(); });
            for (const Json &step : taps)
                one.push_back(step.is_object() ? run.Frame(step, views) : run.Tap(step, views, !flow));
            results.push_back(std::move(one));
        }
        Json out = c;
        out["results"] = results;
        out["views"] = views.Write();
        shown += out["views"].size();
        // A result names its views by index: compared by what they are, not by where they are in the table.
        const auto resolve = [](Json result, const Views &table) {
            for (Json &tap : result)
                for (const char *key : {"view", "settled"})
                    tap[key] = table.At(tap.at(key).get<size_t>());
            return result;
        };
        for (size_t r = 0; r < results.size(); ++r)
        {
            const std::string where = c.at("name").get<std::string>() + " run " + std::to_string(r);
            const std::string d =
                Difference(resolve(c.at("results").at(r), expected), resolve(results[r], views), where);
            if (!d.empty() && ++differences <= 10)
                Expect(false, d);
        }
        actual["cases"].push_back(std::move(out));
    }
    if (differences)
    {
        std::ofstream out("trade-parity-actual.json");
        out << "{\"$comment\": " << recorded.at("$comment").dump() << ",\n \"cases\": [";
        for (size_t c = 0; c < actual["cases"].size(); ++c)
            out << (c ? ",\n  " : "\n  ") << actual["cases"][c].dump(); // a case a line
        out << "\n ]}\n";
        std::fprintf(stderr, "%d run(s) differ; the results are in trade-parity-actual.json\n", differences);
    }
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("trade parity: %zu cases, %zu views\n", recorded.at("cases").size(), shown);
    return failures ? 1 : 0;
}
