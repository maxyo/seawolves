#include "loading_transaction.hpp"
#include "core.h"
#include <storm/render/far_lod.hpp>

#include "entity.h"
#include "math_inlines.h"
#include "sea.h"
#include "shared/messages.h"

#include "shared/layers.h"

void SEA::EnvMap_GetSideMatrix(D3DCUBEMAP_FACES Face, glm::mat4 &mView)
{
    glm::vec3 vLookTo, vUp;

    switch (Face)
    {
    case D3DCUBEMAP_FACE_POSITIVE_X:
        vLookTo = glm::vec3(1.0f, 0.0f, 0.0f);
        vUp = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case D3DCUBEMAP_FACE_NEGATIVE_X:
        vLookTo = glm::vec3(-1.0f, 0.0f, 0.0f);
        vUp = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case D3DCUBEMAP_FACE_POSITIVE_Y:
        vLookTo = glm::vec3(0.0f, 1.0f, 0.0f);
        vUp = glm::vec3(0.0f, 0.0f, -1.0f);
        break;
    case D3DCUBEMAP_FACE_NEGATIVE_Y:
        vLookTo = glm::vec3(0.0f, -1.0f, 0.0f);
        vUp = glm::vec3(0.0f, 0.0f, 1.0f);
        break;
    case D3DCUBEMAP_FACE_POSITIVE_Z:
        vLookTo = glm::vec3(0.0f, 0.0f, 1.0f);
        vUp = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case D3DCUBEMAP_FACE_NEGATIVE_Z:
        vLookTo = glm::vec3(0.0f, 0.0f, -1.0f);
        vUp = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    }

    storm::math::viewMatrix(mView, glm::vec3(0.0f), vLookTo, vUp);
}

bool SEA::SunRoad_Render()
{
    namespace far_lod = storm::render::far_lod;
    struct PassScope // the far LODs choose for this target (storm/render/far_lod.hpp)
    {
        PassScope() { far_lod::pass = far_lod::CubeFace; }
        ~PassScope() { far_lod::pass = far_lod::Main; }
    } pass_scope;
    glm::mat4 mOldView = rs->GetView();
    glm::mat4 mOldProjection = rs->GetProjection();

    rs->EndScene();
    rs->PushRenderTarget();

    rs->SetProjection(storm::math::projectionMatrix(PI / 2.0f, 256.0f, 256.0f, 1.0f, 4000.0f));

    uint32_t Colors[6] = {0xd934c8, 0x2FFF1F, 0x0000FF, 0xFF00, 0xb28e11, 0x0};
    for (uint32_t i = 0; i < 6; i++)
    {
        if ((!bUnderSea && i == D3DCUBEMAP_FACE_NEGATIVE_Y) || !(cubeFaces_ & (1u << i)))
            continue;

        rs->BeginScene();
        bool bCool = rs->SetRenderTarget(sunRoadMap, 0, i, zStencil);
        bCool = D3D_OK != rs->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, 0x0, 1.0f, 0);

        glm::mat4 mView;
        EnvMap_GetSideMatrix(static_cast<D3DCUBEMAP_FACES>(i), mView);

        mView[3][0] = -(mView[0][0] * vCamPos.x - mView[1][0] * vCamPos.y + mView[2][0] * vCamPos.z);
        mView[3][1] = -(mView[0][1] * vCamPos.x - mView[1][1] * vCamPos.y + mView[2][1] * vCamPos.z);
        mView[3][2] = -(mView[0][2] * vCamPos.x - mView[1][2] * vCamPos.y + mView[2][2] * vCamPos.z);

        rs->SetView(mView);

        // Render scene here.
        if (!core.IsLayerFrozen(SEA_REFLECTION2))
        {
            auto &&entities = core.GetEntityIds(SEA_SUNROAD);
            for (auto ent_id : entities)
            {
                const auto hash = core.GetClassCode(ent_id);
                if (hash != dwShipCode && hash != dwSailCode && hash != dwIslandCode)
                {
                    core.Send_Message(ent_id, "l", MSG_SEA_SUNROAD_DRAW);
                }
            }
        }

        rs->EndScene();
    }

    rs->PopRenderTarget();
    rs->SetView(mOldView);
    rs->SetProjection(mOldProjection);

    rs->BeginScene();
    mOldView = rs->GetView();

    return true;
}

bool SEA::EnvMap_Render()
{
    namespace far_lod = storm::render::far_lod;
    struct PassScope // the far LODs choose for this target (storm/render/far_lod.hpp)
    {
        PassScope() { far_lod::pass = far_lod::CubeFace; }
        ~PassScope() { far_lod::pass = far_lod::Main; }
    } pass_scope;
    glm::mat4 mOldView = rs->GetView();
    glm::mat4 mOldProjection = rs->GetProjection();
    rs->EndScene();
    rs->PushRenderTarget();

    rs->SetProjection(storm::math::projectionMatrix(PI / 2.0f, 256.0f, 256.0f, 1.0f, 4000.0f));

    uint32_t Colors[6] = {0xd934c8, 0x2FFF1F, 0x0000FF, 0xFF00, 0xb28e11, 0x0};
    for (uint32_t i = 0; i < 6; i++)
    {
        if ((!bUnderSea && i == D3DCUBEMAP_FACE_NEGATIVE_Y) || !(cubeFaces_ & (1u << i)))
            continue;

        rs->BeginScene();
        bool bCool = rs->SetRenderTarget(envMap, 0, i, zStencil);
        bCool = D3D_OK != rs->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, Colors[i] /*0x0*/, 1.0f, 0);

        glm::mat4 mView;
        EnvMap_GetSideMatrix(static_cast<D3DCUBEMAP_FACES>(i), mView);

        mView[3][0] = -(mView[0][0] * vCamPos.x - mView[1][0] * vCamPos.y + mView[2][0] * vCamPos.z);
        mView[3][1] = -(mView[0][1] * vCamPos.x - mView[1][1] * vCamPos.y + mView[2][1] * vCamPos.z);
        mView[3][2] = -(mView[0][2] * vCamPos.x - mView[1][2] * vCamPos.y + mView[2][2] * vCamPos.z);

        rs->SetView(mView);

        // Render scene here.
        {
            auto &&entities = core.GetEntityIds(SEA_REFLECTION);
            for (auto ent_id : entities)
            {
                core.Send_Message(ent_id, "l", MSG_SEA_REFLECTION_DRAW);
            }
        }
        {
            auto &&entities = core.GetEntityIds(SEA_REFLECTION2);
            for (auto ent_id : entities)
            {
                core.Send_Message(ent_id, "l", MSG_SEA_REFLECTION_DRAW);
            }
        }

        rs->EndScene();
    }

    rs->PopRenderTarget();
    rs->SetView(mOldView);
    rs->SetProjection(mOldProjection);

    rs->BeginScene();

    return true;
}
