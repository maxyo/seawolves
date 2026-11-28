// The only renderer-specific seam of the UI runtime: everything above it talks to
// Rml::RenderInterface, never to a graphics API. The backend is bgfx (backend_bgfx.h);
// the host checks use a backend that draws nothing. See src/libs/ui/README.md.
#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <memory>
#include <string>
#include <vector>

namespace storm::ui
{

enum class PackFormat
{
    Rgba8,
    Rg8
};

class RenderBackend
{
  public:
    virtual ~RenderBackend() = default;
    virtual Rml::RenderInterface &Interface() = 0;
    virtual void SetViewport(int width, int height) = 0;
    // `clear` wipes the target to transparent first (stand-alone UI surface). A backend
    // that draws over the game frame ignores it.
    virtual void BeginFrame(bool clear) = 0;
    virtual void EndFrame() = 0;
    // True when the frame just ended was not shown: it would have been drawn without textures the
    // backend is still making (layer textures: box shadows), so the frame before it stays on the
    // screen. The host draws the frame again until it is shown, and tells nobody it was drawn.
    // A backend that makes every texture within the frame that draws it never holds one.
    virtual bool FrameHeld() const
    {
        return false;
    }

    // Game pictures (.tx). A backend that draws inside the engine frame gets them from the
    // renderer: Pending keeps the element's placeholder and polls again next frame, Ready
    // means GamePictureSource(texture) loads as an RmlUi texture, Failed keeps the
    // placeholder for good (logged by the backend). Unavailable: the backend has no picture
    // source at all (a screen without the engine, a capture without game files): the neutral
    // placeholder stays.
    enum class PictureState
    {
        Pending,
        Ready,
        Failed,
        Unavailable
    };
    virtual PictureState AcquireGamePicture(const std::string &texture) = 0;

    // A texture made ahead of the frame that draws it (Host::Prewarm: font layer textures): its GPU copy is made now
    // and its pixels are given back, so that a texture made ahead holds no second, CPU copy until its first draw. A
    // backend that makes no GPU copies (the host checks) has nothing to do.
    virtual void Upload(Rml::TextureHandle texture) = 0;

    // Why the device cannot sample a pack picture of this size and format (larger than its texture limit, a format it
    // does not sample), empty when it can. Host::Start refuses a pack with such a picture, before anything is drawn.
    virtual std::string RefusedTexture(int width, int height, PackFormat format) const = 0;
};

// RmlUi texture source of a renderer-supplied game picture (see AcquireGamePicture).
inline std::string GamePictureSource(const std::string &texture)
{
    return "storm-game-picture:" + texture;
}

// A picture of the UI pack, decoded when the pack loads (pack.json "images" and the icon atlas's pages), never when
// first drawn. Rgba8: premultiplied; Rg8: the icon atlas's coverage pages (how much of the vertex colour shows,
// coverage), drawn by the backend's coverage shader as (colour.rgb * R, colour.a * G).
struct PackImage
{
    int width = 0, height = 0;
    PackFormat format = PackFormat::Rgba8;
    std::vector<unsigned char> pixels;
};

// For backend authors: the picture an RmlUi texture source of the pack names ("textures/oak.jpg"), null when the
// pack's list lacks it (logged). Game pictures do not come this way; see game_image.cpp.
std::shared_ptr<const PackImage> FindPackImage(const std::string &source);

} // namespace storm::ui
