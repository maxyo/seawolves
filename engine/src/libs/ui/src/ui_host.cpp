#include "host_impl.h"

#include "storm_ui/host_tools.h"
#include "elements.h"
#include "text_utf8.h"

#include <plutovg.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

namespace storm::ui
{

namespace
{

class DirectorySource final : public FileSource
{
  public:
    explicit DirectorySource(std::string root) : root_(std::move(root))
    {
    }
    bool Read(const std::string &path, std::vector<uint8_t> &out) override
    {
        if (path.find("..") != std::string::npos)
            return false;
        std::ifstream file(root_ + "/" + path, std::ios::binary);
        if (!file)
            return false;
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

  private:
    std::string root_;
};

bool ReadJson(FileSource &files, const std::string &path, Json &out, std::string *error)
{
    std::vector<uint8_t> bytes;
    if (!files.Read(path, bytes))
    {
        if (error)
            *error = "UI pack lacks " + path;
        return false;
    }
    out = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (out.is_discarded())
    {
        if (error)
            *error = "UI pack: malformed " + path;
        return false;
    }
    return true;
}

// Every document of the context: counts its layout passes (FrameStats::laid_out), so that a
// re-layout of plain elements (not only of components) makes Prepare draw a frame.
class CountedDocument final : public Rml::ElementDocument
{
  public:
    CountedDocument(const Rml::String &tag, FrameStats &stats) : Rml::ElementDocument(tag), stats_(stats)
    {
    }

  protected:
    void OnLayoutStart() override // patches/rmlui-document-layout-start.patch
    {
        layout_started_ = std::chrono::steady_clock::now();
    }
    void OnLayout() override
    {
        Rml::ElementDocument::OnLayout();
        ++stats_.laid_out;
        ++stats_.layout_passes;
        stats_.layout_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - layout_started_).count();
    }

  private:
    FrameStats &stats_;
    std::chrono::steady_clock::time_point layout_started_;
};

// The one live host of this process (Host::Create).
std::atomic<bool> g_host_alive{false};

} // namespace

class PackFiles final : public Rml::FileInterface
{
  public:
    explicit PackFiles(FileSource &source) : source_(source)
    {
    }

    Rml::FileHandle Open(const Rml::String &path) override
    {
        const std::optional<std::string> normal = Normalise(path);
        if (!normal)
        {
            Rml::Log::Message(Rml::Log::LT_ERROR, "UI pack path leaves the pack: %s", path.c_str());
            return 0;
        }
        auto file = std::make_unique<Opened>();
        if (!source_.Read(*normal, file->bytes))
            return 0;
        return reinterpret_cast<Rml::FileHandle>(file.release());
    }
    void Close(Rml::FileHandle handle) override
    {
        delete reinterpret_cast<Opened *>(handle);
    }
    size_t Read(void *buffer, size_t size, Rml::FileHandle handle) override
    {
        auto *file = reinterpret_cast<Opened *>(handle);
        const size_t count = std::min(size, file->bytes.size() - file->position);
        std::memcpy(buffer, file->bytes.data() + file->position, count);
        file->position += count;
        return count;
    }
    bool Seek(Rml::FileHandle handle, long offset, int origin) override
    {
        auto *file = reinterpret_cast<Opened *>(handle);
        const long base = origin == SEEK_SET   ? 0
                          : origin == SEEK_CUR ? static_cast<long>(file->position)
                                               : static_cast<long>(file->bytes.size());
        const long target = base + offset;
        if (target < 0 || static_cast<size_t>(target) > file->bytes.size())
            return false;
        file->position = static_cast<size_t>(target);
        return true;
    }
    size_t Tell(Rml::FileHandle handle) override
    {
        return reinterpret_cast<Opened *>(handle)->position;
    }

    // Collapses "a/../b" so that documents can link styles relative to themselves; nothing for a path that
    // leaves the pack.
    static std::optional<std::string> Normalise(const std::string &path)
    {
        std::vector<std::string> parts;
        size_t at = 0;
        while (at <= path.size())
        {
            size_t end = path.find('/', at);
            if (end == std::string::npos)
                end = path.size();
            const std::string part = path.substr(at, end - at);
            if (part == "..")
            {
                if (parts.empty())
                    return std::nullopt;
                parts.pop_back();
            }
            else if (!part.empty() && part != ".")
                parts.push_back(part);
            at = end + 1;
        }
        std::string out;
        for (const std::string &part : parts)
            out += (out.empty() ? "" : "/") + part;
        return out;
    }

  private:
    struct Opened
    {
        std::vector<uint8_t> bytes;
        size_t position = 0;
    };
    FileSource &source_;
};

double System::GetElapsedTime()
{
    if (clock)
        return clock();
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

bool System::LogMessage(Rml::Log::Type type, const Rml::String &message)
{
#if defined(__ANDROID__)
    const int priority = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? ANDROID_LOG_ERROR
                         : type == Rml::Log::LT_WARNING                            ? ANDROID_LOG_WARN
                                                                                   : ANDROID_LOG_INFO;
    __android_log_print(priority, "StormUi", "%s", message.c_str());
#else
    std::fprintf(stderr, "[ui %d] %s\n", static_cast<int>(type), message.c_str());
#endif
    if (sink)
        sink(type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? LogLevel::Error
             : type == Rml::Log::LT_WARNING                            ? LogLevel::Warning
                                                                       : LogLevel::Info,
             message);
    return true;
}

class CountedDocumentInstancer final : public Rml::ElementInstancer
{
  public:
    explicit CountedDocumentInstancer(FrameStats &stats) : stats_(stats)
    {
    }
    Rml::ElementPtr InstanceElement(Rml::Element *, const Rml::String &tag, const Rml::XMLAttributes &) override
    {
        return Rml::ElementPtr(new CountedDocument(tag, stats_));
    }
    void ReleaseElement(Rml::Element *element) override
    {
        delete element;
    }

  private:
    FrameStats &stats_;
};

HostImpl::~HostImpl()
{
    captures_.clear();
    ticking_.clear();
    router_.reset();
    hud_.reset();
    if (context_)
    {
        context_->UnloadAllDocuments();
        Rml::RemoveContext(context_->GetName());
    }
    ShutdownGameImages();
    SetGameImageRenderer(nullptr);
    ReleaseIconTextures();
    ReleaseDrawTextures();
    if (initialised_)
        Rml::Shutdown();
    registry_.reset();
    g_host_alive = false;
}

bool HostImpl::Start(const Config &config, std::string *error)
{
    config_ = config;
    system_.clock = config.clock;
    system_.sink = config.on_log;
    if (!config.files)
    {
        if (error)
            *error = "no UI pack";
        return false;
    }
    backend_ = config.backend;
    if (!backend_)
    {
        if (error)
            *error = "no render backend given";
        return false;
    }
    files_ = std::make_unique<PackFiles>(*config.files);
    Rml::SetSystemInterface(&system_);
    Rml::SetFileInterface(files_.get());
    renderer_ = std::make_unique<FrameRenderer>(backend_->Interface(), counters_);
    renderer_->Record(config.verify_unchanged);
    Rml::SetRenderInterface(renderer_.get());
    if (!Rml::Initialise())
    {
        if (error)
            *error = "RmlUi failed to start";
        return false;
    }
    initialised_ = true;
    document_instancer_ = std::make_unique<CountedDocumentInstancer>(counters_);
    Rml::Factory::RegisterElementInstancer("body", document_instancer_.get());

    Json pack, components, icons;
    if (!ReadJson(*config.files, "pack.json", pack, error) ||
        !ReadJson(*config.files, pack.value("components", std::string("components.json")), components, error) ||
        !ReadJson(*config.files, pack.value("icons", std::string("icons.json")), icons, error))
        return false;
    settings_ = pack.value("settings", Json::object());
    RegisterBuiltinBehaviors();
    RegisterDrawElements();
    RegisterIconElement();
    RegisterGameImageElement();
    LoadIconPack(icons);
    registry_ = std::make_unique<ComponentRegistry>(*this);
    if (!registry_->Load(components, error))
        return false;
    SetGameImageRenderer(backend_);
    SetGameImageRoot(config.game_root);

    bool fallback = true;
    const Json fonts = pack.value("fonts", Json::array());
    for (const Json &font : fonts)
    {
        if (!Rml::LoadFontFace(font.get<std::string>(), fallback))
            Rml::Log::Message(Rml::Log::LT_ERROR, "Font %s failed to load", font.get<std::string>().c_str());
        fallback = false;
    }

    backend_->SetViewport(config.width, config.height);
    context_ = Rml::CreateContext("storm-ui", {config.width, config.height});
    if (!context_)
    {
        if (error)
            *error = "RmlUi context failed";
        return false;
    }
    display_density_ = config.density;
    context_->SetDensityIndependentPixelRatio(config.density);
    // UI strings: one table per shipped language, chosen by the game language.
    const Json string_files = pack.value("strings", Json::object());
    for (const auto &entry : string_files.items())
    {
        Json table;
        if (!ReadJson(*config.files, entry.value().get<std::string>(), table, error))
            return false;
        strings_[entry.key()] = std::move(table);
    }
    SetTemplateStrings([this](const std::string &key) { return Text(key); });
    SetTemplateStrict(config.strict_templates);
    screen_path_ = pack.value("screen", std::string());
    // The HUD: its document and where the elements of every scene sit. A pack that
    // names a layout must hold a readable one; a pack without a HUD (none shipped
    // today) answers every HUD state with an error.
    hud_path_ = pack.value("hud", std::string());
    const std::string layout_path = pack.value("hudLayout", std::string());
    if (!layout_path.empty())
    {
        Json layout;
        if (!ReadJson(*config.files, layout_path, layout, error) || !hud::ParseLayout(layout, layout_, error))
            return false;
    }
    hud_ = std::make_unique<hud::Hud>(*this);
    router_ = std::make_unique<screens::Router>(*this);
    if (config_.game_hud)
        router_->AttachHud(hud_.get());
    overlay_path_ = pack.value("overlay", std::string());
    if (!overlay_path_.empty())
    {
        overlay_ = LoadPackDocument(overlay_path_);
        if (overlay_)
            overlay_->Show();
    }
    // What Prewarm makes; a pack without the list has nothing made ahead (and says so when asked).
    if (const std::string list_path = pack.value("prewarm", std::string()); !list_path.empty())
    {
        Json list;
        if (!ReadJson(*config.files, list_path, list, error))
            return false;
        if (!list.is_object() || !list.value("fonts", Json()).is_array() || !list.value("icons", Json()).is_array() ||
            !list.value("layoutWidth", Json()).is_number() || list["layoutWidth"].get<double>() <= 0.0)
        {
            if (error)
                *error = list_path + ": expected {layoutWidth, fonts: [font uses], icons: [icon uses]}";
            return false;
        }
        prewarm_width_ = list["layoutWidth"].get<double>();
        std::string why;
        for (const Json &entry : list["fonts"])
        {
            FontUse use;
            if (!ParseFontUse(entry, use, why))
            {
                if (error)
                    *error = list_path + ": " + why;
                return false;
            }
            font_uses_.push_back(std::move(use));
        }
        for (const Json &entry : list["icons"])
        {
            IconUse use;
            if (!ParseIconUse(entry, use, why))
            {
                if (error)
                    *error = list_path + ": " + why;
                return false;
            }
            icon_uses_.push_back(std::move(use));
        }
    }
    return true;
}

bool HostImpl::Prewarm(double budget_ms, bool hidden)
{
    if (font_uses_.empty() && icon_uses_.empty())
    {
        if (!prewarm_reported_)
            Error("UI prewarm: the pack lists nothing to make ahead (pack.json prewarm)");
        prewarm_reported_ = true;
        return false;
    }
    const float ratio = context_->GetDensityIndependentPixelRatio();
    if (prewarm_language_ != language_ || prewarm_ratio_ != ratio)
    {
        prewarm_language_ = language_;
        prewarm_ratio_ = ratio;
        std::string strings;
        if (const auto table = strings_.find(language_); table != strings_.end())
            for (const auto &entry : table->second.items())
                if (entry.value().is_string())
                    strings += entry.value().get<std::string>();
        warmer_.Reset(font_uses_, icon_uses_, BlockSample(strings), ratio,
                      static_cast<double>(config_.width) / ratio / prewarm_width_, config_.show_errors);
        prewarm_ = {};
        prewarm_reported_ = false;
    }
    if (warmer_.Done())
        return false;
    // Its textures are not the frame's: counted apart, the frame's own counters as they were.
    const int textures = counters_.textures;
    const size_t bytes = counters_.texture_bytes;
    const double texture_ms = counters_.texture_ms;
    double spent = 0.0;
    renderer_->OnTextureMade([this](Rml::TextureHandle texture) { backend_->Upload(texture); });
    while (!warmer_.Done())
    {
        // Behind a loading screen the first step of a call runs whatever it costs; in play a step runs only once
        // one of its kind (a font use, a batch of icons) was measured and the slowest of them fits what is left.
        const bool icons = warmer_.NextIsIcons();
        double &slowest = icons ? prewarm_.slowest_icons_ms : prewarm_.slowest_ms;
        const bool unseen = hidden && spent == 0.0;
        if (!unseen && (slowest == 0.0 || spent + slowest > budget_ms))
            break;
        double made_ms = 0.0;
        std::string error;
        warmer_.Step(context_->GetRenderManager(), made_ms, error);
        if (!error.empty())
            Error(error);
        spent += made_ms;
        prewarm_.ms += made_ms;
        slowest = std::max(slowest, made_ms);
    }
    renderer_->OnTextureMade({});
    prewarm_.textures += counters_.textures - textures;
    prewarm_.bytes += counters_.texture_bytes - bytes;
    counters_.textures = textures;
    counters_.texture_bytes = bytes;
    counters_.texture_ms = texture_ms;
    if (warmer_.Done() && !prewarm_reported_)
    {
        prewarm_reported_ = true;
        char line[256];
        std::snprintf(line, sizeof line,
                      "UI prewarm: %zu font uses and %zu icons made for '%s' at %.3f px/dp in %.0f ms (slowest "
                      "font use %.1f ms, icon batch %.1f ms); %d textures, %zu KB",
                      warmer_.Count(), warmer_.Icons(), language_.c_str(), static_cast<double>(prewarm_ratio_),
                      prewarm_.ms, prewarm_.slowest_ms, prewarm_.slowest_icons_ms, prewarm_.textures,
                      prewarm_.bytes / 1024);
        Log(line);
    }
    return !warmer_.Done();
}

bool HostImpl::LoadDocument(const std::string &path)
{
    game_interface_ = false;
    game_screen_.clear();
    router_->CloseInterface();
    const bool opened = OpenDocument(path);
    PublishTouchMap();
    return opened;
}

// Every pack document: debug builds (Config::show_errors) mark its root `ui-debug`, the one switch
// for the inline marks of a broken component (the `.ui-debug .ui-error` stripes of the pack's
// stylesheet, the crossed icon of icons.cpp). Release builds draw nothing in their place; the
// error itself is logged in both.
Rml::ElementDocument *HostImpl::LoadPackDocument(const std::string &path)
{
    Rml::ElementDocument *document = context_->LoadDocument(path);
    if (document && config_.show_errors)
        document->SetClass(kDebugMarksClass, true);
    return document;
}

bool HostImpl::OpenDocument(const std::string &path)
{
    changed_ = true;
    captures_.clear();
    screen_root_ = nullptr;
    if (document_)
    {
        document_->Close();
        document_ = nullptr;
    }
    document_ = LoadPackDocument(path);
    if (!document_)
        return false;
    ReportUnknownComponents(document_);
    document_->Show();
    if (overlay_)
        overlay_->PullToFront();
    return true;
}

void HostImpl::Resize(int width, int height, float density)
{
    changed_ = true;
    config_.width = width;
    config_.height = height;
    display_density_ = density;
    config_.density = density * ui_scale_; // every dp of the layout and the touch slop
    backend_->SetViewport(width, height);
    context_->SetDimensions({width, height});
    context_->SetDensityIndependentPixelRatio(config_.density);
}

void HostImpl::SetDisplayOptions(float ui_scale, float hud_button_opacity, bool left_handed)
{
    if (left_handed != left_handed_)
    {
        left_handed_ = left_handed;
        // Mounted anew with the other placement and props: restyling the mounted elements in place left the
        // touch regions of their parts at the old place (RmlUi keeps a child's absolute offset until it
        // lays it out itself), so every element is created again. A held control is released first.
        if (hud_document_ && hud_document_->IsVisible())
            HideHud();
        hud_props_.clear();
        if (hud_)
            hud_->Rebuild();
    }
    if (ui_scale != ui_scale_)
    {
        ui_scale_ = ui_scale;
        Resize(config_.width, config_.height, display_density_);
        ApplySafeArea(); // the insets came in the display's dp
    }
    if (hud_button_opacity != hud_button_opacity_)
    {
        hud_button_opacity_ = hud_button_opacity;
        ApplyHudButtonOpacity();
    }
}

void HostImpl::SetSafeArea(float left, float top, float right, float bottom)
{
    display_safe_ = {left, top, right, bottom};
    ApplySafeArea();
}

// «Прозрачность кнопок»: a class on the HUD document's body the HUD buttons' style answers,
// hud-buttons-<opacity percent> (none at full opacity; the pack styles each offered step).
void HostImpl::ApplyHudButtonOpacity()
{
    if (!hud_document_)
        return;
    if (!hud_button_class_.empty())
        hud_document_->SetClass(hud_button_class_, false);
    const int percent = static_cast<int>(std::lround(hud_button_opacity_ * 100.0f));
    hud_button_class_ = percent >= 100 ? std::string() : "hud-buttons-" + std::to_string(percent);
    if (!hud_button_class_.empty())
        hud_document_->SetClass(hud_button_class_, true);
    changed_ = true;
}

// The insets in the layout's dp: the display's dp shrink by the player's UI scale.
void HostImpl::ApplySafeArea()
{
    const auto [left, top, right, bottom] = display_safe_;
    const float scale = ui_scale_ > 0.0f ? ui_scale_ : 1.0f;
    const Json safe = {
        {"left", left / scale}, {"top", top / scale}, {"right", right / scale}, {"bottom", bottom / scale}};
    if (safe == safe_)
        return;
    safe_ = safe;
    changed_ = true;
    // Rebuild every component: the insets reach the templates through their scope.
    std::vector<Rml::Element *> pending = {document_, overlay_, hud_document_};
    while (!pending.empty())
    {
        Rml::Element *element = pending.back();
        pending.pop_back();
        if (!element)
            continue;
        if (auto *component = dynamic_cast<ComponentElement *>(element))
            component->SetProps(Json::object());
        for (int i = 0; i < element->GetNumChildren(); ++i)
            pending.push_back(element->GetChild(i));
    }
    PlaceToasts();
}

bool HostImpl::Prepare(double &delay)
{
    const auto started = std::chrono::steady_clock::now();
    const double style_before = counters_.style_ms;
    const double now = system_.GetElapsedTime();
    // A list in motion after the finger left: placed where it is at this frame's time.
    if (scroll_.Update(now))
        changed_ = true;
    const std::vector<Rml::ObserverPtr<Rml::Element>> ticking = ticking_;
    for (const auto &entry : ticking)
        if (auto *component = static_cast<ComponentElement *>(entry.get()))
            component->Tick(now);
    ExpireToasts(now);
    const bool images = GameImagesChanged();
    const double routing = config_.game_hud ? std::min(router_->Tick(), hud_->Tick()) : router_->Tick();

    UpdateContext();
    // Scroll edges move only with a layout, a touch or a scroll in motion: the walk over
    // every element is skipped on frames without any of them.
    const bool moved = changed_ || counters_.laid_out > 0 || context_->GetNextUpdateDelay() <= 0.0;
    const bool edges = moved && ScrollEdgesChanged(context_->GetRootElement());
    const bool clamps = TextClampsChanged() || edges;
    const bool header = moved && ScreenHeaderMoved();
    // Components that measure their layout (columns) set styles from it.
    bool measured = SettleLaidOutComponents();
    if (edges)
        regions_moved_ = true; // a scroll moved what is drawn without a layout
    // clamped text was re-cut for a new width, a scrolling edge changed, the toasts moved under a header
    const bool relaid = clamps || measured || header;
    if (relaid)
        UpdateContext();
    // A measurement is drawn with the layout it makes. Measuring settles in one more pass
    // (a scrollbar it adds: two); one that keeps changing is an error of the component.
    for (int pass = 1; measured && pass <= 2; ++pass)
        if ((measured = SettleLaidOutComponents()))
            UpdateContext();
    if (measured && SettleLaidOutComponents())
        Rml::Log::Message(Rml::Log::LT_ERROR,
                          "UI: a component's measured layout does not settle (it changes on every pass)");
    // A pass above laid out again (re-cut text, a measurement): a list's height can have changed with
    // it, so its scroll edge and fade are placed from that layout, in this frame (else the frame showed the fade
    // where the layout before had it, and the next one moved it).
    if (relaid)
        for (int pass = 0; pass < 2 && ScrollEdgesChanged(context_->GetRootElement()); ++pass)
            UpdateContext();
    // Debug and contract hosts: what a game interface's screen draws for a tap is mapped (else the tap would be
    // reported only when the player makes it, and do nothing).
    if (screen_root_ && counters_.touched > 0 && (config_.show_errors || config_.strict_templates))
        CheckTaps(screen_root_);
    if (config_.log_taps && (moved || counters_.touched > 0))
        LogTaps();
    delay = context_->GetNextUpdateDelay();
    // What makes the shown frame out of date: input and host calls (changed_), component
    // rebuilds that changed elements (a render equal to the shown one changes nothing),
    // layout, ticking behaviours, animations and transitions, game
    // pictures that arrived, re-cut text.
    if (images || clamps || !ticking_.empty() || delay <= 0.0 || counters_.touched > 0 || counters_.laid_out > 0)
        changed_ = true;
    if (changed_)
        delay = 0.0;
    else if (GameImagesPending())
        delay = std::min(delay, 0.05); // a decoded picture is shown on the next frame
    delay = std::min(delay, routing);
    for (const Toast_ &toast : toasts_)
        if (toast.held < 0.0)
            delay = std::min(
                delay, std::max(0.0, (toast.leaving >= 0.0 ? toast.leaving + ToastLeaveSeconds() : toast.until) - now));
    TakeGameImageCost(counters_.picture_ms, counters_.pictures);
    if (tracer_.Active())
        Traced(tracer_.Poll(system_.GetElapsedTime(), opening_ != Opening::None || router_->Answering()));
    const bool draw = changed_ || !drawn_;
    if (draw)
        counters_.update_ms += Milliseconds(started, std::chrono::steady_clock::now());
    else
    {
        // Nothing to draw: this pass is not part of the next frame's cost (a thousand idle
        // passes summed made a tap's frame look like it took a second of styles).
        counters_.style_ms = style_before;
        if (switch_.drawn)
            ReportSwitch(true); // nothing more to draw: the screen has settled
        // The frame on the screen still shows everything: acknowledgements that wait
        // for a drawn frame are given for it.
        hud_->Drawn();
        PublishTouchMap(true);
        ReleaseHeld();
    }
    return draw;
}

// Context::Update, its time split for FrameStats: layout (the documents' own hooks), component
// rebuilds and game pictures (theirs); the rest is styles.
void HostImpl::UpdateContext()
{
    TakeGameImageCost(counters_.picture_ms, counters_.pictures);
    const double rebuilt = counters_.rebuild_ms, acquired = counters_.picture_ms, laid_out = counters_.layout_ms;
    const auto started = std::chrono::steady_clock::now();
    context_->Update();
    const double ms = Milliseconds(started, std::chrono::steady_clock::now());
    TakeGameImageCost(counters_.picture_ms, counters_.pictures); // acquired in their elements' update
    counters_.style_ms += std::max(0.0, ms - (counters_.rebuild_ms - rebuilt) - (counters_.picture_ms - acquired) -
                                            (counters_.layout_ms - laid_out));
}

double HostImpl::Frame()
{
    double delay = 0.0;
    const bool predicted = Prepare(delay);
    const auto updated = std::chrono::steady_clock::now();
    renderer_->BeginFrame();
    backend_->BeginFrame(true);
    context_->Render();
    backend_->EndFrame();
    renderer_->EndFrame();
    counters_.render_ms += Milliseconds(updated, std::chrono::steady_clock::now());
    if (backend_->FrameHeld())
    {
        // Not shown: the backend is still making textures this frame draws (the box shadows
        // of a screen's panels) and left the frame before it on the screen. Drawn again
        // next time; what it cost goes to the frame that is shown, and nothing that waits
        // for a drawn frame (held touches, the HUD's acknowledgements, the switch report)
        // is told.
        renderer_->Dropped();
        ++counters_.held;
        changed_ = true;
        return 0.0;
    }
    counters_.mispredicted = !predicted && config_.verify_unchanged && renderer_->FrameHash() != shown_hash_;
    if (counters_.mispredicted)
        Error("UI frame predicted unchanged drew something else: a change that does not ask for a frame; " +
              renderer_->Difference());
    shown_hash_ = renderer_->FrameHash();
    // Textures made or handed out while drawing (a font atlas that grew for new glyphs, a
    // box shadow made again) make RmlUi re-encode meshes and re-apply its clip state in
    // the next frame: that frame is drawn too.
    changed_ = counters_.textures > 0 || counters_.textures_shared > 0;
    drawn_ = true;
    counters_.number = last_frame_.number + 1;
    counters_.shown = Shown();
    if (tracer_.Active())
        tracer_.Presented(TraceContent(counters_.shown));
    ReportSwitch(false);
    ReportSlowFrame();
    last_frame_ = counters_;
    counters_ = FrameStats(); // what happens until the next frame counts for it
    hud_->Drawn();
    if (last_frame_.laid_out > 0 || last_frame_.touched > 0)
        regions_moved_ = true; // boxes moved, or elements came, went or changed their classes
    PublishTouchMap(true);
    ReleaseHeld();
    return delay;
}

FrameStats HostImpl::LastFrameStats() const
{
    return last_frame_;
}

bool HostImpl::SetProps(const std::string &element_id, const std::string &json)
{
    changed_ = true;
    Rml::Element *element = document_ ? document_->GetElementById(element_id) : nullptr;
    auto *component = dynamic_cast<ComponentElement *>(element);
    const Json props = Json::parse(json, nullptr, false);
    if (!component || !props.is_object())
        return false;
    component->SetProps(props);
    return true;
}

void HostImpl::ScrollTo(const std::string &element_id)
{
    changed_ = true;
    if (Rml::Element *element = document_ ? document_->GetElementById(element_id) : nullptr)
        element->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Start));
}

void HostImpl::SetLanguage(const std::string &game_language)
{
    static const char *const codes[][2] = {{"russian", "ru"}, {"english", "en"}, {"german", "de"},
                                           {"french", "fr"},  {"spanish", "es"}, {"italian", "it"}};
    std::string code;
    for (const auto &pair : codes)
        if (SameNode(game_language, pair[0]))
            code = pair[1];
    if (!strings_.count(code))
    {
        // A game language the pack has no strings for (a mod, an empty name): English,
        // and said so once per language.
        if (unknown_languages_.insert(game_language).second)
            Error("no UI strings for the game language '" + game_language + "'; using en");
        code = "en";
    }
    if (code != language_)
        TemplateStringsChanged(); // the templates' tr() texts follow
    language_ = code;
}

std::string HostImpl::Text(const std::string &key)
{
    const auto table = strings_.find(language_);
    if (table != strings_.end())
    {
        const auto found = table->second.find(key);
        if (found != table->second.end() && found->is_string())
            return found->get<std::string>();
    }
    if (missing_strings_.insert(language_ + ":" + key).second)
        Error("UI string '" + key + "' is missing for language '" + language_ + "'");
    return key;
}

void HostImpl::Log(const std::string &line)
{
    Rml::Log::Message(Rml::Log::LT_INFO, "%s", line.c_str());
}

void HostImpl::Error(const std::string &line)
{
    Rml::Log::Message(Rml::Log::LT_ERROR, "%s", line.c_str());
    if (config_.show_errors)
        ShowLogged(line, "danger");
}

double HostImpl::Now() const
{
    return const_cast<System &>(system_).GetElapsedTime();
}

float HostImpl::Density() const
{
    return config_.density;
}

Json HostImpl::SafeArea() const
{
    return safe_;
}

double HostImpl::Setting(const std::string &name, double fallback) const
{
    const auto found = settings_.find(name);
    return found != settings_.end() && found->is_number() ? found->get<double>() : fallback;
}

void HostImpl::Emit(ComponentElement &source, const std::string &verb, const std::string &value)
{
    std::string id;
    for (Rml::Element *element = &source; element && id.empty(); element = element->GetParentNode())
        id = element->GetId();
    if (tracer_.Active())
        tracer_.Named((hud_document_ && source.GetOwnerDocument() == hud_document_ ? std::string("hud")
                       : screen_root_ && source.GetOwnerDocument() == screen_root_ ? ScreenTag()
                                                                                   : std::string("document")) +
                      " " + id + " " + verb);
    if (overlay_ && source.GetOwnerDocument() == overlay_)
    {
        ToastTouched(source, verb); // the overlay holds only the toasts and the tooltip
        return;
    }
    if (hud_document_ && source.GetOwnerDocument() == hud_document_)
    {
        // A stick or lever let go is no tap with an answer: what the HUD shows next is the game's live state.
        if (verb == "change")
            tracer_.Drop();
        // The HUD: its scene maps the event to engine input.
        hud_->OnEvent({id, verb, value});
        return;
    }
    if (screen_root_)
    {
        // A game interface: its screen maps the event to engine actions.
        const uint64_t predicted = router_->Predicted();
        router_->OnEvent({id, verb, value});
        if (router_->Predicted() != predicted)
            tracer_.Predicted();
        return;
    }
    if (!config_.on_action)
        return;
    Action action;
    action.node = id;
    action.verb = verb;
    action.value = value;
    config_.on_action(action);
}

void HostImpl::Tooltip(Rml::Element *anchor, const std::string &text, bool below)
{
    changed_ = true;
    Rml::Element *tip = overlay_ ? overlay_->GetElementById("tooltip") : nullptr;
    if (!tip)
        return;
    if (!anchor || text.empty())
    {
        tip->SetClass("open", false);
        return;
    }
    const Rml::Vector2f origin = anchor->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = anchor->GetBox().GetSize(Rml::BoxArea::Border);
    const float gap = 6.0f * config_.density;
    const float width =
        std::min(static_cast<float>(config_.width) - 2.0f * gap, std::max(size.x, 280.0f * config_.density));
    const float left = std::clamp(origin.x, gap, static_cast<float>(config_.width) - gap - width);
    tip->SetInnerRML(Rml::StringUtilities::EncodeRml(BindShortWords(text)));
    tip->SetProperty(Rml::PropertyId::Left, Rml::Property(left, Rml::Unit::PX));
    tip->SetProperty(Rml::PropertyId::Width, Rml::Property(width, Rml::Unit::PX));
    if (below)
    {
        tip->SetProperty(Rml::PropertyId::Top, Rml::Property(origin.y + size.y + gap, Rml::Unit::PX));
        tip->RemoveProperty(Rml::PropertyId::Bottom);
    }
    else
    {
        tip->SetProperty(Rml::PropertyId::Bottom,
                         Rml::Property(static_cast<float>(config_.height) - origin.y + gap, Rml::Unit::PX));
        tip->RemoveProperty(Rml::PropertyId::Top);
    }
    tip->SetClass("open", true);
}

void HostImpl::RequestTicks(ComponentElement &component, bool enable)
{
    const auto found =
        std::find_if(ticking_.begin(), ticking_.end(), [&](const auto &entry) { return entry.get() == &component; });
    if (enable && found == ticking_.end())
        ticking_.push_back(component.GetObserverPtr());
    else if (!enable && found != ticking_.end())
        ticking_.erase(found);
}

void HostImpl::PressStarted(ComponentElement &component)
{
    // Game interfaces only (their scripts' click); the HUD and the application screens (the
    // launcher and the downloads, state channels of their own process) have none.
    if (!game_interface_ || !screen_root_ || component.GetOwnerDocument() != screen_root_)
        return;
    // A part that is no button in the game (the interlude: its Esc is silent) sets constants.pressSound false.
    if (!component.Spec().constants.value("pressSound", true))
        return;
    Action action;
    action.screen = "sound";
    action.verb = "press";
    Send(action);
}

FrameStats &HostImpl::Counters()
{
    return counters_;
}

void HostImpl::Forget(ComponentElement &component)
{
    ticking_.erase(std::remove_if(ticking_.begin(), ticking_.end(),
                                  [&](const auto &entry) { return !entry || entry.get() == &component; }),
                   ticking_.end());
}

std::string HostImpl::ScreenTag() const
{
    const Rml::Element *mounted = document_ ? document_->GetElementById("screen") : nullptr;
    return mounted ? mounted->GetTagName() : std::string("screen");
}

// FrameStats::shown.
std::string HostImpl::Shown() const
{
    std::string shown = "nothing";
    if (video_)
        shown = "video";
    else if (Rml::Element *screen = screen_root_ && document_ ? document_->GetElementById("screen") : nullptr)
        shown = screen->GetTagName() + " " + router_->Model().screen + "#" + std::to_string(router_->Model().opening) +
                "." + std::to_string(router_->Model().revision);
    else if (document_)
        shown = "document";
    else if (hud_document_ && hud_document_->IsVisible())
        shown = "hud";
    return opening_ == Opening::None ? shown : shown + " +opening";
}

bool DecodePackImage(const std::string &source, int &width, int &height, std::vector<unsigned char> &rgba)
{
    Rml::FileInterface *files = Rml::GetFileInterface();
    const Rml::FileHandle handle = files->Open(source);
    if (!handle)
        return false;
    std::vector<uint8_t> bytes(files->Length(handle));
    files->Read(bytes.data(), bytes.size(), handle);
    files->Close(handle);
    plutovg_surface_t *surface = plutovg_surface_load_from_image_data(bytes.data(), static_cast<int>(bytes.size()));
    if (!surface)
        return false;
    width = plutovg_surface_get_width(surface);
    height = plutovg_surface_get_height(surface);
    const int stride = plutovg_surface_get_stride(surface);
    const unsigned char *data = plutovg_surface_get_data(surface);
    rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y)
    {
        const auto *row =
            reinterpret_cast<const uint32_t *>(data + static_cast<size_t>(y) * static_cast<size_t>(stride));
        for (int x = 0; x < width; ++x)
        {
            const uint32_t argb = row[x]; // premultiplied
            unsigned char *pixel = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            pixel[0] = static_cast<unsigned char>(argb >> 16);
            pixel[1] = static_cast<unsigned char>(argb >> 8);
            pixel[2] = static_cast<unsigned char>(argb);
            pixel[3] = static_cast<unsigned char>(argb >> 24);
        }
    }
    plutovg_surface_destroy(surface);
    return true;
}

std::unique_ptr<FileSource> DirectoryFiles(std::string root)
{
    return std::make_unique<DirectorySource>(std::move(root));
}

std::unique_ptr<Host> Host::Create(const Config &config, std::string *error)
{
    if (g_host_alive.exchange(true))
    {
        const std::string why =
            "a UI host already lives in this process: one host per process (RmlUi state is process-global)";
        Rml::Log::Message(Rml::Log::LT_ERROR, "%s", why.c_str());
        if (error)
            *error = why;
        return nullptr;
    }
    auto host = std::make_unique<HostImpl>();
    if (!host->Start(config, error))
        return nullptr;
    return host;
}

namespace tools
{

bool SetProps(Host &host, const std::string &element_id, const std::string &json)
{
    return static_cast<HostImpl &>(host).SetProps(element_id, json); // Host::Create makes HostImpl only
}

void ScrollTo(Host &host, const std::string &element_id)
{
    static_cast<HostImpl &>(host).ScrollTo(element_id);
}

} // namespace tools

} // namespace storm::ui
