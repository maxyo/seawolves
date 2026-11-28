#include "loading_transaction.hpp"
#include "track.h"
#include "entity.h"
#include "core.h"
#include "math_inlines.h"

#include <algorithm>
#include <cfloat>

namespace
{
// How far a strip's edges wander (storm::ship::TrackFoamOffset) and the second ribbon's own pattern.
constexpr float kWobble1 = 0.18f, kWobble2 = 0.12f, kSeed2 = 2.3f;
} // namespace

VRenderer *ShipTracks::ShipTrack::pRS = nullptr;
SEA_BASE *ShipTracks::ShipTrack::pSea = nullptr;
int32_t ShipTracks::ShipTrack::iRefCount = 0;
uint32_t ShipTracks::ShipTrack::dwMaxBufferSize1 = 0, ShipTracks::ShipTrack::dwMaxBufferSize2 = 0;
int32_t ShipTracks::ShipTrack::iVTmpBuffer1 = -1, ShipTracks::ShipTrack::iVTmpBuffer2 = -1;
int32_t ShipTracks::ShipTrack::iITmpBuffer1 = -1, ShipTracks::ShipTrack::iITmpBuffer2 = -1;

ShipTracks::~ShipTracks()
{
    for (const auto &ship : aShips)
        delete ship;
}

bool ShipTracks::Init()
{
    entid_t sea_id;

    ShipTrack::pRS = static_cast<VRenderer *>(core.GetService("renderer"));
    Assert(ShipTrack::pRS);
    if (sea_id = core.GetEntityId("sea"))
        ShipTrack::pSea = static_cast<SEA_BASE *>(core.GetEntityPointer(sea_id));
    return true;
}

void ShipTracks::ResetTrack(SHIP_BASE *pShip)
{
    for (int32_t i = 0; i < aShips.size(); i++)
        if (aShips[i]->pShip == pShip)
        {
            aShips[i]->Reset();
            break;
        }
}

void ShipTracks::AddShip(SHIP_BASE *pShip)
{
    auto *pST = new ShipTrack;

    if (pST->Update(pShip))
        aShips.push_back(pST);
    else
        delete pST;
}

void ShipTracks::DelShip(SHIP_BASE *pShip)
{
    for (int32_t i = 0; i < aShips.size(); i++)
        if (aShips[i]->pShip == pShip)
        {
            STORM_DELETE(aShips[i]);
            aShips.erase(aShips.begin() + i);
            break;
        }
}

void ShipTracks::Execute(uint32_t dwDeltaTime)
{
}

void ShipTracks::Realize(uint32_t dwDeltaTime)
{
    const PLANE *planes = ShipTrack::pRS->GetPlanes();
    for (int32_t i = 0; i < aShips.size(); i++)
    {
        if (!storm::loading::active)
            aShips[i]->Execute(static_cast<float>(dwDeltaTime) * 0.001f);
        // A trail out of the view is aged but neither built nor drawn.
        if (!aShips[i]->InView(planes))
            continue;
        aShips[i]->PrepareVertices();
        aShips[i]->Realize(static_cast<float>(dwDeltaTime) * 0.001f);
    }
}

ShipTracks::ShipTrack::ShipTrack()
{
    bFirstExecute = true;
    iTrackTexture1 = -1;
    iTrackTexture2 = -1;
    iRefCount++;
}

ShipTracks::ShipTrack::~ShipTrack()
{
    iRefCount--;
    if (iRefCount <= 0)
    {
        iRefCount = 0;

        pRS->ReleaseVertexBuffer(iVTmpBuffer1);
        iVTmpBuffer1 = -1;
        pRS->ReleaseVertexBuffer(iVTmpBuffer2);
        iVTmpBuffer2 = -1;

        pRS->ReleaseIndexBuffer(iITmpBuffer1);
        iITmpBuffer1 = -1;
        pRS->ReleaseIndexBuffer(iITmpBuffer2);
        iITmpBuffer2 = -1;

        dwMaxBufferSize1 = 0;
        dwMaxBufferSize2 = 0;
    }

    pRS->TextureRelease(iTrackTexture1);
    iTrackTexture1 = -1;
    pRS->TextureRelease(iTrackTexture2);
    iTrackTexture2 = -1;
}

bool ShipTracks::ShipTrack::Update(SHIP_BASE *pShip)
{
    auto *pAChar = pShip->GetACharacter();
    Assert(pAChar);
    auto *pATrack = pAChar->FindAClass(pAChar, "Ship.Track");
    auto *pATrack1 = pAChar->FindAClass(pAChar, "Ship.Track1");
    auto *pATrack2 = pAChar->FindAClass(pAChar, "Ship.Track2");

    // A pattern of its own per ship, from its entity id: fixed while it sails.
    fSeed = static_cast<float>(static_cast<uint64_t>(pShip->GetId()) % 1000u) * 0.37f;

    if (!pATrack || !pATrack1 || !pATrack2)
        return false;
    if (!pATrack->GetAttributeAsDword("Enable", 0))
        return false;

    fTrackDistance = pATrack->GetAttributeAsFloat("TrackDistance");
    fUP1 = pATrack->GetAttributeAsFloat("WaveHeight1");
    fUP2 = pATrack->GetAttributeAsFloat("WaveHeight2");

    const std::string sTex1 = pATrack1->GetAttribute("Texture");
    fZStart1 = pATrack1->GetAttributeAsFloat("ZStart");
    fLifeTime1 = pATrack1->GetAttributeAsFloat("LifeTime");
    sscanf(pATrack1->GetAttribute("Width"), "%f, %f", &fWidth11, &fWidth12);
    sscanf(pATrack1->GetAttribute("Speed"), "%f, %f", &fSpeed11, &fSpeed12);
    fTrackStep1 = pATrack1->GetAttributeAsFloat("TrackWidthSteps");

    const std::string sTex2 = pATrack2->GetAttribute("Texture");
    fZStart2 = pATrack2->GetAttributeAsFloat("ZStart");
    fLifeTime2 = pATrack2->GetAttributeAsFloat("LifeTime");
    sscanf(pATrack2->GetAttribute("Width"), "%f, %f", &fWidth21, &fWidth22);
    sscanf(pATrack2->GetAttribute("Speed"), "%f, %f", &fSpeed21, &fSpeed22);
    fTrackStep2 = pATrack2->GetAttributeAsFloat("TrackWidthSteps");

    dwTrackStep1 = static_cast<int32_t>(fTrackStep1);
    dwTrackStep2 = static_cast<int32_t>(fTrackStep2);

    pRS->TextureRelease(iTrackTexture1);
    iTrackTexture1 = -1;
    pRS->TextureRelease(iTrackTexture2);
    iTrackTexture2 = -1;

    this->pShip = pShip;
    // TODO: check this

    if (this->pShip)
    {
        iTrackTexture1 = pRS->TextureCreate(sTex1.c_str());
        iTrackTexture2 = pRS->TextureCreate(sTex2.c_str());

        this->vLastPos = pShip->GetPos();
        this->vLastAng = pShip->GetAng();
        this->fCurTV = 0.0f;
    }

    aTrack1.clear();
    aTrack2.clear();
    aColumns1.clear();
    aColumns2.clear();
    trackSpacing.Reset();

    return true;
}

bool ShipTracks::ShipTrack::Reserve1(uint32_t dwSize)
{
    const auto dwNewSize = (dwSize / (100) + 1) * (100);

    if (dwMaxBufferSize1 >= dwNewSize)
        return iVTmpBuffer1 != -1 && iITmpBuffer1 != -1;

    dwMaxBufferSize1 = dwNewSize;

    pRS->ReleaseVertexBuffer(iVTmpBuffer1);
    iVTmpBuffer1 = -1;
    pRS->ReleaseIndexBuffer(iITmpBuffer1);
    iITmpBuffer1 = -1;

    iVTmpBuffer1 = pRS->CreateVertexBuffer(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1,
                                           dwMaxBufferSize1 * dwTrackStep1 * sizeof(TrackVertex),
                                           D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);
    iITmpBuffer1 = pRS->CreateIndexBuffer(dwMaxBufferSize1 * dwTrackStep1 * 6 * sizeof(uint16_t),
                                          D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);

    if (iVTmpBuffer1 == -1 || iITmpBuffer1 == -1)
    {
        dwMaxBufferSize1 = 0;
        pRS->ReleaseVertexBuffer(iVTmpBuffer1);
        iVTmpBuffer1 = -1;
        pRS->ReleaseIndexBuffer(iITmpBuffer1);
        iITmpBuffer1 = -1;
        return false;
    }

    auto *pI = static_cast<uint16_t *>(pRS->LockIndexBuffer(iITmpBuffer1));
    for (uint32_t y = 0; y < dwNewSize; y++)
        for (uint32_t x = 0; x < dwTrackStep1 - 1; x++)
        {
            *pI++ = static_cast<uint16_t>((y + 0) * dwTrackStep1 + x);
            *pI++ = static_cast<uint16_t>((y + 1) * dwTrackStep1 + x);
            *pI++ = static_cast<uint16_t>((y + 0) * dwTrackStep1 + x + 1);

            *pI++ = static_cast<uint16_t>((y + 1) * dwTrackStep1 + x);
            *pI++ = static_cast<uint16_t>((y + 1) * dwTrackStep1 + x + 1);
            *pI++ = static_cast<uint16_t>((y + 0) * dwTrackStep1 + x + 1);
        }
    pRS->UnLockIndexBuffer(iITmpBuffer1);

    return true;
}

bool ShipTracks::ShipTrack::Reserve2(uint32_t dwSize)
{
    const auto dwNewSize = (dwSize / (20) + 1) * (20);

    if (dwMaxBufferSize2 >= dwNewSize)
        return iVTmpBuffer2 != -1 && iITmpBuffer2 != -1;

    dwMaxBufferSize2 = dwNewSize;

    pRS->ReleaseVertexBuffer(iVTmpBuffer2);
    iVTmpBuffer2 = -1;
    pRS->ReleaseIndexBuffer(iITmpBuffer2);
    iITmpBuffer2 = -1;

    iVTmpBuffer2 = pRS->CreateVertexBuffer(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1,
                                           dwMaxBufferSize2 * dwTrackStep2 * sizeof(TrackVertex),
                                           D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);
    iITmpBuffer2 = pRS->CreateIndexBuffer(dwMaxBufferSize2 * dwTrackStep2 * 6 * sizeof(uint16_t), D3DUSAGE_WRITEONLY);

    if (iVTmpBuffer2 == -1 || iITmpBuffer2 == -1)
    {
        dwMaxBufferSize2 = 0;
        pRS->ReleaseVertexBuffer(iVTmpBuffer2);
        iVTmpBuffer2 = -1;
        pRS->ReleaseIndexBuffer(iITmpBuffer2);
        iITmpBuffer2 = -1;
        return false;
    }

    auto *pI = static_cast<uint16_t *>(pRS->LockIndexBuffer(iITmpBuffer2));
    for (uint32_t y = 0; y < dwNewSize; y++)
        for (uint32_t x = 0; x < dwTrackStep2 - 1; x++)
        {
            *pI++ = static_cast<uint16_t>((y + 0) * dwTrackStep2 + x);
            *pI++ = static_cast<uint16_t>((y + 1) * dwTrackStep2 + x);
            *pI++ = static_cast<uint16_t>((y + 0) * dwTrackStep2 + x + 1);

            *pI++ = static_cast<uint16_t>((y + 1) * dwTrackStep2 + x);
            *pI++ = static_cast<uint16_t>((y + 1) * dwTrackStep2 + x + 1);
            *pI++ = static_cast<uint16_t>((y + 0) * dwTrackStep2 + x + 1);
        }
    pRS->UnLockIndexBuffer(iITmpBuffer2);

    return true;
}

void ShipTracks::ShipTrack::Execute(float fDeltaTime)
{
    if (!pShip || !pSea)
        return;

    if (bFirstExecute)
    {
        vLastPos = pShip->GetPos();
        vLastAng = pShip->GetAng();
        fCurTV = 0.0f;
        bFirstExecute = false;
        trackSpacing.Reset();
    }

    const auto vCurPos = pShip->GetPos();
    const auto vCurAng = pShip->GetAng();

    const auto vBoxSize = pShip->GetBoxsize();

    auto vDist = vCurPos - vLastPos;
    vDist.y = 0.0;
    auto fCurrentDistance = sqrtf(storm::math::lengthSquared(vDist));
    if (fCurrentDistance > 100.0f)
    {
        fCurrentDistance = 0.0f;
        vLastPos = vCurPos;

        aTrack1.clear();
        aTrack2.clear();
        aColumns1.clear();
        aColumns2.clear();
        trackSpacing.Reset();
    }
    if (fCurrentDistance > 0.0f)
    {
        const auto fSpeed = Clamp(pShip->GetCurrentSpeed() / 20.0f);
        auto vDir = vCurPos - vLastPos;
        vDir.y = 0.0f;
        vDir = storm::math::normalizeOrZero(vDir);
        // Keep the unspent distance, and stamp each strip at its own pose/time along this frame's path.
        trackSpacing.Advance(fCurrentDistance, fTrackDistance, vLastAng.y, vCurAng.y,
            [&](float fDistance, float fraction, float yaw) {
                const glm::vec3 forward(sinf(yaw), 0.0f, cosf(yaw));

                aTrack1.insert(aTrack1.begin(), Track{});
                aTrack1[0].vPos = vLastPos + vDir * fDistance + forward * (vBoxSize.z * fZStart1);
                aTrack1[0].fCos = cosf(yaw);
                aTrack1[0].fSin = sinf(yaw);
                aTrack1[0].fTime = -fraction * fDeltaTime;
                aTrack1[0].fTV = fCurTV;
                aTrack1[0].fWidth = aTrack1[0].fInitialWidth = RRnd(fWidth11, fWidth12);          // 3.0f;
                aTrack1[0].fSpeed = fSpeed * RRnd(fSpeed11, fSpeed12); // 4.5f + (FRAND(1.0f) - 0.5f);
                aTrack1[0].fInitialAlpha = fSpeed;
                aColumns1.insert(aColumns1.begin(), dwTrackStep1, {});
                storm::ship::TrackFoamColumns(dwTrackStep1, fCurTV, fSeed, kWobble1, aColumns1.begin());

                aTrack2.insert(aTrack2.begin(), Track{});
                aTrack2[0].vPos = vLastPos + vDir * fDistance + forward * (vBoxSize.z * fZStart2);
                aTrack2[0].fCos = cosf(yaw);
                aTrack2[0].fSin = sinf(yaw);
                aTrack2[0].fTime = -fraction * fDeltaTime;
                aTrack2[0].fTV = fCurTV;
                aTrack2[0].fWidth = aTrack2[0].fInitialWidth = RRnd(fWidth21, fWidth22);          // 12.0f;
                aTrack2[0].fSpeed = fSpeed * RRnd(fSpeed21, fSpeed22); // 1.5f + (FRAND(0.4f) - 0.2f);
                aTrack2[0].fInitialAlpha = fSpeed;
                aColumns2.insert(aColumns2.begin(), dwTrackStep2, {});
                storm::ship::TrackFoamColumns(dwTrackStep2, fCurTV, fSeed + kSeed2, kWobble2, aColumns2.begin());

                fCurTV += 0.05f;
            });
    }
    vLastPos = vCurPos;
    vLastAng = vCurAng;

    for (int32_t i = 0; i < aTrack1.size(); i++)
    {
        auto &T = aTrack1[i];

        T.fTime += fDeltaTime;
        T.fAlpha = T.fInitialAlpha * Clamp(1.0f - T.fTime / fLifeTime1); // 22.0f);
        T.fWidth = storm::ship::TrackWidth(T.fInitialWidth, T.fSpeed, T.fTime, fLifeTime1);

        if (T.fTime >= fLifeTime1)
        {
            aColumns1.erase(aColumns1.begin() + i * dwTrackStep1, aColumns1.begin() + (i + 1) * dwTrackStep1);
            aTrack1.erase(aTrack1.begin() + i);
            i--;
        }
    }

    for (int32_t i = 0; i < aTrack2.size(); i++)
    {
        auto &T = aTrack2[i];

        T.fTime += fDeltaTime;
        T.fAlpha = T.fInitialAlpha * Clamp(1.0f - T.fTime / fLifeTime2); // 10.0f);
        T.fWidth = storm::ship::TrackWidth(T.fInitialWidth, -T.fSpeed, T.fTime, fLifeTime2);

        if (T.fTime >= fLifeTime2)
        {
            aColumns2.erase(aColumns2.begin() + i * dwTrackStep2, aColumns2.begin() + (i + 1) * dwTrackStep2);
            aTrack2.erase(aTrack2.begin() + i);
            i--;
        }
    }

}

bool ShipTracks::ShipTrack::InView(const PLANE *planes)
{
    if (!pShip || !pSea)
        return false;
    SeaHeightField field;
    if (!pSea->HeightField(field))
    {
        if (!bNoHeightFieldLogged)
            core.Trace("ERROR: ship tracks: the sea reports no height field: the trails are drawn without a view test");
        bNoHeightFieldLogged = true;
        return true;
    }
    float fFov;
    glm::vec3 vCamPos, vCamAng;
    pRS->GetCamera(vCamPos, vCamAng, fFov);
    const auto fWaveUP = storm::ship::TrackElevation(vCamPos, pShip->GetPos(), fUP1, fUP2);
    // The heights PrepareVertices gives: the lift (0.4..1.4 of it across the first ribbon) plus the sea's height,
    // within the field's bounds this frame.
    const float lift[3] = {0.4f * fWaveUP, fWaveUP, 1.4f * fWaveUP};
    glm::vec3 lo(FLT_MAX, *std::min_element(lift, lift + 3) + field.lowest, FLT_MAX);
    glm::vec3 hi(-FLT_MAX, *std::max_element(lift, lift + 3) + field.highest, -FLT_MAX);
    const auto strips = [&](const std::vector<Track> &track, float wobble) {
        if (track.size() < 2)
            return;
        for (const Track &T : track)
        {
            const float reach = T.fWidth * 0.5f * (1.0f + wobble); // the farthest TrackFoamOffset
            lo.x = std::min(lo.x, T.vPos.x - reach);
            lo.z = std::min(lo.z, T.vPos.z - reach);
            hi.x = std::max(hi.x, T.vPos.x + reach);
            hi.z = std::max(hi.z, T.vPos.z + reach);
        }
    };
    strips(aTrack1, kWobble1);
    strips(aTrack2, kWobble2);
    return lo.x <= hi.x && !storm::ship::TrackBoxOutside(planes, 4, lo, hi);
}

void ShipTracks::ShipTrack::PrepareVertices()
{
    if (!pShip || !pSea) return;
    float fFov;
    glm::vec3 vCamPos, vCamAng;
    pRS->GetCamera(vCamPos, vCamAng, fFov);
    const auto vCurPos = pShip->GetPos();
    const auto fWaveUP = storm::ship::TrackElevation(vCamPos, vCurPos, fUP1, fUP2);
    if (Reserve1(aTrack1.size()))
        if (aTrack1.size() > 1)
        {
            auto *pV = static_cast<TrackVertex *>(pRS->LockVertexBuffer(iVTmpBuffer1, D3DLOCK_DISCARD));
            for (int32_t i = 0; i < aTrack1.size(); i++)
            {
                auto &T = aTrack1[i];
                const auto *columns = &aColumns1[i * dwTrackStep1];
                const float age = T.fTime / fLifeTime1;
                for (uint32_t x = 0; x < dwTrackStep1; x++)
                {
                    const auto k = static_cast<float>(x) / (fTrackStep1 - 1.0f);
                    auto px = T.fWidth * columns[x].offset;
                    auto pz = 0.0f;
                    RotateAroundY(px, pz, T.fCos, T.fSin);
                    px += T.vPos.x;
                    pz += T.vPos.z;
                    auto &v = pV[i * dwTrackStep1 + x];
                    v.vPos = glm::vec3(px, fWaveUP * (1.4f - fabsf((k * 2.0f) - 1.0f)) + pSea->WaveXZ(px, pz), pz) - vCurPos;
                    v.tu = k;
                    v.tv = T.fTV;
                    v.dwColor = ARGB(T.fAlpha * storm::ship::TrackFoamKeep(columns[x].pattern, age) * 255.0f, 0, 0, 0);
                }
            }
            pRS->UnLockVertexBuffer(iVTmpBuffer1);
        }

    if (Reserve2(aTrack2.size()))
        if (aTrack2.size() > 1)
        {
            auto *pV = static_cast<TrackVertex *>(pRS->LockVertexBuffer(iVTmpBuffer2, D3DLOCK_DISCARD));
            for (int32_t i = 0; i < aTrack2.size(); i++)
            {
                auto &T = aTrack2[i];
                const auto *columns = &aColumns2[i * dwTrackStep2];
                const float age = T.fTime / fLifeTime2;
                for (uint32_t x = 0; x < dwTrackStep2; x++)
                {
                    auto px = T.fWidth * columns[x].offset;
                    auto pz = 0.0f;
                    RotateAroundY(px, pz, T.fCos, T.fSin);
                    px += T.vPos.x;
                    pz += T.vPos.z;
                    auto &v = pV[i * dwTrackStep2 + x];
                    v.vPos = glm::vec3(px, fWaveUP + pSea->WaveXZ(px, pz), pz) - vCurPos;
                    v.tu = static_cast<float>(x) / (fTrackStep2 - 1.0f);
                    v.tv = T.fTV * 6.0f;
                    v.dwColor = ARGB(T.fAlpha * storm::ship::TrackFoamKeep(columns[x].pattern, age) * 255.0f, 0, 0, 0);
                }
            }
            pRS->UnLockVertexBuffer(iVTmpBuffer2);
        }
}

void ShipTracks::ShipTrack::Reset()
{
    bFirstExecute = true;
    trackSpacing.Reset();

    aTrack1.clear();
    aTrack2.clear();
    aColumns1.clear();
    aColumns2.clear();
}

uint32_t ShipTracks::AttributeChanged(ATTRIBUTES *pA)
{
    if (*pA == "ResetTracks")
    {
        for (int32_t i = 0; i < aShips.size(); i++)
        {
            aShips[i]->Reset();
        }
    }
    return 0;
}

void ShipTracks::ShipTrack::Realize(float fDeltaTime)
{
    if (!pShip || !pSea)
        return;

    glm::mat4 m;
    storm::math::pos(m) = pShip->GetPos();
    pRS->SetTransform(D3DTS_WORLD, storm::math::d3dMatrix(m));

    if (aTrack1.size() > 1)
    {
        pRS->TextureSet(0, iTrackTexture1);
        pRS->DrawBuffer(iVTmpBuffer1, sizeof(TrackVertex), iITmpBuffer1, 0, aTrack1.size() * dwTrackStep1, 0,
                        (dwTrackStep1 - 1) * (aTrack1.size() - 1) * 2, "ShipTrack");
    }

    if (aTrack2.size() > 1)
    {
        pRS->TextureSet(0, iTrackTexture2);
        pRS->DrawBuffer(iVTmpBuffer2, sizeof(TrackVertex), iITmpBuffer2, 0, aTrack2.size() * dwTrackStep2, 0,
                        (dwTrackStep2 - 1) * (aTrack2.size() - 1) * 2, "ShipTrack");
    }
}
