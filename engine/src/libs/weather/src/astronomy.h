#pragma once

#include <array>
#include <string>

#include "typedef.h"
#include <vector>

class Astronomy : public Entity
{
  public:
    static VRenderer *pRS;
    static VGEOMETRY *pGS;

    Astronomy();
    ~Astronomy() override;

    bool Init() override;
    void SetDevice();
    void Realize(uint32_t Delta_Time);
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
        }
    }

    ATTRIBUTES *GetRoot(ATTRIBUTES *pA);

  private:
    class PLANETS
    {
      public:
        struct Planet
        {
            GEOS *pGeo;
            glm::vec3 vPos;
            int32_t iTexture;
            float fDistance, fRealDistance;
            float fSpeed;
            float fInclination;
            float fDiameter;
            float fScale, fFakeScale, fAngle;
            float fMagMax, fMagMin;
        };

        std::vector<Planet> aPlanets;
        float fPlanetScale;

        float fPlanetFade;
        float fFadeTimeStart;
        float fFadeTime;

        PLANETS();
        ~PLANETS();

        void ReleasePlanets();

        void Init(ATTRIBUTES *pAP);
        void Realize(double dDeltaTime, double dHour);
        void TimeUpdate(ATTRIBUTES *pAP);
    };

    class STARS
    {
      public:
        STARS();
        ~STARS();

        void Init(ATTRIBUTES *pAP);
        void Execute(double dDeltaTime, double dHour);
        void Realize(double dDeltaTime, double dHour);

        void TimeUpdate(ATTRIBUTES *pAP);

        bool IsEnable() const
        {
            return bEnable;
        };

      private:
        const char *sCatalog, *sTexture;
        float fRadius, fSize, fHeightFade, fSunFade;
        float fVisualMagnitude, fTelescopeMagnitude;
        int32_t iTexture;
        bool bEnable;
        int32_t iVertexBuffer, iVertexBufferColors;
        storm::render::VertexDeclarationId vertexDecl;
        float fPrevFov;

        struct Star
        {
            float fRA;
            float fDec;
            float fMag;
            char cSpectr[2];
            uint32_t dwColor;
            float fAlpha;
            glm::vec3 vPos;
        };

        uint32_t Spectr[256]{};
        std::vector<Star> aStars;

        // What the star buffers were built from: the game sets the stars again every hour.
        struct BuiltFrom
        {
            std::string catalog;
            float radius = 0.f, heightFade = 0.f;
            std::array<uint32_t, 256> spectr{};
            bool operator==(const BuiltFrom &) const = default;
        } builtFrom;

        float fFadeValue;
        float fFadeTimeStart;
        float fFadeTime;
        float m_fTwinklingTime;
    };

    double dHour, dTimeScale;
    STARS Stars;
    PLANETS Planets;
};
