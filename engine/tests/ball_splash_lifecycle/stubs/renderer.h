// The renderer the ball splash's translation unit (src/libs/ball_splash/src/t_splash.cpp) creates its shared buffers
// and textures with: it counts them and fails on a release of what it did not create or still holds at its end.
#pragma once
#include "storm_render_types.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <vector>
struct VRenderer
{
    int next = 1, created = 0, released = 0;
    std::map<int, std::vector<uint16_t>> indices;
    std::map<int, int> vertices, textures;
    ~VRenderer()
    {
        assert(indices.empty() && vertices.empty() && textures.empty());
    }
    int CreateIndexBuffer(int bytes, int)
    {
        int id = next++;
        ++created;
        indices[id].resize(bytes / 2);
        return id;
    }
    int CreateVertexBuffer(int, int bytes, int)
    {
        int id = next++;
        ++created;
        vertices[id] = bytes;
        return id;
    }
    int TextureCreate(const char *)
    {
        int id = next++;
        ++created;
        textures[id] = 1;
        return id;
    }
    void *LockIndexBuffer(int id)
    {
        assert(indices.contains(id));
        return indices.at(id).data();
    }
    void UnLockIndexBuffer(int id)
    {
        assert(indices.contains(id));
    }
    void ReleaseIndexBuffer(int id)
    {
        assert(indices.erase(id) == 1);
        ++released;
    }
    void ReleaseVertexBuffer(int id)
    {
        assert(vertices.erase(id) == 1);
        ++released;
    }
    void TextureRelease(int id)
    {
        assert(textures.erase(id) == 1);
        ++released;
    }
    void *LockVertexBuffer(int id, uint32_t = 0)
    {
        assert(vertices.contains(id));
        return nullptr;
    }
    void UnLockVertexBuffer(int)
    {
    }
    void SetTransform(int32_t, D3DMATRIX *)
    {
    }
    bool TextureSet(int32_t, int32_t)
    {
        return true;
    }
    void DrawBuffer(int32_t, int32_t, int32_t, int32_t, size_t, size_t, size_t, const char * = nullptr)
    {
    }
    uint32_t GetRenderState(uint32_t, uint32_t *)
    {
        return 0;
    }
};
