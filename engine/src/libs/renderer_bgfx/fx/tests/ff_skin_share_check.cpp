// The skin share of a character's pixel in the lit programs (shaders/include/ff_skin_share.sh): the torso stays masked
// where the skin colour is unknown, and in the gloss-only variant too (the same function).
#include "../../tests/check.hpp"
#include "shader_scalars.hpp"

namespace shader
{
#include "../../shaders/include/ff_skin_share.sh"
} // namespace shader

int main()
{
    using shader::ffSkinShare;
    CHECK(ffSkinShare(0.0f, 1.0f, -1.0f, 0.0f) == 1.0f);  // unknown colour: the torso is masked
    CHECK(ffSkinShare(0.0f, 0.25f, -1.0f, 0.0f) == 0.5f); // in proportion to its bones' share
    CHECK(ffSkinShare(0.0f, 1.0f, 0.4f, 1.0f) == 1.0f);   // bare skin of the known colour
    CHECK(ffSkinShare(0.0f, 1.0f, 0.4f, 0.0f) == 0.0f);   // cloth keeps its relief and gloss
    CHECK(ffSkinShare(0.0f, 1.0f, 0.0f, 1.0f) == 0.0f);   // the colour test switched off
    CHECK(ffSkinShare(0.5f, 0.0f, 0.0f, 0.0f) == 1.0f);   // half a head bone's share takes all
    return failures != 0;
}
