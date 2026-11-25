// The skin and torso weights the model writes into the GPU copy of a skinned vertex (model gpu_skinning.hpp
// writeSkinWeights): bones of unknown anatomy count as skin, so the relief never reaches unverified bare skin.
#include "gpu_skinning.hpp"
#include "storm/render/skinned_vertex.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace storm::render;

namespace
{
bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}
} // namespace

int main()
{
    uint8_t v[kSkinnedSourceStride]{};
    const float weight = 0.5f;
    std::memcpy(v + kSkinnedWeightOffset, &weight, sizeof(weight));
    v[kSkinnedBonesOffset] = 0;
    v[kSkinnedBonesOffset + 1] = 1;
    const auto skin = [&] { return v[kSkinnedBonesOffset + kSkinWeightByte]; };
    const auto torso = [&] { return v[kSkinnedBonesOffset + kTorsoWeightByte]; };
    storm::model::GpuSkinning::writeSkinWeights(v, 1, {});
    bool ok = expect(skin() == 255 && torso() == 0, "a skeleton without classes: all skin");
    storm::model::GpuSkinning::writeSkinWeights(v, 1, {1, 2, 0});
    ok &= expect(skin() == 128 && torso() == 128, "half a head bone, half a torso bone");
    v[kSkinnedBonesOffset] = v[kSkinnedBonesOffset + 1] = 2;
    storm::model::GpuSkinning::writeSkinWeights(v, 1, {1, 2, 0});
    ok &= expect(skin() == 0 && torso() == 0, "clothing bones: neither");
    v[kSkinnedBonesOffset] = v[kSkinnedBonesOffset + 1] = 255;
    storm::model::GpuSkinning::writeSkinWeights(v, 1, {1, 2, 0});
    ok &= expect(skin() == 255, "a bone past the classes: skin");
    return ok ? 0 : 1;
}
