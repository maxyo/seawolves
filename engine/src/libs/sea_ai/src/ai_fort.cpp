#include "ai_fort.h"
#include "sea_script_protocol.hpp"
#include "shared/messages.h"

#include <spdlog/spdlog.h>

AIFort *AIFort::pAIFort = nullptr;

AIFort::AIFort()
{
    dtFiredTimer.Setup(FRAND(1.0f), 1.0f);
    pLastTraceFort = nullptr;
    pShipsLights = nullptr;
    pAIFort = this;
}

AIFort::~AIFort()
{
    pAIFort = nullptr;
    for (auto &aFort : aForts)
        STORM_DELETE(aFort);
}

bool AIFort::Init()
{
    return true;
}


void AIFort::Execute(uint32_t Delta_Time)
{
    fMinCannonDamageDistance = AttributesPointer->GetAttributeAsFloat("MinCannonDamageDistance");

    const auto fDeltaTime = static_cast<float>(Delta_Time) * 0.001f;
    if (!aForts.size())
        return;

    const auto bFiredTimer = dtFiredTimer.Update(fDeltaTime);

    for (uint32_t k = 0; k < aForts.size(); k++)
    {
        auto *pF = aForts[k];
        if (pF->isDead())
            continue;
        if (!pF->isNormalMode())
            continue;

        auto fSpeedV0 = 0.0f;
        auto dwCurrentCannonType = 0xFFFFFFFF;
        const char *currentKey = nullptr;
        const auto iMax = pF->GetAllCannonsNum(); // boal fix
        for (uint32_t i = 0; i < iMax; i++)
        {
            auto *pC = pF->GetCannon(i);

            const auto dwNewCurrentCannonType = pF->GetCannonType(i);

            // update cannons parameters
            if (dwCurrentCannonType != dwNewCurrentCannonType)
            {
                dwCurrentCannonType = dwNewCurrentCannonType;
                auto *pACannons = pF->GetACharacter()->FindAClass(pF->GetACharacter(), "Ship.Cannons");
                if (!pACannons)
                    pACannons = pF->GetACharacter()->CreateSubAClass(pF->GetACharacter(), "Ship.Cannons");
                Assert(pACannons);
                pACannons->SetAttributeUseDword("type", dwCurrentCannonType);
                core.Event(CANNON_RECALCULATE_PARAMETERS, "l", GetIndex(pF->GetACharacter()));
            }
            if (const char *key = pF->GetCannonKey(i); key != currentKey)
            {
                currentKey = key;
                fSpeedV0 = storm::sea_ai::ReadBoardBallistics(pF->GetACharacter(), key).speedV0;
            }

            if (!pC->isFired() && pC->isReady2Fire() && bFiredTimer)
            {
                AIShip *pFireAIShip = nullptr;
                auto fMinDistance = 1e10f;
                auto vCPos = pC->GetPos();
                const auto fMaxFireDistance = AICannon::CalcMaxFireDistance(vCPos.y, fSpeedV0, 0.35f); // FIX-ME
                for (auto &j : AIShip::AIShips)
                    if (!j->isDead() &&
                        Helper.isEnemy(pF->GetACharacter(), j->GetACharacter()))
                    {
                        const auto fDistance = j->GetDistance(vCPos);
                        if (fDistance > fMaxFireDistance)
                            continue;
                        if (fDistance < fMinDistance)
                        {
                            fMinDistance = fDistance;
                            pFireAIShip = j;
                        }
                    }
                if (pFireAIShip)
                    pC->Fire(fSpeedV0, pFireAIShip->GetPos(), true);
            }
            pC->Execute(fDeltaTime);
        }
    }
}

void AIFort::Realize(uint32_t Delta_Time)
{


}

bool AIFort::AddFort(ATTRIBUTES *pIslandAP, ATTRIBUTES *pFortLabelAP, ATTRIBUTES *pFortCharacter, entid_t eidModel,
                     entid_t eidBlot)
{
    Assert(pFortLabelAP);

    auto *pFortAP = pFortLabelAP->FindAClass(pFortLabelAP, "fort");
    Assert(pFortAP);
    auto *const pModelAP = pFortAP->FindAClass(pFortAP, "model");
    Assert(pModelAP);
    auto *const pLocatorsAP = pFortAP->FindAClass(pFortAP, "locators");
    Assert(pLocatorsAP);
    auto *const pModelsDirAP = pIslandAP->FindAClass(pIslandAP, "filespath.models");
    Assert(pModelsDirAP);

    const char *pModelName = pModelAP->GetThisAttr();
    Assert(pModelName);
    const char *pLocatorsName = pLocatorsAP->GetThisAttr();
    Assert(pLocatorsName);
    const char *pModelsDir = pModelsDirAP->GetThisAttr();
    Assert(pModelsDir);

    auto *pFort = new AI_FORT(pFortLabelAP);
    pFort->SetACharacter(pFortCharacter);
    pFort->SetModelEID(eidModel);
    pFort->SetBlotEID(eidBlot);
    aForts.push_back(pFort);

    auto *pACannonsType1 = pFortCharacter->FindAClass(pFortCharacter, "Fort.Cannons.Type.1");
    Assert(pACannonsType1);
    auto *pACannonsType2 = pFortCharacter->FindAClass(pFortCharacter, "Fort.Cannons.Type.2");
    Assert(pACannonsType2);
    auto *pACannonsType3 = pFortCharacter->FindAClass(pFortCharacter, "Fort.Cannons.Type.3");
    Assert(pACannonsType3);
    pFort->dwCannonType = pACannonsType1->GetAttributeAsDword();
    pFort->dwCulverinType = pACannonsType2->GetAttributeAsDword();
    pFort->dwMortarType = pACannonsType3->GetAttributeAsDword();

    pFort->mOldMatrix = pFort->GetModel()->mtx;

    ScanFortForCannons(pFort, pModelsDir, pLocatorsName);

    auto *pALights = pFortCharacter->FindAClass(GetACharacter(), "ship.lights");
    auto *pAFlares = pFortCharacter->FindAClass(GetACharacter(), "ship.flares");

    const auto bLights = (pALights) ? pALights->GetAttributeAsDword() != 0 : false;
    const auto bFlares = (pAFlares) ? pAFlares->GetAttributeAsDword() != 0 : false;

    const auto eidTmp = core.GetEntityId("shiplights");
    pShipsLights = static_cast<IShipLights *>(core.GetEntityPointer(eidTmp));
    Assert(pShipsLights);

    pShipsLights->AddLights(&pFort->tmpObject, pFort->GetModel(), bLights, bFlares);
    pShipsLights->ProcessStage(Stage::execute, 0);

    core.Event(FORT_CREATE, "al", pFortCharacter, pFort->GetAllCannonsNum());

    return true;
}

void AIFort::AddFortHit(int32_t iCharacterIndex, glm::vec3 &vHitPos, int32_t iCannonType)
{
    const auto &protocol = storm::sea_ai::seaScriptProtocol();
    uint32_t i, j, iMax;
    for (i = 0; i < aForts.size(); i++)
    {
        auto *pF = aForts[i];

        iMax = pF->GetAllCannonsNum(); // boal fix
        // Fort_Damage goes to every fort, the hit one or not, as the game's scripts expect.
        if (protocol.fort_damage_event)
        {
            const int32_t intact = static_cast<int32_t>(iMax - pF->GetDamagedCannonsNum());
            auto *pVData = core.Event("Fort_Damage", "lllfffl", iCharacterIndex, GetIndex(pF->GetACharacter()), intact,
                                      vHitPos.x, vHitPos.y, vHitPos.z, iCannonType);
            Assert(pVData);
            for (int32_t destroyed = pVData->GetInt(); destroyed > 0; destroyed--)
                DestroyRandomCannon(i, iCharacterIndex);
        }
        for (j = 0; j < iMax; j++)
        {
            auto *pC = pF->GetCannon(j);

            if (pC->isDamaged())
                continue;

            auto vPos = pC->GetPos();
            const auto fDistance = sqrtf(storm::math::lengthSquared(vPos - vHitPos));
            if (fDistance > fMinCannonDamageDistance)
                continue;

            MESSAGE event;
            storm::sea_ai::ResetFortCannonDamage(event, protocol,
                                                 {iCharacterIndex, GetIndex(pF->GetACharacter()), pF->pFortLabelAP,
                                                  static_cast<int32_t>(iMax),
                                                  static_cast<int32_t>(pF->GetDamagedCannonsNum()), vPos, fDistance,
                                                  pC->GetDamage()},
                                                 iCannonType);
            auto *pVData = core.Event(FORT_CANNON_DAMAGE, event);
            Assert(pVData);

            pC->SetDamage(pVData->GetFloat());
        }
    }
}

AIFort::AI_FORT *AIFort::FindFort(entid_t eidModel)
{
    for (auto &aFort : aForts)
    {
        if (core.GetEntityPointer(aFort->GetModelEID()) == core.GetEntityPointer(eidModel))
            return aFort;
    }
    return nullptr;
}

uint64_t AIFort::ProcessMessage(MESSAGE &message)
{
    glm::vec3 vHit;
    int32_t iCharacterIndex;
    entid_t eidFortModel, eidBlot;
    ATTRIBUTES *pFortAPLabel, *pCharacter, *pIslandAP;
    AI_FORT *pFort = nullptr;

    switch (message.Long())
    {
    case AI_MESSAGE_FORT_HIT: {
        iCharacterIndex = message.Long();
        vHit.x = message.Float();
        vHit.y = message.Float();
        vHit.z = message.Float();
        // Scripts whose ball events carry the ball's cannon type pass it on ("llfffl"); the others' fort events have
        // no place for it.
        const int32_t iCannonType = message.GetFormat() == "llfffl" ? message.Long() : -1;
        AddFortHit(iCharacterIndex, vHit, iCannonType);
    }
    break;
    case AI_MESSAGE_ADD_FORT:
        pIslandAP = message.AttributePointer();
        pFortAPLabel = message.AttributePointer();
        pCharacter = message.AttributePointer();
        eidFortModel = message.EntityID();
        eidBlot = message.EntityID();
        AddFort(pIslandAP, pFortAPLabel, pCharacter, eidFortModel, eidBlot);
        Helper.AddCharacter(pCharacter, pCharacter);
        break;
    case AI_MESSAGE_FORT_SET_LIGHTS:
        pFort = FindFort(message.EntityID());
        if (pShipsLights && pFort)
            pShipsLights->SetLights(&pFort->tmpObject);
        break;
    case AI_MESSAGE_FORT_UNSET_LIGHTS:
        pFort = FindFort(message.EntityID());
        if (pShipsLights && pFort)
            pShipsLights->UnSetLights(&pFort->tmpObject);
        break;
    }
    return 0;
}

uint32_t AIFort::AttributeChanged(ATTRIBUTES *pAttribute)
{
    return 0;
}

bool AIFort::Mount(ATTRIBUTES *pAttribute)
{
    return true;
}

void AIFort::DestroyRandomCannon(uint32_t dwFortIndex, int32_t iBallOwner)
{
    auto *pF = aForts[dwFortIndex];
    const uint32_t all = pF->GetAllCannonsNum();
    const uint32_t damaged = pF->GetDamagedCannonsNum();
    if (damaged == all)
    {
        spdlog::warn("AIFort: Fort_Damage destroys a cannon of fort {}, which has none left",
                     GetIndex(pF->GetACharacter()));
        return;
    }
    uint32_t pick = static_cast<uint32_t>(rand()) % (all - damaged);
    for (uint32_t i = 0; i < all; i++)
    {
        auto *pC = pF->GetCannon(i);
        if (pC->isDamaged() || pick-- != 0)
            continue;
        const glm::vec3 vPos = pC->GetPos();
        core.Event("Fort_CannonDestroy", "lllfff", iBallOwner, GetIndex(pF->GetACharacter()),
                   static_cast<int32_t>(damaged), vPos.x, vPos.y, vPos.z);
        pC->SetDamage(1.0f);
        return;
    }
}

bool AIFort::ScanFortForCannons(AI_FORT *pFort, const char *pModelsDir, const char *pLocatorsName) const
{
    entid_t model_id;
    GEOS::LABEL label;
    GEOS::INFO info;
    NODE *pNode;

    const auto path = std::filesystem::path() / pModelsDir / pLocatorsName;
    const auto pathStr = path.string();
    // MessageBoxA(NULL, (LPCSTR)path.c_str(), "", MB_OK); //~!~
    model_id = core.CreateEntity("MODELR");
    core.Send_Message(model_id, "ls", MSG_MODEL_LOAD_GEO, (char *)pathStr.c_str());

    auto *pModel = static_cast<MODEL *>(core.GetEntityPointer(model_id));
    Assert(pModel);

    // search and add cannons & culverins
    uint32_t dwIdx = 0;
    while (pNode = pModel->GetNode(dwIdx))
    {
        pNode->geo->GetInfo(info);
        for (int32_t i = 0; i < info.nlabels; i++)
        {
            pNode->geo->GetLabel(i, label);
            const char *key = nullptr;
            std::vector<AICannon> *group = nullptr;

            if (storm::iEquals(label.name, "cannon", 6) && pFort->dwCannonType != -1)
            {
                key = AI_FORT::kCannonKey;
                group = &pFort->aCannons;
            }
            if (storm::iEquals(label.name, "culverin", 8) && pFort->dwCulverinType != -1)
            {
                key = AI_FORT::kCulverinKey;
                group = &pFort->aCulverins;
            }
            if (storm::iEquals(label.name, "mortar", 6) && pFort->dwMortarType != -1)
            {
                key = AI_FORT::kMortarKey;
                group = &pFort->aMortars;
            }

            if (group)
            {
                auto &cannon = group->emplace_back();
                if (group == &pFort->aMortars)
                    cannon.SetType(AICannon::CANNONTYPE::CANNONTYPE_MORTAR);
                cannon.Init(pFort, GetId(), label, key, static_cast<int32_t>(group->size() - 1));
                cannon.QuickRecharge(true);
            }
        }
        dwIdx++;
    }

    core.EraseEntity(model_id);
    return true;
}

ATTRIBUTES *AIFort::GetACharacter()
{
    if (pLastTraceFort)
        return pLastTraceFort->GetACharacter();
    return nullptr;
}

float AIFort::Trace(const glm::vec3 &vSrc, const glm::vec3 &vDst)
{
    pLastTraceFort = nullptr;

    auto fBestRes = 2.0f;
    auto fBestDistance = 1e10f;
    for (uint32_t i = 0; i < GetNumForts(); i++)
    {
        auto *pModel = static_cast<MODEL *>(core.GetEntityPointer(GetFort(i)->GetModelEID()));
        Assert(pModel);
        const auto fRes = pModel->Trace(vSrc, vDst);
        if (fRes > 1.0f)
            continue;
        const auto fDistance = fRes * sqrtf(storm::math::lengthSquared(vDst - vSrc));
        if (fDistance < fBestDistance)
        {
            pLastTraceFort = GetFort(i);
            fBestDistance = fDistance;
            fBestRes = fRes;
        }
    }

    return fBestRes;
}

float AIFort::Cannon_Trace(int32_t iBallOwner, int32_t iCannonType, const glm::vec3 &vSrc, const glm::vec3 &vDst)
{
    float fBestRes = 2.0;
    for (uint32_t i = 0; i < GetNumForts(); i++)
    {
        auto *pModel = static_cast<MODEL *>(core.GetEntityPointer(GetFort(i)->GetModelEID()));

        const float fRes = pModel->Trace(vSrc, vDst);
        if (fRes < fBestRes)
            fBestRes = fRes;
        if (fRes <= 1.0f)
        {
            const glm::vec3 vTemp = vSrc + (vDst - vSrc) * fRes;
            MESSAGE event;
            storm::sea_ai::ResetBallHit(event, storm::sea_ai::seaScriptProtocol(), iBallOwner, vTemp, iCannonType);
            core.Event(BALL_FORT_HIT, event);

            const glm::vec3 vDir = storm::math::normalizeOrZero(vDst - vSrc);
            core.Send_Message(GetFort(i)->GetBlotEID(), "lffffff", MSG_BLOTS_HIT, vTemp.x, vTemp.y, vTemp.z, vDir.x,
                              vDir.y, vDir.z);
        }
    }
    return fBestRes;
}

AIFort::AI_FORT *AIFort::FindFort(ATTRIBUTES *pACharacter)
{
    for (uint32_t i = 0; i < GetNumForts(); i++)
    {
        if (GetFort(i)->GetACharacter() == pACharacter)
            return GetFort(i);
    }
    return nullptr;
}

glm::vec3 AIFort::AI_FORT::GetAttackPoint(VAI_INNEROBJ *pObj)
{
    const float fDistance = pObj->GetMaxFireDistance();
    const uint32_t iMax = GetAllCannonsNum(); // boal fix
    for (uint32_t i = 0; i < iMax; i++)
    {
        AICannon *pC = GetCannon(i);
        if (pC->isDamaged())
            continue;

        const glm::vec3 vCPos = pC->GetPos();
        const float fDirY = pC->GetDirY();

        const glm::vec3 vRes = vCPos + fDistance / 1.4f * glm::vec3(sinf(fDirY), 0.0f, cosf(fDirY));

        // check for ship can place here
        // pObj->

        return (vRes - GetPos());
    }

    return glm::vec3(0.0f);
}

void AIFort::Save(CSaveLoad *pSL)
{
    pSL->SaveFloat(fMinCannonDamageDistance);
    for (auto &aFort : aForts)
        aFort->Save(pSL);
}

void AIFort::Load(CSaveLoad *pSL)
{
    fMinCannonDamageDistance = pSL->LoadFloat();
    for (auto &aFort : aForts)
        aFort->Load(pSL, GetId());
}

void AIFort::AI_FORT::Save(CSaveLoad *pSL)
{
    pSL->SaveVector(vPos);

    uint32_t i;
    for (i = 0; i < aCannons.size(); i++)
        aCannons[i].Save(pSL);
    for (i = 0; i < aCulverins.size(); i++)
        aCulverins[i].Save(pSL);
    for (i = 0; i < aMortars.size(); i++)
        aMortars[i].Save(pSL);
}

void AIFort::AI_FORT::Load(CSaveLoad *pSL, entid_t eid)
{
    vPos = pSL->LoadVector();

    uint32_t i;
    for (i = 0; i < aCannons.size(); i++)
    {
        aCannons[i].Load(pSL, this, eid, kCannonKey, static_cast<int32_t>(i));
        if (aCannons[i].isDamaged())
        {
            const glm::vec3 vPos = aCannons[i].GetPos();
            core.Event(FORT_LOADDMGCANNON, "fff", vPos.x, vPos.y, vPos.z);
        }
    }

    for (i = 0; i < aCulverins.size(); i++)
    {
        aCulverins[i].Load(pSL, this, eid, kCulverinKey, static_cast<int32_t>(i));
        if (aCulverins[i].isDamaged())
        {
            const glm::vec3 vPos = aCulverins[i].GetPos();
            core.Event(FORT_LOADDMGCANNON, "fff", vPos.x, vPos.y, vPos.z);
        }
    }

    for (i = 0; i < aMortars.size(); i++)
    {
        aMortars[i].Load(pSL, this, eid, kMortarKey, static_cast<int32_t>(i));
        if (aMortars[i].isDamaged())
        {
            const glm::vec3 vPos = aMortars[i].GetPos();
            core.Event(FORT_LOADDMGCANNON, "fff", vPos.x, vPos.y, vPos.z);
        }
    }
}

void AIFort::Fire(const glm::vec3 &vPos)
{
    for (auto &aFort : aForts)
        pShipsLights->AddDynamicLights(&aFort->tmpObject, vPos);
}
