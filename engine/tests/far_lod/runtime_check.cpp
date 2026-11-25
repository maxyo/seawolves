// The actual GEOM loader and GeomFarLod runtime, with only their external services replaced.
// PostBake keeps the production lambda: tests explicitly advance Load/Bake or drop the queue.
#include "geom.h"
#include <storm/render/frame_owner.hpp>
#include <storm/render/far_lod.hpp>
#include <storm/render/shadow_casters.hpp>
#include <storm/render/texture_cache.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <mutex>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <utility>

#include <catch2/catch.hpp>

namespace fixture
{
size_t allocations = 0, fail_size = 0, failures = 0;
bool count_allocations = false;
const void *watched_source = nullptr;
bool source_released = false;
void release(void *p) noexcept
{
    if (p && p == watched_source)
        source_released = true;
    std::free(p);
}
void *allocate(size_t size)
{
    if (count_allocations)
        ++allocations;
    if (fail_size && size == fail_size)
    {
        fail_size = 0;
        ++failures;
        throw std::bad_alloc();
    }
    if (void *p = std::malloc(std::max(size, size_t{1})))
        return p;
    throw std::bad_alloc();
}
struct Posted
{
    std::shared_ptr<storm::fx::detail::BakeState> state;
    storm::fx::BakeStep step;
};
std::vector<Posted> queue;
size_t posts = 0, parallel_calls = 0;
bool cancel_on_parallel = false;
std::shared_ptr<storm::fx::detail::BakeState> running;
void run()
{
    REQUIRE(queue.size() == 1); // expected exactly one queued production job
    Posted job = std::move(queue.front());
    queue.clear();
    REQUIRE(!job.state->cancelled); // attempted to execute a cancelled job
    running = job.state;
    // The bake may throw (an injected allocation failure): its caller expects that, not a failed check.
    if (!job.step(storm::fx::BakeStage::Load))
    {
        const bool baked = job.step(storm::fx::BakeStage::Bake);
        REQUIRE(baked);
    }
    running.reset();
}
enum class Read
{
    Missing,
    Damaged,
    Throw,
    Hit
};
Read read = Read::Missing;
bool write_throw = false, write_false = false;
class Errors final : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    std::vector<std::string> messages;
    std::vector<std::string> lines; // every level the logger passes
    bool has(std::string_view text) const
    {
        return std::ranges::any_of(messages,
                                   [&](const auto &message) { return message.find(text) != std::string::npos; });
    }

  protected:
    void sink_it_(const spdlog::details::log_msg &message) override
    {
        lines.emplace_back(message.payload.data(), message.payload.size());
        if (message.level >= spdlog::level::err)
            messages.emplace_back(message.payload.data(), message.payload.size());
    }
    void flush_() override
    {
    }
};
std::shared_ptr<Errors> errors;
bool detailed = false;

size_t reads = 0, writes = 0;
std::vector<uint8_t> payload;
void resetCache(Read mode = Read::Missing)
{
    read = mode;
    write_throw = write_false = false;
    reads = writes = 0;
    parallel_calls = 0;
    errors->messages.clear();
}
class Owner final : public storm::render::FrameOwner
{
  public:
    std::vector<storm::render::FrameClient *> clients;
    void AddClient(storm::render::FrameClient *c, int) override
    {
        REQUIRE(std::find(clients.begin(), clients.end(), c) == clients.end()); // duplicate frame registration
        clients.push_back(c);
    }
    void RemoveClient(storm::render::FrameClient *c) override
    {
        auto i = std::find(clients.begin(), clients.end(), c);
        REQUIRE(i != clients.end()); // missing frame client on destruction
        clients.erase(i);
    }
    void frame()
    {
        for (auto *c : clients)
            c->OnFrameBegin({});
    }
    storm::render::SurfaceMetrics Surface() const override
    {
        return {};
    }
    GameTarget CurrentGameTarget() const override
    {
        return {};
    }
    bool DeviceReady() const override
    {
        return true;
    }
    storm::render::TextureId CreateTexture(const storm::render::TextureDesc &, std::span<const std::byte>) override
    {
        return 0;
    }
    bool UpdateTexture(storm::render::TextureId, uint16_t, uint16_t, uint16_t, uint16_t,
                       std::span<const std::byte>) override
    {
        return false;
    }
    void DestroyTexture(storm::render::TextureId) override
    {
    }
    uint16_t BgfxTexture(storm::render::TextureId) const override
    {
        return 0xffff;
    }
    bool IsFormatSupported(storm::render::PixelFormat) const override
    {
        return false;
    }
    storm::render::GamePicture AcquireGamePicture(std::string_view) override
    {
        return {};
    }
    storm::render::GamePicture QueryGamePicture(std::string_view) override
    {
        return {};
    }
    void ReleaseGamePicture(std::string_view) override
    {
    }
} owner;

class Service final : public GEOM_SERVICE
{
  public:
    std::filesystem::path file;
    std::vector<std::vector<uint8_t>> indices, vertices;
    size_t creates = 0, progress_creates = 0, readbacks = 0, draws = 0, releases = 0;
    int32_t drawn_triangles = 0;
    bool throw_readback = false;
    size_t fail_create = 0, fail_lock = 0;
    enum class FailCopy
    {
        None,
        Triangles,
        Vertices
    } fail_copy = FailCopy::None;
    int32_t triangle_bytes = 0, vertex_bytes = 0;
    GEOM_FILE OpenFile(const char *) override
    {
        std::ifstream in(file, std::ios::binary);
        return {std::make_shared<const std::vector<char>>(std::istreambuf_iterator<char>(in),
                                                          std::istreambuf_iterator<char>())};
    }
    bool ReadFile(GEOM_FILE &f, void *data, int32_t bytes) override
    {
        const bool whole = f.position + size_t(bytes) <= f.size();
        if (bytes && whole)
            std::memcpy(data, f.bytes->data() + f.position, size_t(bytes));
        f.position += size_t(bytes);
        if (fail_copy == FailCopy::Triangles && bytes == triangle_bytes && !indices.empty() &&
            data == indices[0].data())
            fail_size = size_t(bytes);
        if (fail_copy == FailCopy::Vertices && bytes == vertex_bytes && !vertices.empty() && data == vertices[0].data())
            fail_size = size_t(bytes);
        return whole;
    }
    void CloseFile(GEOM_FILE &f) override
    {
        f = {};
    }
    void *malloc(int32_t bytes) override
    {
        return std::calloc(std::max(bytes, 1), 1);
    }
    void free(void *p) override
    {
        std::free(p);
    }
    GEOS::ID CreateTexture(const char *, const char *) override
    {
        return -1;
    }
    std::string TextureFile(const char *) const override
    {
        return {};
    }
    GEOS::ID MissingTexture() override
    {
        return -1;
    }
    void ReleaseTexture(GEOS::ID) override
    {
    }
    void SetMaterial(const GEOS::MATERIAL &) override
    {
    }
    GEOS::ID CreateVertexBuffer(int32_t, int32_t size) override
    {
        vertices.emplace_back(size_t(size));
        return int32_t(vertices.size() - 1);
    }
    void *LockVertexBuffer(GEOS::ID id) override
    {
        return vertices.at(size_t(id)).data();
    }
    const void *ReadVertexBuffer(GEOS::ID id) override
    {
        return vertices.at(size_t(id)).data();
    }
    const void *ReadIndexBuffer(GEOS::ID) override
    {
        ++readbacks;
        if (throw_readback)
            throw std::runtime_error("readback forbidden");
        return nullptr;
    }
    void UnlockVertexBuffer(GEOS::ID) override
    {
    }
    void ReleaseVertexBuffer(GEOS::ID) override
    {
    }
    GEOS::ID CreateIndexBuffer(int32_t size, bool progress = true) override
    {
        ++creates;
        progress_creates += progress;
        if (creates == fail_create)
            return -1;
        indices.emplace_back(size_t(size));
        return int32_t(indices.size() - 1);
    }
    void *LockIndexBuffer(GEOS::ID id) override
    {
        if (id == -1 || creates == fail_lock)
            return nullptr;
        return indices.at(size_t(id)).data();
    }
    void UnlockIndexBuffer(GEOS::ID) override
    {
    }
    void ReleaseIndexBuffer(GEOS::ID) override
    {
        ++releases;
    }
    void SetIndexBuffer(GEOS::ID) override
    {
    }
    void SetVertexBuffer(int32_t, GEOS::ID) override
    {
    }
    void DrawIndexedPrimitive(int32_t, int32_t, int32_t, int32_t, int32_t triangles) override
    {
        ++draws;
        drawn_triangles = triangles;
    }
    GEOS::ID CreateLight(GEOS::LIGHT) override
    {
        return -1;
    }
    void ActivateLight(GEOS::ID) override
    {
    }
    void SetCausticMode(bool) override
    {
    }
};

GeomFarLod::Source source(int n)
{
    GeomFarLod::Source s;
    s.name = "resource\\models\\islands\\host.gm";
    s.strides = {36};
    s.buffers.resize(1);
    for (int z = 0; z <= n; ++z)
        for (int x = 0; x <= n; ++x)
        {
            RDF_VERTEX0 vertex{};
            vertex.pos = {float(x), .1f * float((x + z) % 3), float(z)};
            vertex.norm = {0.f, 1.f, 0.f};
            vertex.tu0 = float(x) / n;
            vertex.tv0 = float(z) / n;
            const auto *p = reinterpret_cast<const uint8_t *>(&vertex);
            s.buffers[0].insert(s.buffers[0].end(), p, p + sizeof(vertex));
        }
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x)
        {
            const auto a = uint16_t(z * (n + 1) + x), b = uint16_t(a + 1), c = uint16_t(a + n + 1), d = uint16_t(c + 1);
            s.triangles.insert(s.triangles.end(), {a, c, b, b, c, d});
        }
    s.objects.push_back({0, 0, uint32_t((n + 1) * (n + 1)), 0, uint32_t(n * n * 2)});
    return s;
}
void seed(const GeomFarLod::Source &s)
{
    const storm::geometry::far_lod::MeshBuffer b{s.buffers[0].data(), 36, s.objects[0].vertex_count};
    const auto lods = storm::geometry::far_lod::Build(s.objects, std::span(&b, 1), s.triangles);
    payload = storm::geometry::far_lod::Encode(lods);
}
void clean()
{
    REQUIRE(owner.clients.empty());                // far LOD frame client leaked
    REQUIRE(storm::render::far_lod::pending == 0); // pending far LOD jobs leaked
    REQUIRE(queue.empty());                        // queued work leaked
}
void cacheFailures()
{
    using namespace storm::render::far_lod;
    for (const Read mode : {Read::Missing, Read::Damaged, Read::Throw})
        for (const bool throws : {false, true})
        {
            resetCache(mode);
            write_throw = throws;
            write_false = !throws;
            Service srv;
            {
                GeomFarLod lod(source(4), srv);
                REQUIRE((queue.size() == 1 && owner.clients.size() == 1 &&
                         pending == 1)); // construction did not post/register
                const auto before = posts;
                for (int i = 0; i < 4; ++i)
                    REQUIRE(!lod.Ready());                      // unbaked LOD ready
                REQUIRE((posts == before && srv.creates == 0)); // Ready posted or installed work
                run();
                REQUIRE((reads == 1 && writes == 1 &&
                         parallel_calls > 0));               // cache failure did not build once after one read
                REQUIRE((!lod.Ready() && srv.creates == 0)); // Ready installed completed LODs outside frame begin
                owner.frame();
                REQUIRE(lod.Ready()); // cache write failure discarded successfully built LODs
                REQUIRE(errors->has(throws ? "keeping the in-memory LODs"
                                           : "cannot write the LODs")); // cache write fallback not logged at error
                if (mode == Read::Damaged)
                    REQUIRE(errors->has("does not decode")); // damaged cache fallback not logged at error
                if (mode == Read::Throw)
                    REQUIRE(errors->has("reading the cache entry")); // cache read failure not logged at error
                REQUIRE(srv.progress_creates == 0); // frame installation requested loading-screen progress
            }
            clean();
        }
    std::puts("CL15/18 runtime: damaged/read-throw cache read once; write-false/write-throw retain built LODs and log "
              "errors");
}
void lifecycle()
{
    using namespace storm::render::far_lod;
    resetCache();
    Service srv;
    {
        auto data = source(4);
        watched_source = data.triangles.data();
        source_released = false;
        GeomFarLod lod(std::move(data), srv);
        run();
        REQUIRE(source_released); // completed job retained its triangle source allocation before installation
        watched_source = nullptr;
        owner.frame();
    }
    clean();
    {
        GeomFarLod lod(source(4), srv);
        const auto initial = posts;
        queue.clear();             // the real guard's final release marks the real job dropped
        REQUIRE(!lod.Ready());     // dropped job ready
        REQUIRE(posts == initial); // Ready reposted a dropped job
        owner.frame();
        REQUIRE((posts == initial + 1 && queue.size() == 1 && pending == 1)); // frame did not retry dropped work
        run();
        owner.frame();
        REQUIRE((lod.Ready() && pending == 0)); // retried production job did not install
    }
    clean();
    {
        GeomFarLod lod(source(4), srv);
        const auto state = queue[0].state;
        pixels = 0;
        owner.frame();
        REQUIRE((state->cancelled && !lod.Ready())); // turning LOD off did not cancel queued production work
        queue.clear();
        pixels = 2;
        owner.frame();
        REQUIRE((queue.empty() && pending == 0 &&
                 !lod.Ready())); // disabled source was retained/reposted instead of released
    }
    clean();
    {
        GeomFarLod lod(source(4), srv);
        REQUIRE(queue.size() == 1); // destructor cancellation fixture not posted
    }
    REQUIRE(queue[0].state->cancelled); // destruction did not cancel bake ticket
    queue.clear();
    clean();
    {
        GeomFarLod lod(source(4), srv);
        const auto state = queue[0].state;
        cancel_on_parallel = true;
        bool cancelled = false;
        try
        {
            run();
        }
        catch (const storm::fx::BakeCancelled &)
        {
            cancelled = true;
            running.reset();
        }
        REQUIRE((cancelled && state->cancelled && !lod.Ready() &&
                 pending == 0)); // running production bake did not propagate cancellation after option shutdown
        pixels = 2;
    }
    clean();
    std::puts(
        "CL18 runtime: actual lambda queue drop/retry, queued/running cancellation, source release and destruction");
}
void installFailures()
{
    for (const bool lock : {false, true})
    {
        resetCache();
        Service srv;
        if (lock)
            srv.fail_lock = 1;
        else
            srv.fail_create = 1;
        {
            GeomFarLod lod(source(4), srv);
            run();
            owner.frame();
            REQUIRE(!lod.Ready());                   // failed index upload became ready
            REQUIRE(errors->has("no index buffer")); // index buffer failure fallback not logged at error
        }
        clean();
    }
    std::puts("CL17 runtime: static index buffer creation/lock failure is unready and logged at error");
}
void drawBoundary()
{
    using namespace storm::render::far_lod;
    auto s = source(40);
    seed(s);
    resetCache(Read::Hit);
    Service srv;
    {
        GeomFarLod lod(s, srv);
        run();
        REQUIRE((reads == 1 && writes == 0 && parallel_calls == 0)); // cache hit unexpectedly baked
        owner.frame();
        REQUIRE(lod.Ready()); // cache hit not installed at frame begin
        REQUIRE(
            (srv.creates == 1 + 3 && srv.progress_creates == 0)); // multi-cluster lists not preallocated for every pass
        GEOS::OBJECT object{};
        object.ntriangles = int32_t(s.objects[0].triangle_count);
        object.num_vertices = int32_t(s.objects[0].vertex_count);
        view.valid = true;
        view.forward[2] = 1;
        view.scale = 1;
        view.near_plane = 0;
        view.far_plane = 100000;
        const auto buffers = srv.creates;
        allocations = 0;
        count_allocations = true;
        bool okay = true;
        for (int iteration = 0; iteration < 12; ++iteration)
            for (int p = 0; p < kPasses; ++p)
            {
                pass = Pass(p);
                view.ortho = iteration % 2 == 0;
                view.camera[0] = float(iteration * 10);
                view.camera[2] = -100.f;
                view.pixels_per_unit = iteration % 3 ? .001f : 10000.f;
                view.near_plane = iteration % 4 ? 0.f : 10000.f;
                okay = lod.Ready() && lod.Draw(0, object, 36) && okay;
            }
        count_allocations = false;
        REQUIRE((okay && srv.draws > 0)); // multi-cluster draw route not exercised
        REQUIRE(srv.creates == buffers);  // Draw created an index buffer
        REQUIRE(allocations == 0);        // Ready/Draw allocated CPU memory across changed views and passes
    }
    clean();
    std::puts("CL17 runtime: all 3 passes, changing levels/cameras/depth slices: no Draw buffers or CPU allocations");
}

template <class T> void append(std::ofstream &out, const T &value)
{
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}
void writeModel(const std::filesystem::path &path, const GeomFarLod::Source &s)
{
    std::ofstream out(path, std::ios::binary);
    RDF_HEAD h{};
    h.version = RDF_VERSION;
    h.flags = FLAGS_VISIBLE_PRESENT;
    h.name_size = 1;
    h.nmaterials = h.nobjects = h.nvrtbuffs = 1;
    h.ntriangles = int32_t(s.triangles.size() / 3);
    h.radius = 100;
    append(out, h);
    out.put('\0');
    RDF_MATERIAL material{};
    append(out, material);
    RDF_OBJECT object{};
    object.flags = VISIBLE;
    object.radius = 100;
    object.ntriangles = h.ntriangles;
    object.nvertices = int32_t(s.objects[0].vertex_count);
    append(out, object);
    out.write(reinterpret_cast<const char *>(s.triangles.data()), std::streamsize(s.triangles.size() * 2));
    RDF_VERTEXBUFF buffer{};
    buffer.size = int32_t(s.buffers[0].size());
    append(out, buffer);
    out.write(reinterpret_cast<const char *>(s.buffers[0].data()), buffer.size);
}
void loader(const std::filesystem::path &directory)
{
    using namespace storm::render::far_lod;
    const auto s = source(4);
    const auto file = directory / "runtime-fixture.gm";
    writeModel(file, s);
    for (int mode = 0; mode < 6; ++mode)
    {
        resetCache();
        pixels = mode >= 4 ? 0.f : 2.f;
        Service srv;
        srv.file = file;
        srv.throw_readback = mode == 1;
        srv.triangle_bytes = int32_t(s.triangles.size() * 2);
        srv.vertex_bytes = int32_t(s.buffers[0].size());
        if (mode == 2 || mode == 4)
            srv.fail_copy = Service::FailCopy::Triangles;
        if (mode == 3 || mode == 5)
            srv.fail_copy = Service::FailCopy::Vertices;
        const auto failures_before = failures;
        {
            GEOM geom(s.name.c_str(), nullptr, srv, LOAD_VISIBLE);
            const bool expected = mode < 2;
            REQUIRE(geom.HasFarLods() == expected); // GEOM source-copy failure/off did not skip only far LODs
            REQUIRE(srv.readbacks == 0);            // GEOM read back the index buffer for far LODs
            REQUIRE(failures ==
                    failures_before +
                        (mode == 2 || mode == 3 ? 1 : 0)); // source-copy allocation fault not exercised exactly once
            if (mode == 2 || mode == 3)
                REQUIRE(errors->has("copying")); // source-copy failure fallback not logged at error
            fail_size = 0;
            geom.Draw(nullptr, 0, nullptr);
            REQUIRE((srv.draws == 1 &&
                     srv.drawn_triangles ==
                         int32_t(s.objects[0].triangle_count))); // original model not drawable after optional LOD setup
            if (expected)
            {
                run();
                owner.frame();
                geom.Draw(nullptr, 0, nullptr);
            }
        }
        queue.clear();
        clean();
    }
    std::filesystem::remove(file);
    pixels = 2;
    std::puts(
        "CL14 runtime: real GEOM load without readback, source allocation faults preserve original, Full skips copies");
}
// A place's models: one line once the last of them is installed; a model's own line only with detailed diagnostics.
void logVolume()
{
    resetCache();
    spdlog::set_level(spdlog::level::debug);
    for (const bool on : {false, true})
    {
        detailed = on;
        Service srv;
        {
            GeomFarLod first(source(4), srv), second(source(4), srv);
            REQUIRE(queue.size() == 2);
            for (Posted &job : std::exchange(queue, {}))
                if (!job.step(storm::fx::BakeStage::Load))
                    REQUIRE(job.step(storm::fx::BakeStage::Bake));
            errors->lines.clear();
            owner.frame();
            REQUIRE((first.Ready() && second.Ready()));
            const auto named = std::ranges::count_if(
                errors->lines, [](const std::string &line) { return line.find("host.gm") != std::string::npos; });
            REQUIRE(named == (on ? 2 : 0));
            REQUIRE(errors->lines.size() == size_t(named) + 1);
            REQUIRE(errors->lines.back().find(" 2 models") != std::string::npos); // the place's models, no earlier
        }
        clean();
        errors->lines.clear();
        {
            GeomFarLod dropped(source(4), srv); // the place unloaded before its LODs were made
            queue.clear();
        }
        REQUIRE(errors->lines.empty());
        clean();
    }
    detailed = false;
    spdlog::set_level(spdlog::level::err);
    std::puts("far LOD log: one line per place, a model's own line only with detailed diagnostics");
}
} // namespace fixture

void *operator new(size_t size)
{
    return fixture::allocate(size);
}
void *operator new[](size_t size)
{
    return fixture::allocate(size);
}
void operator delete(void *p) noexcept
{
    fixture::release(p);
}
void operator delete[](void *p) noexcept
{
    fixture::release(p);
}
void operator delete(void *p, size_t) noexcept
{
    fixture::release(p);
}
void operator delete[](void *p, size_t) noexcept
{
    fixture::release(p);
}

namespace storm::fx
{
void BakeTicket::reset()
{
    if (state_)
    {
        state_->cancelled = true;
        state_.reset();
    }
}
BakeTicket PostBake(std::string, BakeStep step, BakePriority)
{
    auto state = std::make_shared<detail::BakeState>();
    fixture::queue.push_back({state, std::move(step)});
    ++fixture::posts;
    return BakeTicket(state);
}
unsigned BakeThreads()
{
    return 1;
}
void ParallelRanges(size_t count, unsigned, const std::function<void(size_t, size_t)> &work, size_t)
{
    ++fixture::parallel_calls;
    if (fixture::cancel_on_parallel)
    {
        fixture::cancel_on_parallel = false;
        storm::render::far_lod::pixels = 0;
        fixture::owner.frame();
    }
    if (fixture::running && fixture::running->cancelled)
        throw BakeCancelled{};
    work(0, count);
}
} // namespace storm::fx
namespace storm::render
{
FrameOwner *FrameOwner::Get()
{
    return &fixture::owner;
}
uint64_t DerivedDataHash(const void *data, size_t size, uint64_t seed)
{
    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
        seed = (seed ^ p[i]) * 1099511628211ull;
    return seed;
}
bool DerivedDataCacheEnabled()
{
    return true;
}
bool DetailedDiagnostics()
{
    return fixture::detailed;
}
// No light publishes shadow volumes here: nothing the camera rejects is drawn for them.
bool ShadowCasterReachLocal(const glm::vec3 &, float)
{
    return false;
}
ShadowCasterOnly::ShadowCasterOnly() : previous_(false)
{
}
ShadowCasterOnly::~ShadowCasterOnly()
{
}
std::optional<std::vector<uint8_t>> ReadDerivedData(uint64_t, const DerivedDataShape &, size_t)
{
    ++fixture::reads;
    switch (fixture::read)
    {
    case fixture::Read::Missing:
        return std::nullopt;
    case fixture::Read::Damaged:
        return std::vector<uint8_t>{0, 1, 2};
    case fixture::Read::Throw:
        throw std::runtime_error("injected cache read failure");
    case fixture::Read::Hit:
        return fixture::payload;
    }
    return std::nullopt;
}
bool WriteDerivedData(uint64_t, const DerivedDataShape &, std::span<const uint8_t>)
{
    ++fixture::writes;
    if (fixture::write_throw)
        throw std::runtime_error("injected cache write failure");
    return !fixture::write_false;
}
} // namespace storm::render
TEST_CASE("far_lod.runtime")
{
    // A failure unwinds through here: no allocation is counted or failed while it is reported.
    struct EndFaults
    {
        ~EndFaults()
        {
            fixture::count_allocations = false;
            fixture::fail_size = 0;
        }
    } endFaults;
    fixture::errors = std::make_shared<fixture::Errors>();
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("runtime-check", fixture::errors));
    spdlog::set_level(spdlog::level::err);
    storm::render::far_lod::pixels = 2;
    fixture::cacheFailures();
    fixture::lifecycle();
    fixture::installFailures();
    fixture::drawBoundary();
    fixture::loader(std::filesystem::temp_directory_path());
    fixture::logVolume();
}
