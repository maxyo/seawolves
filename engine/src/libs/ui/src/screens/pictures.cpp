#include "pictures.h"

#include <cstdio>

namespace storm::ui::screens
{
Json PictureOf(const ImageRef *image)
{
    if (image && image->kind == "portrait" && !image->image.empty())
        return Json{{"kind", "portrait"}, {"texture", "INTERFACES/PORTRAITS/128/face_" + image->image}};
    if (image && image->kind == "texture" && !image->image.empty())
        return Json{{"kind", "item"}, {"texture", image->image}};
    if (!image || image->kind != "atlas" || image->group.empty() || image->image.empty())
        return Json();
    return Json{{"kind", "item"}, {"texture", "atlas:" + image->group + "/" + image->image}};
}

Json TextureOf(const ImageRef *image)
{
    if (image && image->kind == "texture" && !image->image.empty())
    {
        char uv[96];
        std::snprintf(uv, sizeof uv, "%g,%g,%g,%g", image->uv[0], image->uv[1], image->uv[2], image->uv[3]);
        return Json{{"texture", image->image}, {"uv", uv}};
    }
    if (image && image->kind == "atlas" && !image->group.empty() && !image->image.empty())
        return Json{{"texture", "atlas:" + image->group + "/" + image->image}, {"uv", ""}};
    return Json();
}
} // namespace storm::ui::screens
