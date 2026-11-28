// The renderer's loading screen (Renderer's Start/Draw/Release ProgressView and RunEnd): templates over the renderer
// so that tests/scene_readiness_check runs them with a renderer of its own. A friend of Renderer.
#pragma once

#include "core.h"
#include "loading_transaction.hpp"
#include "storm/render/pipelines.hpp"
#include "storm_render_types.h"

#include <SDL_timer.h>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>

#ifdef STORM_TOUCH_UI
#include <platform/android_hud_presenter.hpp>
#include <platform/android_ui.hpp>
#endif

namespace storm::render
{
struct LoadingScreen
{
    // StartProgressView: the loading artwork of a new load (the common spinner kept, the scene's pictures replaced).
    template <class R> static void start(R &self)
    {
        // The full-screen loading picture of every fader load (reload.c: FADER_PICTURE0 "loading\\ImgBack.tga").
        constexpr const char *kStockLoadingBackImage = "loading\\ImgBack.tga";
#ifdef STORM_TOUCH_UI
        storm::android::publishLoading(true);
#endif
        storm::loading::screen_up = true;
        self.progressSafeCounter = 0;
        self.progressPresents = self.progressPresentMs = 0;
        self.progressPresentGap = self.kProgressPresentGapMs;
        self.progressShownAt = SDL_GetTicks();
        if (self.progressTexture < 0)
        {
            // Loading the texture
            self.loadFrame = 0;
            self.isInPViewProcess = true;
            const int32_t t = self.TextureCreate("Loading\\progress.tga");
            self.isInPViewProcess = false;
            if (t < 0)
            {
                core.Trace(
                    "ERROR: the loading artwork Loading\\progress.tga cannot be loaded: loading screens show none");
                return;
            }
            self.progressTexture = t;
        }
        // A new load may start while the previous generation still owns its artwork.
        // Keep the common spinner, but replace all scene-specific images (including absent tips).
        if (self.back0Texture >= 0)
            self.TextureRelease(self.back0Texture);
        if (self.backTexture >= 0)
            self.TextureRelease(self.backTexture);
        if (self.progressTipsTexture >= 0)
            self.TextureRelease(self.progressTipsTexture);
        self.back0Texture = self.backTexture = self.progressTipsTexture = -1;
        // Loading an unscaled background image
        if (self.progressBackImage && self.progressBackImage[0])
        {
            self.isInPViewProcess = true;
            if (self.back0Texture >= 0)
                self.TextureRelease(self.back0Texture);
            self.back0Texture = -1;
            self.back0Texture = self.TextureCreate(self.progressBackImage);
            self.isInPViewProcess = false;
        }
        if (self.back0Texture < 0)
        {
            // A load without a fader (reload.c LoadMainCharacterInFirstLocationGroup: the new game without its
            // tutorial) sets no full-screen picture: the one every fader load sets (reload.c FADER_PICTURE0) is taken
            // instead.
            const bool scripts_set = self.progressBackImage && self.progressBackImage[0];
            self.isInPViewProcess = true;
            self.back0Texture = self.TextureCreate(kStockLoadingBackImage);
            self.isInPViewProcess = false;
            if (self.back0Texture >= 0)
                core.Trace("Loading screen: %s%s; the full-screen picture is the stock %s",
                           scripts_set ? "cannot load " : "the scripts set no full-screen picture",
                           scripts_set ? self.progressBackImage : "", kStockLoadingBackImage);
            else
                core.Trace(
                    "ERROR: Loading screen: %s%s and the stock %s cannot be loaded: black under the loading picture",
                    scripts_set ? "cannot load " : "the scripts set no full-screen picture",
                    scripts_set ? self.progressBackImage : "", kStockLoadingBackImage);
        }
        // Loading a scalable background image
        if (self.progressImage && self.progressImage[0])
        {
            self.isInPViewProcess = true;
            if (self.backTexture >= 0)
                self.TextureRelease(self.backTexture);
            self.backTexture = -1;
            self.backTexture = self.TextureCreate(self.progressImage);
            self.isInPViewProcess = false;
        }
        else
        {
            self.backTexture = -1;
        }
        // Loading tips
        if (self.progressTipsImage && self.progressTipsImage[0])
        {
            self.isInPViewProcess = true;
            if (self.progressTipsTexture >= 0)
                self.TextureRelease(self.progressTipsTexture);
            self.progressTipsTexture = -1;
            self.progressTipsTexture = self.TextureCreate(self.progressTipsImage);
            self.isInPViewProcess = false;
        }
        self.progressUpdateTime = SDL_GetTicks() - 1000;
    }

    // ProgressView: a progress frame between pieces of the scripts' loading work.
    template <class R> static void progress(R &self)
    {
        // TextureLoad can reenter here from an offscreen reflection/resource pass.
        // The loading transaction presents artwork once, after all prepare/realize
        // consumers restore their targets, in RunEnd. Never present a partial scene.
        if (storm::loading::active)
            return;
        self.DrawProgressView(true);
    }

    // DrawProgressView: the loading picture over the frame; `present`: a frame of its own, paced against the loading
    // work.
    template <class R> static void draw(R &self, bool present)
    {
        // get the texture
        if (self.progressTexture < 0)
            return;
        if (self.isInPViewProcess)
            return;
        // The loading picture is presented between pieces of loading work, never instead of them: the next one waits
        // until the load has worked four times as long as the last present took (at least the stock 50 ms). A display
        // that takes 66 ms a frame then costs a fifth of the load; with the stock 50 ms from one present's start to the
        // next, every resource the load created presented a frame and the load ran at the display's frame rate.
        if (present && SDL_GetTicks() - self.progressUpdateTime < self.progressPresentGap)
            return;
        self.isInPViewProcess = true;
        self.progressSafeCounter = 0;
        // During asynchronous preparation this overlays the existing world frame.
        if (present)
            self.BeginScene();
        // Filling the vertices of the texture
        struct LoadVertex
        {
            float x, y, z, rhw;
            uint32_t color;
            float u, v;
        } v[4];
        uint32_t i;
        for (i = 0; i < 4; i++)
        {
            v[i].z = 0.5;
            v[i].rhw = 2.0;
            v[i].color = 0xffffffff;
        }
        // Calculate the rectangle in which to draw
        D3DVIEWPORT9 vp;
        self.GetViewport(&vp);

        v[0].x = 0.0f;
        v[0].y = 0.0f;
        v[1].x = float(vp.Width);
        v[1].y = 0.0f;
        v[2].x = 0.0f;
        v[2].y = float(vp.Height);
        v[3].x = float(vp.Width);
        v[3].y = float(vp.Height);
        v[0].u = 0.0f;
        v[0].v = 0.0f;
        v[1].u = 1.0f;
        v[1].v = 0.0f;
        v[2].u = 0.0f;
        v[2].v = 1.0f;
        v[3].u = 1.0f;
        v[3].v = 1.0f;
        // The full-screen layer always covers the world: the scene under preparation is never shown, and the reveal
        // counts only frames whose cover was presented opaque (effects/scene_reveal.hpp). Without any full-screen
        // picture (StartProgressView logged it as an ERROR) the layer is black, the sampler's unbound texture (shim
        // Context).
        self.TextureSet(0, self.back0Texture);
        storm::render::ArmSceneCover(!present && vp.X == 0 && vp.Y == 0);
        self.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 2, v, sizeof(v[0]),
                             "ProgressBackTech");
        storm::render::ArmSceneCover(false);

        float dy = 0.0f;
        float dx = ((float(vp.Width) - (4.0f * float(vp.Height) / 3.0f)) / 2.0f);
        if (dx < 10.0f)
            dx = 0.0f;
        else
        {
            dy = 25.0f;
            dx = ((float(vp.Width) - (4.0f * (float(vp.Height) - 2.0f * dy) / 3.0f)) / 2.0f);
        }

        v[0].x = 0.0f + dx;
        v[0].y = 0.0f + dy;
        v[1].x = float(vp.Width) - dx;
        v[1].y = 0.0f + dy;
        v[2].x = 0.0f + dx;
        v[2].y = float(vp.Height) - dy;
        v[3].x = float(vp.Width) - dx;
        v[3].y = float(vp.Height) - dy;
        v[0].u = 0.0f;
        v[0].v = 0.0f;
        v[1].u = 1.0f;
        v[1].v = 0.0f;
        v[2].u = 0.0f;
        v[2].v = 1.0f;
        v[3].u = 1.0f;
        v[3].v = 1.0f;

        self.TextureSet(0, self.backTexture);
        if (self.backTexture < 0)
        {
            for (i = 0; i < 4; i++)
                v[i].color = 0;
        }
        else
        {
            if (self.progressTipsTexture >= 0)
            {
                self.TextureSet(1, self.progressTipsTexture);
                self.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 2, v,
                                     sizeof(v[0]), "ProgressBackTechWithTips");
            }
            else
            {
                self.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 2, v,
                                     sizeof(v[0]), "ProgressBackTech");
            }
        }
        if (self.backTexture < 0)
            for (i = 0; i < 4; i++)
                v[i].color = 0xffffffff;
        // Animated object
        self.m_fHeightDeformator = ((float)vp.Height * 4.0f) / ((float)vp.Width * 3.0f);
        glm::vec3 pos((vp.Width - dx * 2.0f) * self.progressFramesPosX + dx,
                      (vp.Height - dy * 2.0f) * self.progressFramesPosY + dy, 0.0f);
        glm::vec3 size((vp.Width - dx * 2.0f) * self.progressFramesWidth,
                       (vp.Height - dy * 2.0f) * self.progressFramesHeight * 4.0f / 3.0f, 0.0f);
        v[0].x = pos.x;
        v[0].y = pos.y;
        v[1].x = pos.x + size.x + 0.5f;
        v[1].y = pos.y;
        v[2].x = pos.x;
        v[2].y = pos.y + size.y + 0.5f;
        v[3].x = pos.x + size.x + 0.5f;
        v[3].y = pos.y + size.y + 0.5f;
        v[3].y = pos.y + size.y + 0.5f;
        // Frame grid size
        int32_t sizeX = self.progressFramesCountX;
        int32_t sizeY = self.progressFramesCountY;
        // Position of the current frame
        int32_t fx = self.loadFrame % sizeX;
        int32_t fy = self.loadFrame / sizeY;
        v[0].u = fx / float(sizeX);
        v[0].v = fy / float(sizeY);
        v[1].u = (fx + 1.0f) / float(sizeX);
        v[1].v = fy / float(sizeY);
        v[2].u = fx / float(sizeX);
        v[2].v = (fy + 1.0f) / float(sizeY);
        v[3].u = (fx + 1.0f) / float(sizeX);
        v[3].v = (fy + 1.0f) / float(sizeY);
        // Draw
#ifdef STORM_TOUCH_UI
        // The loading/saving spinner of the original interface does not draw over a touch UI; the game's loading
        // picture stays.
        if (!storm::android::legacyDrawRefused("loading spinner"))
#endif
        {
            self.TextureSet(0, self.progressTexture);
            self.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 2, v, sizeof(v[0]),
                                 "ProgressTech");
        }
        if (present)
        {
            const uint32_t started = SDL_GetTicks();
            self.EndScene();
            self.d3d9->Present();
            self.BeginScene();
            self.progressUpdateTime = SDL_GetTicks();
            const uint32_t took = self.progressUpdateTime - started;
            self.progressPresentGap = std::max(self.kProgressPresentGapMs, took * 4);
            ++self.progressPresents;
            self.progressPresentMs += took;
        }
        // Next frame
        self.loadFrame++;
        if (self.loadFrame >= sizeX * sizeY)
            self.loadFrame = 0;
        // leave
        self.isInPViewProcess = false;
    }

    // RunEnd: the end of a frame, the loading transaction's frames included: one Present on every path.
    template <class R> static void run_end(R &self)
    {
        if (storm::loading::active)
        {
            // Without the stock artwork (Loading\\progress.tga failed, StartProgressView logged why) the location still
            // loads: the frames present without it.
            if (self.progressTexture < 0 && storm::loading::first_frame)
                core.Trace("ERROR: the stock loading artwork is missing: the location loads without it");
            self.bInsideScene = false;
            storm::loading::ready = !storm::render::HoldSceneReveal();
            self.DrawProgressView(false);
            self.DrawFrameStats(); // one Present; never release artwork from inside the warm loop
            return;
        }
        self.bInsideScene = false;
        // Ordinary legacy retention only. Readiness is completed synchronously inside
        // NativeReloadProgressEnd, before scripts may activate the new world.
        if (self.progressTexture >= 0)
        {
            if (self.progressSafeCounter >= 50)
                self.EndProgressView();
            else
            {
                const int32_t oldCnt = self.progressSafeCounter;
                self.DrawProgressView(false);
                self.progressSafeCounter = oldCnt + 1;
            }
        }
        self.DrawFrameStats(); // exactly one ordinary Present on every path, including a held/error load
    }

    // ReleaseProgressView: the loading screen goes with its artwork.
    template <class R> static void release(R &self)
    {
#ifdef STORM_TOUCH_UI
        storm::android::publishLoading(false);
#endif
        storm::loading::screen_up = false;
        if (self.progressPresents)
            core.Trace("Loading screen: %u progress frames presented in %u ms of loading, %u ms of it presenting them",
                       self.progressPresents, SDL_GetTicks() - self.progressShownAt, self.progressPresentMs);
        self.progressPresents = 0;
        if (self.progressTexture >= 0)
            self.TextureRelease(self.progressTexture);
        self.progressTexture = -1;
        if (self.backTexture >= 0)
            self.TextureRelease(self.backTexture);
        self.backTexture = -1;
        if (self.back0Texture >= 0)
            self.TextureRelease(self.back0Texture);
        self.back0Texture = -1;
        if (self.progressTipsTexture >= 0)
            self.TextureRelease(self.progressTipsTexture);
        self.progressTipsTexture = -1;
        if (self.progressImage && self.progressImageSize > 0)
            self.progressImage[0] = 0;
        if (self.progressBackImage && self.progressBackImageSize > 0)
            self.progressBackImage[0] = 0;
        if (self.progressTipsImage && self.progressTipsImageSize > 0)
            self.progressTipsImage[0] = 0;
    }
};
} // namespace storm::render
