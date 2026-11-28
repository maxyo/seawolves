// *  Author : Vano, 1861; Description : Sea/Ocean/Water/Lake/River/pool  *
#pragma once

#include "sea_base.h"
#include <chrono>
#include "renderer.h"
#include "vma.hpp"
#include "cube_schedule.hpp"
#include "ocean_driver.hpp"
#include "parallel_for.hpp"
#include "sea_tree.hpp"
#include "wave_focus.hpp"


class SEA : public SEA_BASE, private SeaBlockTree
{
  private:
    uint32_t dwIslandCode = MakeHashValue("island");
    uint32_t dwShipCode = MakeHashValue("ship");
    uint32_t dwSailCode = MakeHashValue("sail");

    storm::render::VertexDeclarationId vertexDecl_;

    // A vertex of the sea's mesh. GPU surface: a point of the still water (vPos, metres from MeshOrigin; the rest 0),
    // displaced by the vertex shader (sea_surface_gpu.hpp). CPU surface: displaced here, every frame, the point of
    // the still water in (tu, tv).
    struct SeaVertex
    {
        glm::vec3 vPos;
        glm::vec3 vNormal;
        float tu, tv;
    };

    struct SeaTrash
    {
        glm::vec3 vPos;
        glm::vec3 vSpeed;
        uint32_t dwSubTexture;
        float fTime;
        float fSize;
    };

    struct SeaLight
    {
        glm::vec3 vPos;
        glm::vec3 vSpeed;
        uint32_t dwSubTexture;
        float fTime;
    };

    VRenderer *rs;
    static SEA *pSea;

    float fMaxSeaDistance;
    float fBumpScale; // the foam texture's tiling (with Sea.FoamUV)

    glm::vec3 vWorldOffset;

    bool bStarted;
    bool bUnderSea;
    bool bUnderSeaEnable;
    float bUnderSeaStarted;

    int32_t iVSeaBuffer, iISeaBuffer;

    // Renderer texture ids: the environment / sun road cubes.
    int32_t envMap = -1, sunRoadMap = -1;
    storm::render::DepthSurfaceId zStencil;

    void CreateTargets();
    void ReleaseTargets();

    SeaVertex *pVSea;

    uint32_t dwNumFrustumPlanes;

    int32_t iSeaTrashTexture;
    float fLastTrashTime;
    std::vector<SeaTrash> aSeaTrash;
    std::vector<RS_RECT> aTrashRects;

    int32_t iSeaLightTexture;
    std::vector<SeaLight> aSeaLights;
    float fLastLightTime;
    std::vector<RS_RECT> aLightsRects;

    // The flat grid in the buffers (GPU surface): the blocks it was built from (iTX, iTY, iSize, iLOD each), its tree's
    // place and step. Rebuilt only when one of them changes (BuildMesh); the CPU surface rewrites the buffers.
    std::vector<int32_t> aMeshBlocks;
    glm::vec3 vMeshTree{};
    float fMeshGridStep = 0.0f;
    bool bMeshBuilt = false;
    // The wave field (ocean_driver.hpp: made on the worker) and what the GPU reads of it this frame: the samples for
    // the vertex shader (sea_surface_gpu.hpp, D3DVERTEXTEXTURESAMPLER0) and the slopes for the pixel shader (a volume,
    // a slice per cascade, stage 0), made for `textureCascades_` x `textureSize_`.
    storm::sea::ocean::Driver driver_;
    storm::sea::ocean::Sea2Keys keys_{};
    int32_t fieldTexture_ = -1, slopeTexture_ = -1;
    int textureCascades_ = 0, textureSize_ = 0;
    std::vector<uint16_t> slopeHalves_;
    void UpdateField(float dt);
    void UploadField();
    float WindDirection() const;

    // Where the sea's vertices are displaced. Checking: on the CPU until the GPU surface self-check (sea_surface_check.hpp)
    // has compared the shader's surface with Field::Point on this device; Gpu after it passed; Cpu when it failed or
    // could not run (logged as an error: the fallback costs game-thread time).
    enum class Surface
    {
        Checking,
        Gpu,
        Cpu,
    };
    Surface surface_ = Surface::Checking;
    uint32_t surfaceCheck_ = 0;        // the check in flight (its ticket), 0 none
    uint32_t surfaceCheckFrames_ = 0;  // frames it has been in flight
    uint32_t surfaceCheckLosses_ = 0;  // checks lost with the device and started again
    static constexpr uint32_t kSurfaceCheckFrames = 600;
    static constexpr uint32_t kSurfaceCheckLosses = 3;
    void UpdateSurfaceCheck();
    void SurfaceCheckFailed(const std::string &why);
    // The check's own field (a fixed sea of the layout in use, uploaded once per check) and its texture.
    std::unique_ptr<storm::sea::ocean::Field> checkField_;
    int32_t checkTexture_ = -1;
    storm::ParallelFor meshWorkers_; // the CPU surface's blocks (BuildCpuMesh)

    glm::vec4 v4SeaColor, v4SkyColor, v4SeaParameters;

    float fFrenel;

    bool bStop;
    // The hulls the Execute stage put on the water, handed to the renderer's wakes when the sea draws.
    std::vector<storm::render::EffectsWaveSource> waveSources_;
    storm::sea::WaveFocus waveFocus_;

    float fFogSeaDensity, fFogStartDistance;
    glm::vec3 vFogColor;
    bool bFogEnable;

    bool bTempFullMode;
    float fTempGridStep, fTempLodScale;

    bool bIniFoamEnable, bFoamEnable;
    float fFoamV, fFoamK, fFoamUV, fFoamTextureDisturb;
    int32_t iFoamTexture;

    bool HeightField(SeaHeightField &field) const override;
    float WaveXZ(float x, float z, glm::vec3 *pNormal = nullptr) override;
    void AddWaveSource(const storm::render::EffectsWaveSource &source) override;

    void SetBlock(uint32_t dwBlockIndex);

    void DisplaceBlock(const SeaBlock &block);
    void WriteBlockVertices(const SeaBlock &block, SeaVertex *pVertices) const;
    bool BuildMesh();
    void BuildCpuMesh();
    glm::dvec2 MeshOrigin() const;

    bool SunRoad_Render();
    bool EnvMap_Render();

    // Environment and sun-road cube faces drawn this frame (bit per D3DCUBEMAP_FACES; cube_schedule.hpp).
    storm::sea::CubeSchedule cubeSchedule_;
    std::vector<storm::sea::CubeSchedule::Object> cubeObjects_;
    uint32_t cubeFaces_ = 0;
    std::chrono::steady_clock::time_point cubeTime_{};
    bool cubeSeaInView_ = true; // the last frame drew sea blocks
    void EnvMap_GetSideMatrix(D3DCUBEMAP_FACES Face, glm::mat4 &mView);

    void SFLB_CreateBuffers();
    void CreateVertexDeclaration();

  public:
    SEA();
    ~SEA() override;

    bool Init() override;
    void Realize(uint32_t dwDeltaTime);

    float Trace(const glm::vec3 &vSrc, const glm::vec3 &vDst) override;
    float Cannon_Trace(int32_t iBallOwner, int32_t iCannonType, const glm::vec3 &src, const glm::vec3 &dst) override;

    bool Clip(const PLANE *planes, int32_t nplanes, const glm::vec3 &center, float radius, ADD_POLYGON_FUNC addpoly) override
    {
        return false;
    }

    const char *GetCollideMaterialName() override
    {
        return nullptr;
    };

    bool GetCollideTriangle(TRIANGLE &triangle) override
    {
        return false;
    }

    bool EditMode_Update();

    uint32_t AttributeChanged(ATTRIBUTES *pAttribute) override;

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
        case Stage::loading_realize:
            Realize(0);
            break;
        case Stage::realize:
            Realize(delta);
            break;
        case Stage::lost_render:
            LostRender();
            break;
        case Stage::restore_render:
            RestoreRender();
            break;
        }
    }

    void LostRender();
    void RestoreRender();
};
