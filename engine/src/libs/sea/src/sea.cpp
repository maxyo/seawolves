#include "loading_transaction.hpp"
#include "sea.h"
#include "sea_surface_gpu.hpp"
#include "storm/render/sea_surface_check.hpp"

#include <algorithm>
#include <cmath>
#include <execution>
#include <thread>

#include "core.h"
#include "math_inlines.h"
#include "sea_script_protocol.hpp"
#include "shared/sea_ai/script_defines.h"
#include "storm/render/perf_stats.hpp"
#include "v_file_service.h"

#include <fmt/format.h>
#include <numbers>
#include <spdlog/spdlog.h>

CREATE_CLASS(SEA)

#define GC_CONSTANT 0 // Global constants = {0.0, 1.0, 0.5, 0.0000152590218967 = (0.5 / 32767.5)}
#define GC_CONSTANT2 1 // Global constants 2 = {2.0, -1.0, 0.00036621652552071 = (12 / 32767.5), fog}
#define GC_SHADOW_CONST1 3
#define GC_SHADOW_CONST2 7
#define GC_LIGHT4 11 //
#define GC_LIGHT3 13
#define GC_LIGHT2 15
#define GC_LIGHT1 17
#define GC_LIGHT0 19
#define GC_MATERIAL 21 //
#define GC_CAMERA_POS 23 // Local Camera position = {x, y, z, 0.0}
#define GC_MTX_WVP 24 // c[0] = mWorld * mView * mProjection

#define GC_FREE 28

SEA *SEA::pSea = nullptr;

SEA::SEA()
{
    aBlocks.reserve(128);
    aSeaTrash.reserve(512);
    aTrashRects.reserve(512);
    aSeaLights.reserve(512);
    aLightsRects.reserve(512);

    dwMaxDim = 65536 * 2;
    dwMinDim = 128;

    fMaxSeaHeight = 20.0f;
    fMaxSeaDistance = 1600.0f;
    fGridStep = 0.06f;
    fLodScale = 0.4f;

    fFoamV = 3.0f;
    fFoamK = 0.0f;
    fFoamUV = 3.0f;
    fFoamTextureDisturb = 0.7f;
    iFoamTexture = -1;
    bIniFoamEnable = false;
    bFoamEnable = false;

    iSeaLightTexture = -1;
    fLastLightTime = 0.0f;

    fLastTrashTime = 0.0f;
    iSeaTrashTexture = -1;
    bUnderSea = false;
    bUnderSeaStarted = false;
    bUnderSeaEnable = false;
    bStarted = false;

    fFogSeaDensity = 0.0f;
    fFogStartDistance = 0.0f;
    vFogColor = glm::vec3(0.0f);
    bFogEnable = false;

    iVSeaBuffer = -1;
    iISeaBuffer = -1;

    bTempFullMode = false;

    pSea = this;

    bStop = false;

    vWorldOffset = glm::vec3(0.f);

}

SEA::~SEA()
{
    if (dwFramesCut)
        spdlog::warn("sea: in {} frames the farthest blocks did not fit the vertex buffer, at most {} blocks at once",
                     dwFramesCut, dwMostBlocksCut);
    ReleaseTargets();
    rs->TextureRelease(fieldTexture_);
    rs->TextureRelease(slopeTexture_);
    rs->TextureRelease(checkTexture_);

    rs->TextureRelease(iSeaTrashTexture);
    rs->TextureRelease(iSeaLightTexture);

    if (iVSeaBuffer >= 0)
        rs->ReleaseVertexBuffer(iVSeaBuffer);
    iVSeaBuffer = -1;
    if (iISeaBuffer >= 0)
        rs->ReleaseIndexBuffer(iISeaBuffer);
    iISeaBuffer = -1;

    if (iFoamTexture >= 0)
        rs->TextureRelease(iFoamTexture);
    iFoamTexture = -1;

    delete[] pIndices;
    pIndices = nullptr;
    delete[] pVSea;
    pVSea = nullptr;
}

void SEA::SFLB_CreateBuffers()
{
    iVSeaBuffer = rs->CreateVertexBuffer(0, NUM_VERTEXS * sizeof(SeaVertex), D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);
    iISeaBuffer = rs->CreateIndexBuffer(NUM_INDICES * 3 * sizeof(uint16_t), D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);

    pIndices = new uint32_t[NUM_VERTEXS * 3];
    pVSea = new SeaVertex[NUM_VERTEXS];
}

void SEA::CreateVertexDeclaration()
{
    if (vertexDecl_)
        return;

    const D3DVERTEXELEMENT9 VertexElements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()};

    vertexDecl_ = rs->CreateVertexDeclaration(VertexElements);
}

bool SEA::Init()
{
    rs = static_cast<VRenderer *>(core.GetService("renderer"));
    CreateVertexDeclaration();
    {
        auto pEngineIni = fio->OpenIniFile(core.EngineIniFileName());
        bIniFoamEnable = (pEngineIni) ? pEngineIni->GetInt("Sea", "FoamEnable", 1) != 0 : false;
    }

    iFoamTexture = rs->TextureCreate("weather\\sea\\pena\\pena.tga");

    SFLB_CreateBuffers();

    CreateTargets();

    iSeaTrashTexture = rs->TextureCreate("seatrash.tga");
    iSeaLightTexture = rs->TextureCreate("sealight.tga");

    EditMode_Update();

    return true;
}

bool SEA::EditMode_Update()
{
    v4SeaColor = glm::vec4(10.0f / 255.0f, 55.0f / 255.0f, 100.0f / 255.0f, 1.0f);
    v4SkyColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    const float fReflection = 0.8f;
    const float fTransparency = 0.7f;
    fFrenel = 0.75f; // the member: a local here left it unset until the script sets Sea.Frenel
    const float fAttenuation = 0.9f;
    v4SeaParameters = glm::vec4(fAttenuation, fReflection, fTransparency, 0.0f);

    keys_.amp1 = 8.0f;
    keys_.scale1 = 0.5f;
    keys_.amp2 = 1.0f;
    keys_.scale2 = 2.0f;
    keys_.pos_shift = 1.2f;
    keys_.max_sea_height = 200.0f;

    fBumpScale = 0.1f;
    fGridStep = 0.07f;
    fLodScale = 0.5f;

    Realize(0);

    return true;
}

void SEA::AddWaveSource(const storm::render::EffectsWaveSource &source)
{
    waveSources_.push_back(source);
}

// The field WaveXZ below reads, for the coast foam's vertex shader (vs_port_coastfoam.sc): keep the two together.
bool SEA::HeightField(SeaHeightField &field) const
{
    if (!driver_.Ready() || fieldTexture_ < 0)
        return false;
    const storm::sea::ocean::Field &current = driver_.Current();
    field.texture = fieldTexture_;
    field.size = textureSize_;
    field.cascades = textureCascades_;
    for (int c = 0; c < textureCascades_; c++)
        field.patch[c] = current.Layout()[c].patch;
    field.mean_level = current.MeanLevel();
    field.lowest = current.Lowest();
    field.highest = current.Highest();
    field.camera = vCamPos;
    field.reach = fMaxSeaDistance;
    return true;
}

float SEA::WaveXZ(float x, float z, glm::vec3 *pNormal)
{
    if (pNormal)
        *pNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    if (!driver_.Ready())
        return 0.0f;
    const storm::sea::ocean::Field &field = driver_.Current();
    if (Sqr(x - vCamPos.x) + Sqr(z - vCamPos.z) > fMaxSeaDistance * fMaxSeaDistance)
        return field.MeanLevel();
    return field.Height(x, z, field.Layout().size(), pNormal);
}

// The direction the waves travel (storm::sea::ocean::WaveSystem: (cos, sin) in world x, z): downwind. The game's wind
// of angle a blows towards (sin a, cos a): its flags stream and its rain drifts that way (rigging flag.cpp,
// weather rain.cpp); so the waves' angle is pi / 2 - a.
float SEA::WindDirection() const
{
    const entid_t weather = core.GetEntityId("weather");
    Entity *entity = weather ? core.GetEntityPointer(weather) : nullptr;
    ATTRIBUTES *root = entity ? entity->AttributesPointer : nullptr;
    ATTRIBUTES *angle = root ? root->FindAClass(root, "Wind.Angle") : nullptr;
    if (!angle)
        return keys_.wind_direction;
    return std::numbers::pi_v<float> / 2.0f - angle->GetAttributeAsFloat();
}

// This frame's field: the worker's last one becomes the one drawn and read, the next one starts (ocean_driver.hpp).
void SEA::UpdateField(float dt)
{
    // A turn of the wind smaller than this keeps the spectrum: each change eases the whole sea over seconds.
    constexpr float kWindTurn = 0.1f;
    const float wind = WindDirection();
    if (std::abs(std::remainder(wind - keys_.wind_direction, 2.0f * std::numbers::pi_v<float>)) > kWindTurn)
        keys_.wind_direction = wind;
    const storm::render::EffectsSea preset = storm::render::CurrentEffectsSea();
    driver_.Frame(preset.cascades, preset.size, keys_, dt, storm::loading::active || !bStarted);
    const storm::sea::ocean::Field &field = driver_.Current();
    fMaxSeaHeight = 2.0f * std::max(std::abs(field.Lowest()), std::abs(field.Highest())) + 1.0f;
    UploadField();
}

// The field drawn this frame to its textures: the samples as they are (RGBA32F, the vertex shader's) and the slopes
// as the worker packed them in half floats (the pixel shader's, filtered).
void SEA::UploadField()
{
    const storm::sea::ocean::Field &field = driver_.Current();
    const auto cascades = static_cast<int>(field.Layout().size());
    const int n = field.Layout()[0].size;
    if (n != textureSize_ || cascades != textureCascades_)
    {
        TEXTURE_RELEASE(rs, fieldTexture_);
        TEXTURE_RELEASE(rs, slopeTexture_);
        fieldTexture_ = rs->TextureCreate(n, n * cascades, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED);
        slopeTexture_ = rs->VolumeTextureCreate(n, n, cascades, 1, 0, D3DFMT_A16B16G16R16F, D3DPOOL_MANAGED);
        if (fieldTexture_ < 0 || slopeTexture_ < 0)
            throw std::runtime_error("SEA: cannot create the wave field's textures");
        textureSize_ = n;
        textureCascades_ = cascades;
    }
    const size_t row = static_cast<size_t>(n);
    D3DLOCKED_RECT locked{};
    if (!rs->TextureLock(fieldTexture_, 0, locked, D3DLOCK_DISCARD))
        throw std::runtime_error("SEA: cannot fill the wave field's texture");
    for (int c = 0; c < cascades; c++)
        for (int y = 0; y < n; y++)
            memcpy(static_cast<uint8_t *>(locked.pBits) + static_cast<size_t>(c * n + y) * locked.Pitch,
                   field.Samples(c).data() + y * row, row * sizeof(storm::sea::ocean::Displacement));
    rs->TextureUnlock(fieldTexture_, 0);
    D3DLOCKED_BOX box{};
    if (!rs->TextureLockBox(slopeTexture_, 0, box, D3DLOCK_DISCARD))
        throw std::runtime_error("SEA: cannot fill the wave field's slopes");
    for (int c = 0; c < cascades; c++)
    {
        const uint64_t *halves = driver_.SlopeHalves(c).data();
        for (int y = 0; y < n; y++)
            memcpy(static_cast<uint8_t *>(box.pBits) + static_cast<size_t>(c) * box.SlicePitch +
                       static_cast<size_t>(y) * box.RowPitch,
                   halves + y * row, row * sizeof(uint64_t));
    }
    rs->TextureUnlockBox(slopeTexture_, 0);
    storm::render::NoteSeaField(driver_.WorkerMs(), driver_.WaitMs(),
                                static_cast<uint64_t>(n) * n * cascades * (sizeof(storm::sea::ocean::Displacement) + 8));
}

// The grid points of the block this block owns (PrepareIndicesForBlock: [iIFirst, iILast]; the points on an edge
// shared with a block before it are that block's), metres from MeshOrigin, flat: the vertex shader displaces them.
void SEA::WriteBlockVertices(const SeaBlock &block, SeaVertex *pVertices) const
{
    const auto half = static_cast<int32_t>(TreeDim() / 2);
    const int32_t step = 1 << block.iLOD, size0 = block.iSize >> block.iLOD;
    const int32_t x0 = block.iTX * block.iSize - half, z0 = block.iTY * block.iSize - half;
    int32_t point = block.iIStart;
    for (int32_t y = 0; y <= size0; y++)
        for (int32_t x = 0; x <= size0; x++)
        {
            const auto vertex = static_cast<int32_t>(pIndices[point++]);
            if (vertex < block.iIFirst || vertex > block.iILast)
                continue;
            pVertices[vertex] = {glm::vec3(static_cast<float>(x0 + x * step) * fGridStep, 0.0f,
                                           static_cast<float>(z0 + y * step) * fGridStep),
                                 glm::vec3(0.0f), 0.0f, 0.0f};
        }
}

// The mesh's origin in the world (x, z): the middle of the block tree, so the grid's numbers stay within a quarter of
// the tree around the camera (PlaceTree).
glm::dvec2 SEA::MeshOrigin() const
{
    const double half = static_cast<double>(TreeDim() / 2) * fGridStep;
    return {vSeaCenterPos.x + half, vSeaCenterPos.z + half};
}

// GPU surface: the flat grid of this frame's blocks in the buffers, written only when the blocks, the tree's place or
// the grid step changed since the grid there was built; the vertex shader displaces it every frame (sea_surface.sh).
// True when it was written.
bool SEA::BuildMesh()
{
    bool same = bMeshBuilt && vMeshTree == vSeaCenterPos && fMeshGridStep == fGridStep &&
                aMeshBlocks.size() == aBlocks.size() * 4;
    for (size_t i = 0; same && i < aBlocks.size(); i++)
    {
        const SeaBlock &b = aBlocks[i];
        const int32_t *key = &aMeshBlocks[i * 4];
        same = key[0] == b.iTX && key[1] == b.iTY && key[2] == b.iSize && key[3] == b.iLOD;
    }
    if (same)
        return false;
    aMeshBlocks.clear();
    for (const SeaBlock &b : aBlocks)
        aMeshBlocks.insert(aMeshBlocks.end(), {b.iTX, b.iTY, b.iSize, b.iLOD});
    vMeshTree = vSeaCenterPos;
    fMeshGridStep = fGridStep;
    bMeshBuilt = true;

    iVStart = 0;
    iTStart = 0;
    iIStart = 0;
    memset(pIndices, 0xFF, NUM_VERTEXS * sizeof(pIndices[0]) * 3);
    auto *pVertices = static_cast<SeaVertex *>(rs->LockVertexBuffer(iVSeaBuffer, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK));
    pTriangles = static_cast<uint16_t *>(rs->LockIndexBuffer(iISeaBuffer, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK));
    if (!pVertices || !pTriangles)
        throw std::runtime_error("SEA: cannot lock the sea's vertex or index buffer");
    for (uint32_t i = 0; i < aBlocks.size(); i++)
        PrepareIndicesForBlock(i);
    for (const SeaBlock &b : aBlocks)
        WriteBlockVertices(b, pVertices);
    rs->UnLockVertexBuffer(iVSeaBuffer);
    rs->UnLockIndexBuffer(iISeaBuffer);
    return true;
}

// CPU surface (until the GPU surface self-check passed, or for good when it failed): every vertex of this frame's
// blocks displaced here as the vertex shader would (DisplaceBlock) and the buffers written again.
void SEA::BuildCpuMesh()
{
    bMeshBuilt = false; // the GPU surface builds its grid again
    iVStart = 0;
    iTStart = 0;
    iIStart = 0;
    memset(pIndices, 0xFF, NUM_VERTEXS * sizeof(pIndices[0]) * 3);

    auto *pVSea2 = static_cast<SeaVertex *>(rs->LockVertexBuffer(iVSeaBuffer, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK));
    pTriangles = static_cast<uint16_t *>(rs->LockIndexBuffer(iISeaBuffer, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK));
    if (!pVSea2 || !pTriangles)
        throw std::runtime_error("SEA: cannot lock the sea's vertex or index buffer");

    for (uint32_t i = 0; i < aBlocks.size(); i++)
        PrepareIndicesForBlock(i);

    // Blocks write disjoint vertex ranges ([iIFirst, iILast], assigned above by PrepareIndicesForBlock) and only read
    // the field, so they are displaced concurrently. The vertex buffer upload below stays on this thread.
    meshWorkers_.run(aBlocks.size(), [this](size_t index) { DisplaceBlock(aBlocks[index]); });

    if (iVStart && iTStart)
        memcpy(pVSea2, pVSea, iVStart * sizeof(SeaVertex));

    rs->UnLockVertexBuffer(iVSeaBuffer);
    rs->UnLockIndexBuffer(iISeaBuffer);
}

// The block's points of the still water moved by the field (sea_surface.sh's seaSurface on the CPU): the drawn
// position and normal, the still point in (tu, tv) for the pixel shader's slopes.
void SEA::DisplaceBlock(const SeaBlock &block)
{
    WriteBlockVertices(block, pVSea);
    const storm::sea::ocean::Field &field = driver_.Current();
    const size_t cascades = field.Layout().size();
    const glm::dvec2 origin = MeshOrigin();
    const auto toVertex = glm::vec2(static_cast<float>(origin.x - vWorldOffset.x),
                                    static_cast<float>(origin.y - vWorldOffset.z));
    const glm::vec2 camera(static_cast<float>(vCamPos.x - origin.x), static_cast<float>(vCamPos.z - origin.y));
    const float reach = fMaxSeaDistance * fMaxSeaDistance, level = field.MeanLevel();
    for (int32_t v = block.iIFirst; v <= block.iILast; v++)
    {
        SeaVertex &vertex = pVSea[v];
        const glm::vec2 local(vertex.vPos.x, vertex.vPos.z);
        storm::sea::ocean::SurfacePoint point;
        if (glm::dot(local - camera, local - camera) <= reach)
            point = field.Point(static_cast<float>(origin.x + local.x), static_cast<float>(origin.y + local.y),
                                cascades);
        vertex.vPos = glm::vec3(local.x + point.offset.x + toVertex.x, level + point.offset.y,
                                local.y + point.offset.z + toVertex.y);
        vertex.vNormal = point.Normal();
        vertex.tu = local.x;
        vertex.tv = local.y;
    }
}

void SEA::SurfaceCheckFailed(const std::string &why)
{
    surface_ = Surface::Cpu;
    spdlog::error("sea: the GPU surface self-check failed on {}: {}. The sea's vertices stay displaced on the CPU "
                  "(more game-thread time a frame) instead of a wrong surface",
                  storm::render::SeaSurfaceCheckDevice(), why);
}

// The GPU surface self-check (sea_surface_check.hpp): the vertex shader's surface for points of a field of this sea's
// layout, read back and compared with Field::Point (WaveXZ's samples and formula), before the sea's vertices are
// displaced on the GPU.
void SEA::UpdateSurfaceCheck()
{
    if (surface_ != Surface::Checking || !driver_.Ready())
        return;
    // The check's own field, whatever the weather: a rough choppy sea of the layout in use (a texture read as zeros
    // or garbage cannot match), its own texture; a far origin.
    constexpr double kOriginX = 1234.5, kOriginZ = -678.25;
    constexpr uint32_t kPoints = storm::render::kSeaSurfaceCheckMaxPoints;
    float points[kPoints][2];
    for (uint32_t i = 0; i < kPoints; i++)
    {
        // A spiral out to 90 m: every sample phase of every cascade
        const float angle = static_cast<float>(i) * 2.39996f, radius = 1.37f * static_cast<float>(i) + 0.21f;
        points[i][0] = radius * std::cos(angle);
        points[i][1] = radius * std::sin(angle);
    }
    if (surfaceCheck_ == 0)
    {
        const std::vector<storm::sea::ocean::Cascade> &layout = driver_.Current().Layout();
        const int n = layout[0].size, cascades = static_cast<int>(layout.size());
        checkField_ = std::make_unique<storm::sea::ocean::Field>(layout);
        checkField_->SetState(storm::sea::ocean::MapSea2({6.0f, 0.5f, 1.5f, 2.0f, 2.0f, 200.0f, 0.7f}));
        checkField_->Update(17.25);
        TEXTURE_RELEASE(rs, checkTexture_);
        checkTexture_ = rs->TextureCreate(n, n * cascades, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED);
        D3DLOCKED_RECT locked{};
        if (checkTexture_ < 0 || !rs->TextureLock(checkTexture_, 0, locked, 0))
        {
            SurfaceCheckFailed("no texture for its field");
            return;
        }
        for (int c = 0; c < cascades; c++)
            for (int y = 0; y < n; y++)
                memcpy(static_cast<uint8_t *>(locked.pBits) + static_cast<size_t>(c * n + y) * locked.Pitch,
                       checkField_->Samples(c).data() + static_cast<size_t>(y) * n,
                       n * sizeof(storm::sea::ocean::Displacement));
        rs->TextureUnlock(checkTexture_, 0);
        float registers[sea_gpu::kRegisters][4]{};
        for (int c = 0; c < sea_gpu::kMaxCascades; c++)
            if (c < cascades)
                sea_gpu::cascade_register(kOriginX, kOriginZ, n, layout[c].patch, c, registers[c]);
            else
                sea_gpu::absent_cascade(registers[c]);
        registers[3][2] = checkField_->MeanLevel();
        registers[4][2] = 1.0e8f; // every point within reach (camera at the origin)
        std::string why;
        bool retry = false;
        surfaceCheck_ = storm::render::StartSeaSurfaceCheck(rs->GetTextureFromID(checkTexture_), &registers[0][0],
                                                            &points[0][0], kPoints, why, retry);
        surfaceCheckFrames_ = 0;
        if (!surfaceCheck_ && !retry)
            SurfaceCheckFailed("it cannot run: " + why);
        return;
    }
    std::vector<storm::render::SeaSurfaceSample> samples;
    switch (storm::render::PollSeaSurfaceCheck(surfaceCheck_, samples))
    {
    case storm::render::SeaSurfaceCheckState::Pending:
        if (++surfaceCheckFrames_ > kSurfaceCheckFrames)
            SurfaceCheckFailed(fmt::format("no result after {} frames", kSurfaceCheckFrames));
        return;
    case storm::render::SeaSurfaceCheckState::Lost:
        surfaceCheck_ = 0;
        if (++surfaceCheckLosses_ > kSurfaceCheckLosses)
            SurfaceCheckFailed(fmt::format("its result was lost {} times", surfaceCheckLosses_));
        return;
    case storm::render::SeaSurfaceCheckState::Done:
        break;
    }
    surfaceCheck_ = 0;
    // Within float rounding of the two (world positions on the CPU, wrapped sample coordinates on the GPU) and a half
    // float target's precision: a centimetre in a surface metres high, normals to 1/500.
    float worstHeight = 0.0f, worstNormal = 0.0f, worstShift = 0.0f;
    uint32_t wrong = 0;
    std::string example;
    const size_t cascades = checkField_->Layout().size();
    for (uint32_t i = 0; i < kPoints; i++)
    {
        const auto x = static_cast<float>(kOriginX + points[i][0]), z = static_cast<float>(kOriginZ + points[i][1]);
        const storm::sea::ocean::SurfacePoint point = checkField_->Point(x, z, cascades);
        const glm::vec3 normal = point.Normal();
        const float height = point.offset.y + checkField_->MeanLevel();
        const storm::render::SeaSurfaceSample &gpu = samples[i];
        const float dh = std::fabs(gpu.height - height);
        const float dx = std::fabs(gpu.dx - point.offset.x);
        const float dn = std::max(std::fabs(gpu.nx - normal.x), std::fabs(gpu.nz - normal.z));
        const bool bad = !(dh <= 0.004f + 0.002f * std::fabs(height)) || !(dx <= 0.004f + 0.002f * std::fabs(point.offset.x)) ||
                         !(dn <= 0.002f);
        worstHeight = std::max(worstHeight, std::isfinite(dh) ? dh : INFINITY);
        worstShift = std::max(worstShift, std::isfinite(dx) ? dx : INFINITY);
        worstNormal = std::max(worstNormal, std::isfinite(dn) ? dn : INFINITY);
        if (bad && wrong++ == 0)
            example = fmt::format("point {} ({:.2f}, {:.2f}): GPU height {:.4f} shift {:.4f} normal ({:.4f}, {:.4f}), CPU "
                                  "height {:.4f} shift {:.4f} normal ({:.4f}, {:.4f})",
                                  i, points[i][0], points[i][1], gpu.height, gpu.dx, gpu.nx, gpu.nz, height,
                                  point.offset.x, normal.x, normal.z);
    }
    checkField_.reset();
    TEXTURE_RELEASE(rs, checkTexture_);
    if (wrong)
    {
        SurfaceCheckFailed(fmt::format("{} of {} points differ from the CPU's surface (largest difference: height {:.4f} "
                                       "m, shift {:.4f} m, normal {:.4f}); first {}",
                                       wrong, kPoints, worstHeight, worstShift, worstNormal, example));
        return;
    }
    surface_ = Surface::Gpu;
    spdlog::info("sea: the GPU surface self-check passed on {}: {} points match the CPU's surface (largest difference: "
                 "height {:.5f} m, shift {:.5f} m, normal {:.5f}); the sea's vertices are displaced on the GPU",
                 storm::render::SeaSurfaceCheckDevice(), kPoints, worstHeight, worstShift, worstNormal);
}

void SEA::Realize(uint32_t dwDeltaTime)
{

    if (!storm::loading::active && core.Controls->GetDebugAsyncKeyState(VK_SHIFT) < 0 && core.Controls->GetDebugAsyncKeyState('S') < 0)
    {
        if (bTempFullMode)
        {
            bTempFullMode = false;
            fGridStep = fTempGridStep;
            fLodScale = fTempLodScale;
        }
        else
        {
            bTempFullMode = true;
            fTempGridStep = fGridStep;
            fTempLodScale = fLodScale;

            fGridStep = 0.07f;
            fLodScale = 0.5f;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (bStop)
        return;

    float fDeltaTime = static_cast<float>(dwDeltaTime) * 0.001f;
    UpdateField(fDeltaTime);

    uint64_t dwTotalRDTSC;
    RDTSC_B(dwTotalRDTSC);

    glm::mat4 mView, mIView;
    rs->GetTransform(D3DTS_VIEW, storm::math::d3dMatrix(mView));
    mIView = mView;
    storm::math::invertRigid(mIView);

    vCamPos = storm::math::pos(mIView);
#ifdef OLD_WORLD_POS
    vWorldOffset = 0.0f;
#else
    vWorldOffset = vCamPos;
    vWorldOffset.y = 0.f;
#endif

    bUnderSea = false;

    if (vCamPos.y < 0.0f)
        bUnderSea = true;

    if (bStarted && vCamPos.y < WaveXZ(vCamPos.x, vCamPos.z))
        bUnderSea = true;

    if (!storm::loading::active && !bUnderSea)
        bUnderSeaStarted = false;

    {
        // EnvMap_Render / SunRoad_Render draw only these faces this frame (cube_schedule.hpp), first those the view
        // reads in the reflection (its axes: the inverse view's first three columns, the third the look as below; its
        // half angles: the projection's scales).
        const auto now = std::chrono::steady_clock::now();
        storm::sea::CubeSchedule::Frame cube;
        cube.dt = cubeTime_ == std::chrono::steady_clock::time_point{}
                      ? 0.0f : std::chrono::duration<float>(now - cubeTime_).count();
        cubeTime_ = now;
        cube.eye = {vCamPos.x, vCamPos.y, vCamPos.z};
        const glm::mat4 projection = rs->GetProjection();
        const auto axis = [&](int i) { return std::array<float, 3>{mIView[i][0], mIView[i][1], mIView[i][2]}; };
        const auto read = storm::sea::CubeSchedule::ReadFaces(axis(0), axis(1), axis(2), 1.0f / projection[0][0],
                                                              1.0f / projection[1][1]);
        cube.read = read.faces;
        cube.direct = read.direct;
        // The hulls the cubes reflect (this frame's wave sources): a face is drawn again once its reflection of one
        // of them is off by more than the schedule allows.
        cubeObjects_.clear();
        for (const auto &source : waveSources_)
            cubeObjects_.push_back({source.identity,
                                    {source.position.x, source.position.y, source.position.z},
                                    std::max(source.half_length, source.half_width)});
        cube.objects = cubeObjects_.data();
        cube.object_count = cubeObjects_.size();
        cube.under_sea = bUnderSea;
        cube.loading = storm::loading::active;
        cube.sea_in_view = cubeSeaInView_;
        cubeFaces_ = cubeSchedule_.Due(cube);
    }
    EnvMap_Render();
    SunRoad_Render();

    pFrustumPlanes = rs->GetPlanes();

    PlaceTree();

    aBlocks.clear();
    BuildTree(0, 0, 0);
    FitBlocksToBuffer();
    cubeSeaInView_ = !aBlocks.empty(); // the next frame's cube faces (CubeSchedule)

    UpdateSurfaceCheck();
    const bool bGpuSurface = surface_ == Surface::Gpu;
    if (bGpuSurface)
        BuildMesh();
    else
        BuildCpuMesh();

    if (iVStart && iTStart)
    {
        // The hulls' wakes (renderer: fx::SeaWakes): this frame's hulls and the sea's vertex origin.
        storm::render::EffectsSeaFrame waves;
        waves.vertex_origin = vWorldOffset;
        waves.camera = vCamPos;
        {
            // Where the view ray meets the water, at most 45 m out (row-vector view: its third column is the look).
            const glm::vec3 look = storm::math::normalizeOrZero(glm::vec3(mIView[2]));
            float reach = 45.0f;
            if (look.y < -0.01f)
                reach = std::min(reach, std::max(vCamPos.y, 0.0f) / -look.y);
            const glm::vec3 flat = storm::math::normalizeOrZero(glm::vec3(look.x, 0.0f, look.z)) *
                                   (reach * std::sqrt(std::max(0.0f, 1.0f - look.y * look.y)));
            waves.focus = waveFocus_.Update(waves.camera, look,
                {vCamPos.x + flat.x, 0.0f, vCamPos.z + flat.z}, waveSources_, fDeltaTime);
        }
        waves.dt = fDeltaTime;
        waves.sources = waveSources_;
        storm::render::SetEffectsSeaFrame(waves);

        glm::mat4 mWorldView, mWorldViewProj;

        glm::mat4 mView = rs->GetView();
        glm::mat4 mWorld;
        mWorld = glm::mat4(1.0f); // = rs->GetWorld();
        glm::mat4 mProjection = rs->GetProjection();

#ifndef OLD_WORLD_POS
        storm::math::moveInversePosition(mView, -vWorldOffset.x, 0.f, -vWorldOffset.z);
#endif

        mWorldView = mView * mWorld;
        mWorldViewProj = mProjection * mWorldView;

        mWorldViewProj = glm::transpose(mWorldViewProj);

        rs->SetVertexDeclaration(vertexDecl_);

        const auto vec1 = glm::vec4(0.0f, 1.0f, 0.5f, -0.04f);
        const auto vec2 = glm::vec4(2.0f, -1.0f, 0.00036621652552071f, (bFogEnable) ? fFogSeaDensity : 0.0f);
        const auto vec3 = glm::vec4(fFoamV, fFoamK, fFoamUV, 6.0f);
        const auto vec5 = glm::vec4(vCamPos.x - vWorldOffset.x, vCamPos.y, vCamPos.z - vWorldOffset.z, 1.0f);
        const auto vec6 = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        const auto vec7 = glm::vec4(fFrenel, 1.0f, 0.5f, 1.0f);
        const auto vec8 = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        glm::vec3 vTmp = storm::math::normalizeOrZero(glm::vec3(0.0f, 1.0f, 0.0f));
        const auto vec9 = glm::vec4(vTmp.x, vTmp.y, vTmp.z, 1.0f);
        rs->SetVertexShaderConstantF(GC_CONSTANT, (const float *)&vec1, 1);
        rs->SetVertexShaderConstantF(GC_CONSTANT2, (const float *)&vec2, 1);
        rs->SetVertexShaderConstantF(GC_SHADOW_CONST1, (const float *)&vec3, 1);
        const glm::dvec2 origin = MeshOrigin();
        const storm::sea::ocean::Field &field = driver_.Current();
        {
            // The surface's registers (sea_surface_gpu.hpp); c8.w: whether the shader displaces the grid (the CPU
            // surface wrote the vertices displaced). The field is bound either way (the port declares it).
            float surface[sea_gpu::kRegisters][4]{};
            for (int c = 0; c < sea_gpu::kMaxCascades; c++)
                if (c < textureCascades_)
                    sea_gpu::cascade_register(origin.x, origin.y, textureSize_, field.Layout()[c].patch, c, surface[c]);
                else
                    sea_gpu::absent_cascade(surface[c]);
            surface[3][0] = static_cast<float>(origin.x - vWorldOffset.x);
            surface[3][1] = static_cast<float>(origin.y - vWorldOffset.z);
            surface[3][2] = field.MeanLevel();
            surface[4][0] = static_cast<float>(vCamPos.x - origin.x);
            surface[4][1] = static_cast<float>(vCamPos.z - origin.y);
            surface[4][2] = fMaxSeaDistance * fMaxSeaDistance;
            surface[4][3] = bGpuSurface ? sea_gpu::kSurfaceGpu : sea_gpu::kSurfaceCpu;
            rs->SetVertexShaderConstantF(sea_gpu::kFirstRegister, &surface[0][0], sea_gpu::kRegisters);
            rs->TextureSet(D3DVERTEXTEXTURESAMPLER0, fieldTexture_);
        }
        rs->SetVertexShaderConstantF(GC_CAMERA_POS, (const float *)&vec5, 1);
        rs->SetVertexShaderConstantF(GC_MTX_WVP, (const float *)&mWorldViewProj, 4);

        rs->SetVertexShaderConstantF(GC_FREE, (const float *)&v4SeaParameters, 1);
        rs->SetVertexShaderConstantF(GC_FREE + 1, (const float *)&v4SeaColor, 1);
        rs->SetVertexShaderConstantF(GC_FREE + 2, (const float *)&v4SkyColor, 1);

        rs->SetVertexShaderConstantF(GC_FREE + 5, (const float *)&vec6, 1);
        rs->SetVertexShaderConstantF(GC_FREE + 6, (const float *)&vec7, 1);
        // Frenel K, Frenel Max
        rs->SetVertexShaderConstantF(GC_FREE + 7, (const float *)&vec8, 1);
        rs->SetVertexShaderConstantF(GC_FREE + 30, (const float *)&vec9, 1);

        {
            // The field's slopes for the pixel shader (fs_port_sea2.sc: u_psc[1..4]), each cascade's coordinates
            // of the mesh's origin wrapped in double like the vertex shader's.
            float slopes[4][4]{};
            for (int c = 0; c < sea_gpu::kMaxCascades; c++)
            {
                if (c >= textureCascades_)
                {
                    slopes[c][3] = -1.0f;
                    continue;
                }
                const double patch = field.Layout()[c].patch;
                slopes[c][0] = static_cast<float>(origin.x / patch - std::floor(origin.x / patch));
                slopes[c][1] = static_cast<float>(origin.y / patch - std::floor(origin.y / patch));
                slopes[c][2] = static_cast<float>(1.0 / patch);
                slopes[c][3] = (static_cast<float>(c) + 0.5f) / static_cast<float>(textureCascades_);
            }
            // The foam texture's tiling (the original's bump coordinates times Sea.FoamUV), wrapped likewise.
            const double tiling = static_cast<double>(fBumpScale) * fFoamUV;
            slopes[3][0] = static_cast<float>(textureSize_);
            slopes[3][1] = static_cast<float>(tiling);
            slopes[3][2] = static_cast<float>(origin.x * tiling - std::floor(origin.x * tiling));
            slopes[3][3] = static_cast<float>(origin.y * tiling - std::floor(origin.y * tiling));
            rs->SetPixelShaderConstantF(1, &slopes[0][0], 4);

            rs->TextureSet(0, slopeTexture_);
            // The foam texture for the hulls' wakes (the renderer's Sea2 pixel shader reads it only on their foam;
            // the original shader leaves stage 1 unread).
            rs->TextureSet(1, iFoamTexture);
            rs->TextureSet(3, envMap);
            // The sun road's cube: the renderer's Sea2 pixel shader takes the colour of the sun's glint from the sun
            // as the game draws it there (the original shader leaves stage 2 unread).
            rs->TextureSet(2, sunRoadMap);
            // The technique's foam and sun road passes are drawn by this one (fs_port_sea2.sc folds them in); c0.x:
            // whether the foam's is.
            const bool foam = fFoamK > 0.0f && bFoamEnable && bIniFoamEnable;
            const auto layers = glm::vec4(foam ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
            rs->SetPixelShaderConstantF(0, (const float *)&layers, 1);
            rs->DrawIndexedPrimitiveNoVShader(D3DPT_TRIANGLELIST, iVSeaBuffer, sizeof(SeaVertex), iISeaBuffer, 0,
                                              iVStart, 0, iTStart, "Sea2");
            rs->TextureSet(0, -1);
            rs->TextureSet(1, -1);
            rs->TextureSet(2, -1);
        }
        rs->TextureSet(D3DVERTEXTEXTURESAMPLER0, -1); // a skinned draw's bone palette goes there
    }

    if (!storm::loading::active)
        waveSources_.clear(); // the Execute stage of the next frame adds them again
    RDTSC_E(dwTotalRDTSC);


    if (bUnderSea && bUnderSeaEnable)
    {
        glm::vec3 d(20.0f, 5.0f, 20.0f);
        glm::vec3 d2(54.0f, 10.0f, 54.0f);
        if (!storm::loading::active && !bUnderSeaStarted)
        {
            // doing a bunch of sea trash for the first time
            aSeaTrash.clear();
            for (int32_t i = 0; i < 1000; i++)
            {
                aSeaTrash.push_back(SeaTrash{});
                SeaTrash &st = aSeaTrash.back();
                st.vPos = vCamPos + glm::vec3(RRnd(-d.x, d.x), RRnd(-d.y, d.y), RRnd(-d.z, d.z));
                st.vSpeed = glm::vec3(0.1f, 0.0f, 0.0f);
                st.dwSubTexture = rand() % 4;
                st.fSize = RRnd(0.01f, 0.05f);
                st.fTime = static_cast<float>(i) * 0.02f;
            }

            // making a bunch of light poles for the first time
            aSeaLights.clear();
            for (int32_t i = 0; i < 50; i++)
            {
                aSeaLights.push_back(SeaLight{});
                SeaLight &sl = aSeaLights.back();
                sl.vPos = vCamPos + glm::vec3(RRnd(-d2.x, d2.x), RRnd(-d2.y, d2.y), RRnd(-d2.z, d2.z));
                sl.vSpeed = glm::vec3(0.1f, 0.0f, 0.0f);
                sl.dwSubTexture = rand() % 4;
                sl.fTime = static_cast<float>(i);
            }
        }

        // add 1 item of sea trash every second
        fLastTrashTime += fDeltaTime;
        while (!storm::loading::active && fLastTrashTime >= 0.03f)
        {
            fLastTrashTime -= 0.03f;

            aSeaTrash.push_back(SeaTrash{});
            SeaTrash &st = aSeaTrash.back();
            st.vPos = vCamPos + glm::vec3(RRnd(-d.x, d.x), RRnd(-d.y, d.y), RRnd(-d.z, d.z));
            st.vSpeed = glm::vec3(0.1f, 0.0f, 0.0f);
            st.dwSubTexture = rand() % 4;
            st.fTime = 20.0f;
            st.fSize = RRnd(0.01f, 0.05f);
        }

        // Remove the old sea trash
        for (int32_t i = 0; i < aSeaTrash.size(); i++)
        {
            aSeaTrash[i].fTime -= fDeltaTime;
            if (!storm::loading::active && aSeaTrash[i].fTime <= 0.0f)
            {
                aSeaTrash[i] = aSeaTrash.back();
                aSeaTrash.pop_back();
                i--;
                continue;
            }
            aSeaTrash[i].vPos += aSeaTrash[i].vSpeed * fDeltaTime;
        }
        // Render sea trash
        aTrashRects.clear();
        for (int32_t i = 0; i < aSeaTrash.size(); i++)
        {
            aTrashRects.push_back(RS_RECT{});
            RS_RECT &r = aTrashRects.back();

            float fAlpha = 1.0f;
            if (aSeaTrash[i].fTime >= 45.0f)
                fAlpha = 1.0f - (aSeaTrash[i].fTime - 45.0f) / 5.0f;
            if (aSeaTrash[i].fTime <= 5.0f)
                fAlpha = 1.0f - (5.0f - aSeaTrash[i].fTime) / 5.0f;

            r.vPos = aSeaTrash[i].vPos;
            r.fSize = aSeaTrash[i].fSize;
            r.fAngle = 0.0f;
            r.dwColor = ARGB(fAlpha * 255.0f, 255, 255, 255);
            r.dwSubTexture = aSeaTrash[i].dwSubTexture;
        }

        if (aTrashRects.size())
        {
            rs->TextureSet(0, iSeaTrashTexture);
            rs->DrawRects(&aTrashRects[0], aTrashRects.size(), "seatrash", 2, 2);
        }

        // Render schools of fish

        // draw light poles
        // add 1 light pole every second
        fLastLightTime += fDeltaTime;
        while (!storm::loading::active && fLastLightTime >= 1.0f)
        {
            fLastLightTime -= 1.0f;

            aSeaLights.push_back(SeaLight{});
            SeaLight &sl = aSeaLights.back();
            sl.vPos = vCamPos + glm::vec3(RRnd(-d2.x, d2.x), RRnd(-d2.y, d2.y), RRnd(-d2.z, d2.z));
            sl.vSpeed = glm::vec3(0.1f, 0.0f, 0.0f);
            sl.dwSubTexture = rand() % 4;
            sl.fTime = 50.0f;
        }

        // Remove the old light poles
        for (int32_t i = 0; i < aSeaLights.size(); i++)
        {
            aSeaLights[i].fTime -= fDeltaTime;
            if (!storm::loading::active && aSeaLights[i].fTime <= 0.0f)
            {
                aSeaLights[i] = aSeaLights.back();
                aSeaLights.pop_back();
                i--;
                continue;
            }
            aSeaLights[i].vPos += aSeaLights[i].vSpeed * fDeltaTime;
        }

        float fAlphaK = (vCamPos.y < 0.0f) ? Max(0.0f, 1.0f + vCamPos.y / 30.0f) : 1.0f;

        // draw light poles
        aLightsRects.clear();
        for (int32_t i = 0; i < aSeaLights.size(); i++)
        {
            aLightsRects.push_back(RS_RECT{});
            RS_RECT &r = aLightsRects.back();
            float fAlpha = 1.0f;
            if (aSeaLights[i].fTime >= 45.0f)
                fAlpha = 1.0f - (aSeaLights[i].fTime - 45.0f) / 5.0f;
            if (aSeaLights[i].fTime <= 5.0f)
                fAlpha = 1.0f - (5.0f - aSeaLights[i].fTime) / 5.0f;

            glm::vec3 v = aSeaLights[i].vPos;
            r.vPos = glm::vec3(v.x, WaveXZ(v.x, v.z) - 5.0f, v.z);
            r.fSize = 20.0f;
            r.fAngle = 0.0f;
            r.dwColor = ARGB(0.07f * fAlphaK * fAlpha * 255.0f, 227, 245, 153);
            r.dwSubTexture = aSeaLights[i].dwSubTexture;
        }

        if (aLightsRects.size())
        {
            rs->TextureSet(0, iSeaLightTexture);
            rs->DrawRects(&aLightsRects[0], aLightsRects.size(), "seatrash", 2, 2, 0.5f);
        }

        if (!storm::loading::active) bUnderSeaStarted = true;
    }

    bStarted = true;
}

float SEA::Trace(const glm::vec3 &vSrc, const glm::vec3 &vDst)
{
    const int32_t iNumTests = 5;
    float fRes = 2.0f;
    const float fDV = 1.0f / static_cast<float>(iNumTests - 1);

    if (vSrc.y > fMaxSeaHeight && vDst.y > fMaxSeaHeight)
        return 2.0f;

    for (int32_t i = 0; i < iNumTests; i++)
    {
        const glm::vec3 vTemp = vSrc + static_cast<float>(i) * fDV * (vDst - vSrc);
        const float fWaveY = WaveXZ(vTemp.x, vTemp.z, nullptr);

        if (fWaveY > vTemp.y)
            return static_cast<float>(i) * fDV;
    }

    return 2.0f;
}

float SEA::Cannon_Trace(int32_t iBallOwner, int32_t iCannonType, const glm::vec3 &vSrc, const glm::vec3 &vDst)
{
    const float fRes = Trace(vSrc, vDst);

    if (fRes <= 1.0f)
    {
        const glm::vec3 vTemp = vSrc + fRes * (vDst - vSrc);
        const float fTmpY = WaveXZ(vTemp.x, vTemp.z, nullptr);
        MESSAGE event;
        storm::sea_ai::ResetBallHit(event, storm::sea_ai::seaScriptProtocol(), iBallOwner,
                                    glm::vec3(vTemp.x, fTmpY, vTemp.z), iCannonType);
        core.Event(BALL_WATER_HIT, event);
    }

    return fRes;
}

uint32_t SEA::AttributeChanged(ATTRIBUTES *pAttribute)
{
    ATTRIBUTES *pParent = pAttribute->GetParent();
    ATTRIBUTES *pParent2 = (pParent) ? pParent->GetParent() : nullptr;

    if (*pParent == "isDone")
    {
        Realize(0);
    }

    // Sea.Sea2: the waves' keys feed the wave field's spectrum (ocean_spectrum.hpp MapSea2); what animated the old
    // height frames (AnimSpeed*, MoveSpeed*, BumpSpeed) and SimpleSea have no meaning for it and are not read.
    if (*pParent == "Sea2")
    {
        if (*pAttribute == "WaterColor")
        {
            v4SeaColor = storm::math::colorFromARGB(pAttribute->GetAttributeAsDword());
            return 0;
        }
        if (*pAttribute == "SkyColor")
        {
            v4SkyColor = storm::math::colorFromARGB(pAttribute->GetAttributeAsDword());
            return 0;
        }

        if (*pAttribute == "Amp1")
        {
            keys_.amp1 = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Scale1")
        {
            keys_.scale1 = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "Amp2")
        {
            keys_.amp2 = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Scale2")
        {
            keys_.scale2 = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "PosShift")
        {
            keys_.pos_shift = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "BumpScale")
        {
            fBumpScale = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "LodScale")
        {
            fLodScale = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "GridStep")
        {
            fGridStep = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "Reflection")
        {
            v4SeaParameters.y = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Transparency")
        {
            v4SeaParameters.z = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Attenuation")
        {
            v4SeaParameters.x = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "Frenel")
        {
            fFrenel = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "FoamV")
        {
            fFoamV = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamK")
        {
            fFoamK = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamUV")
        {
            fFoamUV = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamTexDisturb")
        {
            fFoamTextureDisturb = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamEnable")
        {
            bFoamEnable = pAttribute->GetAttributeAsDword() != 0;
            return 0;
        }

        return 0;
    }

    if (*pParent == "fog")
    {
        if (*pAttribute == "Enable")
        {
            bFogEnable = pAttribute->GetAttributeAsDword() != 0;
            return 0;
        }
        if (*pAttribute == "Start")
        {
            fFogStartDistance = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Color")
        {
            vFogColor = (1.0f / 255.0f) * COLOR2VECTOR(pAttribute->GetAttributeAsDword());
            return 0;
        }
        if (*pAttribute == "SeaDensity")
        {
            fFogSeaDensity = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        return 0;
    }

    if (*pAttribute == "Stop")
    {
        bStop = AttributesPointer->GetAttributeAsDword("Stop") != 0;
    }

    if (*pAttribute == "UnderWater")
    {
        bUnderSeaEnable = AttributesPointer->GetAttributeAsDword("UnderWater") != 0;
    }

    if (*pAttribute == "MaxSeaHeight")
    {
        keys_.max_sea_height = AttributesPointer->GetAttributeAsFloat("MaxSeaHeight", 50.0f);
    }

    if (*pAttribute == "MaxDim")        { dwMaxDim = pAttribute->GetAttributeAsDword(); return 0; }
    if (*pAttribute == "MinDim")        { dwMinDim = pAttribute->GetAttributeAsDword(); return 0; }

    if (*pAttribute == "MaxSeaDistance")
    {
        fMaxSeaDistance = AttributesPointer->GetAttributeAsFloat("MaxSeaDistance");
    }

    return 0;
}

void SEA::CreateTargets()
{
    envMap = rs->CubeTextureCreate(128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R5G6B5, D3DPOOL_DEFAULT);
    sunRoadMap = rs->CubeTextureCreate(128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R5G6B5, D3DPOOL_DEFAULT);
    zStencil = rs->DepthSurfaceCreate(128, 128, D3DFMT_D24S8);

}

void SEA::ReleaseTargets()
{
    TEXTURE_RELEASE(rs, envMap);
    TEXTURE_RELEASE(rs, sunRoadMap);
    rs->DepthSurfaceRelease(zStencil);
    zStencil = {};
}

void SEA::LostRender()
{
    ReleaseTargets();
}

void SEA::RestoreRender()
{
    cubeSchedule_.RefreshAll(); // new cube targets hold nothing yet
    CreateTargets();
}
