#include "font.h"
#include "font_glyphs.hpp"
#include "core.h"
#include "storm/config.hpp"
#include "utf8.h"
#include <platform/android_interface_draw.hpp>

#include <fmt/format.h>

#include <cmath>

namespace
{

constexpr size_t MAX_SYMBOLS = 512;
constexpr size_t SYM_VERTEXS = 6;
constexpr size_t USED_CODES = 0x2070; // the char_<glyph> keys: up to the end of the General Punctuation block

constexpr auto FONT_CHAR_FVF = (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXTUREFORMAT2);

struct FONT_CHAR_VERTEX
{
    glm::vec3 pos;
    float rhw;
    uint32_t color;
    float tu, tv;
};

bool MakeLong(char **pDataPointer, int32_t *result)
{
    int32_t index;
    char *pData;
    pData = *pDataPointer;
    index = 0;
    while (true)
    {
        if (pData[index] == 0)
        {
            if (index == 0)
                return false;
            *result = atol(pData);
            return true;
        }
        if (pData[index] == ',')
        {
            pData[index] = 0;
            *result = atol(pData);
            *pDataPointer += (index + 1);
            return true;
        }
        index++;
        if (index > 8)
            throw std::runtime_error("inf loop");
    }
    return false;
}

} // namespace

FONT::FONT(VRenderer &renderer, storm::shim::Device &device)
    : charDescriptors_(USED_CODES), renderService_(renderer), device_(device)
{
}

FONT::~FONT()
{
    if (vertexBuffer_)
        vertexBuffer_->Release();
    if (textureHandle_ >= 0)
        renderService_.TextureRelease(textureHandle_);
}

bool FONT::Init(const char *font_name, const char *iniName)
{
    if (initialized_)
    {
        throw std::runtime_error("Font already initialized");
    }
    initialized_ = true;

    char buffer[MAX_PATH];

    auto ini = fio->OpenIniFile(iniName);
    if (ini == nullptr)
        return false;

    if (ini->GetInt(font_name, "AspectHeightConstant", 0) == 1)
    {
        horizontalAspectRatio_ = 1.f / verticalAspectRatio_;
        verticalAspectRatio_ = 1.f;
    }

    auto _fscale = 1.f;
    _fscale = ini->GetFloat(font_name, "pcscale", 1.f);
    if (_fscale != 1.f)
    {
        horizontalAspectRatio_ *= _fscale;
        verticalAspectRatio_ *= _fscale;
    }

    height_ = ini->GetInt(font_name, "Height", 0);
    height_ = static_cast<int32_t>(height_ * verticalAspectRatio_);

    if (ini->ReadString(font_name, "Texture", buffer, sizeof(buffer) - 1, ""))
    {
        textureName_ = buffer;
    }
    if (ini->ReadString(font_name, "Techniques", buffer, sizeof(buffer) - 1, ""))
    {
        techniqueName_ = buffer;
    }
    textureSizeX_ = ini->GetInt(font_name, "Texture_xsize", 1);
    textureSizeY_ = ini->GetInt(font_name, "Texture_ysize", 1);
    symbolInterval_ = ini->GetInt(font_name, "Symbol_interval", 0);

    if (ini->TestKey(font_name, "Shadow", nullptr))
        drawShadows_ = true;

    shadowOffsetX_ = ini->GetInt(font_name, "Shadow_offsetx", 2);
    shadowOffsetY_ = ini->GetInt(font_name, "Shadow_offsety", 2);
    spacebarWidth_ = static_cast<int32_t>(ini->GetInt(font_name, "Spacebar", 8) * horizontalAspectRatio_);

    LoadGlyphs(*ini, font_name, iniName);

    FONT_CHAR_VERTEX *pVertex;
    device_.CreateVertexBuffer(sizeof(FONT_CHAR_VERTEX) * MAX_SYMBOLS * SYM_VERTEXS,
                               D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, FONT_CHAR_FVF, D3DPOOL_SYSTEMMEM, &vertexBuffer_,
                               nullptr);
    if (vertexBuffer_ == nullptr)
        throw std::runtime_error("vbuffer error");
    vertexBuffer_->Lock(0, sizeof(FONT_CHAR_VERTEX) * MAX_SYMBOLS * SYM_VERTEXS, (void **)&pVertex, 0);
    for (size_t vertex = 0; vertex < MAX_SYMBOLS * SYM_VERTEXS; vertex++)
        pVertex[vertex].pos.z = 0.5f;
    vertexBuffer_->Unlock();

    textureHandle_ = renderService_.TextureCreate(textureName_.c_str());
    if (textureHandle_ < 0)
    {
        core.Trace("Not Found Texture: %s", textureName_.c_str());
        return false;
    }

    return true;
}

void FONT::SetGlyph(const storm::font::GlyphEntry &glyph)
{
    if (glyph.codepoint >= charDescriptors_.size())
        charDescriptors_.resize(glyph.codepoint + 1);
    auto &symbol = charDescriptors_[glyph.codepoint];
    symbol.Pos.x1 = 0.f;
    symbol.Pos.x2 = static_cast<float>(static_cast<int32_t>(glyph.width * horizontalAspectRatio_));
    symbol.Pos.y1 = static_cast<float>(height_ - static_cast<int32_t>(glyph.height * verticalAspectRatio_));
    symbol.Pos.y2 = static_cast<float>(height_);
    symbol.Tuv.x1 = (glyph.x + .5f) / static_cast<float>(textureSizeX_);
    symbol.Tuv.y1 = (glyph.y + .5f) / static_cast<float>(textureSizeY_);
    symbol.Tuv.x2 = symbol.Tuv.x1 + (glyph.width - 1.f) / static_cast<float>(textureSizeX_);
    symbol.Tuv.y2 = symbol.Tuv.y1 + (glyph.height - 1.f) / static_cast<float>(textureSizeY_);
    symbol.KernLeft = static_cast<float>(static_cast<int32_t>(glyph.kern_left * horizontalAspectRatio_));
    symbol.KernRight = static_cast<float>(static_cast<int32_t>(glyph.kern_right * horizontalAspectRatio_));
}

// The `char = ...` entries of the section `GroupTemplate` names (or of the font's own section) where a fonts.ini has
// them; else one `char_<glyph>` key per glyph.
void FONT::LoadGlyphs(INIFILE &ini, const char *font_name, const char *iniName)
{
    char buffer[MAX_PATH];
    std::string table = font_name;
    if (ini.ReadString(font_name, "GroupTemplate", buffer, sizeof(buffer) - 1, ""))
        table = buffer;
    const bool kerning = ini.TestKey(font_name, "Kerning", nullptr);
    if (ini.ReadString(table.c_str(), "char", buffer, sizeof(buffer) - 1, ""))
    {
        do
        {
            try
            {
                SetGlyph(storm::font::ParseGlyphEntry(buffer, kerning));
            }
            catch (const std::invalid_argument &problem)
            {
                throw std::runtime_error(fmt::format("{} [{}] char = {}: {}", iniName, table, buffer, problem.what()));
            }
        } while (ini.ReadStringNext(table.c_str(), "char", buffer, sizeof(buffer) - 1));
        return;
    }

    ini.CaseSensitive(true);
    for (char32_t codepoint = 30; codepoint < USED_CODES; codepoint++)
    {
        char utf8[5];
        utf8::CodepointToUtf8(utf8, codepoint);
        char key_name[MAX_PATH];
        if (codepoint >= 'a' && codepoint <= 'z')
            sprintf_s(key_name, "char_%s_", utf8);
        else if (codepoint == '=')
            sprintf_s(key_name, "char_equ");
        else
            sprintf_s(key_name, "char_%s", utf8);
        if (!ini.ReadString(font_name, key_name, buffer, sizeof(buffer), ""))
            continue;
        char *pData = buffer;
        storm::font::GlyphEntry glyph;
        glyph.codepoint = codepoint;
        for (int32_t *number : {&glyph.x, &glyph.y, &glyph.width, &glyph.height})
            if (!MakeLong(&pData, number))
                throw std::runtime_error("invalid font record");
        SetGlyph(glyph);
    }
    ini.CaseSensitive(false);
}

void OffsetFRect(FLOAT_RECT &fr, float dx, float dy)
{
    fr.x1 += dx;
    fr.x2 += dx;
    fr.y1 += dy;
    fr.y2 += dy;
}

int32_t FONT::GetStringWidth(const std::string_view &text, std::optional<float> scale_override) const
{
    if (text.empty())
        return 0;
    float xoffset = 0;
    const int32_t s_num = text.size();

    const float scale = scale_override.value_or(scale_);

    for (int32_t i = 0; i < s_num; i += utf8::u8_inc(text.data() + i))
    {
        uint32_t Codepoint = utf8::Utf8ToCodepoint(text.data() + i);

        if (Codepoint >= charDescriptors_.size()) { // also catches -1 (invalid sequence) converted to unsigned
            core.Trace("Invalid codepoint: %d", Codepoint);
            if constexpr(storm::kIsDebug) {
                throw std::runtime_error(fmt::format("Invalid codepoint: {}", Codepoint));
            }
            continue;
        }

        const FONT_SYMBOL &symbol = charDescriptors_[Codepoint];
        xoffset += (symbol.Pos.x2 - symbol.Pos.x1 - symbol.KernLeft - symbol.KernRight + symbolInterval_) * scale;
        if (Codepoint == ' ')
        {
            xoffset += spacebarWidth_ * scale;
        }
    }

    return static_cast<int32_t>(xoffset);
}

int32_t FONT::UpdateVertexBuffer(int32_t x, int32_t y, char *data_PTR, int utf8length, float scale, uint32_t color)
{
    int32_t s_num;
    int32_t n;
    float xoffset;
    FONT_CHAR_VERTEX *pVertex;

    s_num = strlen(data_PTR);

    vertexBuffer_->Lock(0, sizeof(FONT_CHAR_VERTEX) * utf8length * SYM_VERTEXS, (void **)&pVertex, 0);

    xoffset = 0;

    // Never write past the glyphs locked above (callers clamp utf8length to the buffer).
    for (int i = 0, curLetter = 0; i < s_num && curLetter < utf8length; i += utf8::u8_inc(data_PTR + i), curLetter++)
    {

        int Codepoint = utf8::Utf8ToCodepoint(data_PTR + i);

        if (Codepoint < 0 || Codepoint >= static_cast<int>(charDescriptors_.size())) {
            core.Trace("Invalid codepoint: %d", Codepoint);
            if constexpr(storm::kIsDebug) {
                throw std::runtime_error(fmt::format("Invalid codepoint: {}", Codepoint));
            }
            continue;
        }

        n = curLetter * 6;
        const FONT_SYMBOL &symbol = charDescriptors_[Codepoint];
        FLOAT_RECT pos = symbol.Pos;
        if (scale != 1.f)
        {
            pos.x1 *= scale;
            pos.x2 *= scale;
            pos.y1 *= scale;
            pos.y2 *= scale;
        }
        // kerning: the glyph starts left of the pen and the pen moves on by its width less both kernings
        OffsetFRect(pos, static_cast<float>(x) + xoffset - symbol.KernLeft * scale, static_cast<float>(y));
        xoffset += (symbol.Pos.x2 - symbol.Pos.x1 - symbol.KernLeft - symbol.KernRight + symbolInterval_) * scale;

        if (Codepoint == ' ')
        {
            pos.x1 = pos.x2 = pos.y1 = pos.y2 = 0;
            xoffset += spacebarWidth_ * scale;
        }

        pVertex[n + 0].pos.x = pos.x1;
        pVertex[n + 1].pos.x = pos.x1;
        pVertex[n + 2].pos.x = pos.x2;

        pVertex[n + 3].pos.x = pos.x1;
        pVertex[n + 4].pos.x = pos.x2;
        pVertex[n + 5].pos.x = pos.x2;

        pVertex[n + 0].pos.y = pos.y1;
        pVertex[n + 1].pos.y = pos.y2;
        pVertex[n + 2].pos.y = pos.y1;

        pVertex[n + 3].pos.y = pos.y2;
        pVertex[n + 4].pos.y = pos.y2;
        pVertex[n + 5].pos.y = pos.y1;

        FLOAT_RECT tuv = charDescriptors_[Codepoint].Tuv;

        pVertex[n + 0].tu = tuv.x1;
        pVertex[n + 1].tu = tuv.x1;
        pVertex[n + 2].tu = tuv.x2;

        pVertex[n + 3].tu = tuv.x1;
        pVertex[n + 4].tu = tuv.x2;
        pVertex[n + 5].tu = tuv.x2;

        pVertex[n + 0].tv = tuv.y1;
        pVertex[n + 1].tv = tuv.y2;
        pVertex[n + 2].tv = tuv.y1;

        pVertex[n + 3].tv = tuv.y2;
        pVertex[n + 4].tv = tuv.y2;
        pVertex[n + 5].tv = tuv.y1;

        pVertex[n + 0].color = pVertex[n + 1].color = pVertex[n + 2].color = pVertex[n + 3].color =
            pVertex[n + 4].color = pVertex[n + 5].color = color;

        pVertex[n + 0].rhw = pVertex[n + 1].rhw = pVertex[n + 2].rhw = pVertex[n + 3].rhw = pVertex[n + 4].rhw =
            pVertex[n + 5].rhw = scale;
    }
    vertexBuffer_->Unlock();
    return static_cast<int32_t>(xoffset);
}

int32_t FONT::Print(int32_t x, int32_t y, char *data_PTR, const FONT_PRINT_OVERRIDES &overrides)
{
    if (data_PTR == nullptr || techniqueName_.empty())
        return 0;
    auto xoffset = 0L;
    int32_t s_num = utf8::Utf8StringLength(data_PTR);
    if (s_num == 0)
        return 0;
    // The vertex buffer holds MAX_SYMBOLS glyphs; longer text is truncated, not written out of bounds.
    s_num = std::min<int32_t>(s_num, static_cast<int32_t>(MAX_SYMBOLS));

    // The renderer's SetFVF also unbinds the vertex shader the last technique left bound (a technique closes only its
    // pixel shader): the glyphs are pre-transformed and must not go through, say, the sea's vertex shader.
    renderService_.SetFVF(FONT_CHAR_FVF);
    const auto bDraw = renderService_.TechniqueExecuteStart(techniqueName_.c_str());
    if (!bDraw)
        return xoffset;

    renderService_.TextureSet(0, textureHandle_);
    device_.SetStreamSource(0, vertexBuffer_, 0, sizeof(FONT_CHAR_VERTEX));

    const bool drawShadows = overrides.shadow.value_or(drawShadows_);
    const float scale = overrides.scale.value_or(scale_);
    const uint32_t color = overrides.color.value_or(color_);
    // The shadow and the outline darken what is under the glyphs by their alpha.
    const auto darken = [&](int32_t at_x, int32_t at_y) {
        UpdateVertexBuffer(at_x, at_y, data_PTR, s_num, scale, color);

        device_.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
        device_.SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

        if (!storm::android::NativeInterfaceDrawSuppressed())
            device_.DrawPrimitive(D3DPT_TRIANGLELIST, 0, s_num * 2);
    };
    if (drawShadows)
        darken(x + shadowOffsetX_, y + shadowOffsetY_);
    if (const float outline = overrides.outline.value_or(0.0f); outline > 0.0f)
    {
        // Eight copies around the glyphs, at the outline's width on the axes and the diagonals.
        const float diagonal = outline * 0.70710678f;
        const float offsets[8][2] = {{outline, 0.0f},      {-outline, 0.0f},     {0.0f, outline},      {0.0f, -outline},
                                     {diagonal, diagonal}, {-diagonal, diagonal}, {diagonal, -diagonal}, {-diagonal, -diagonal}};
        for (const auto &offset : offsets)
            darken(x + static_cast<int32_t>(std::lround(offset[0])), y + static_cast<int32_t>(std::lround(offset[1])));
    }
    xoffset = UpdateVertexBuffer(x, y, data_PTR, s_num, scale, color);
    device_.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device_.SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    if (!storm::android::NativeInterfaceDrawSuppressed())
        device_.DrawPrimitive(D3DPT_TRIANGLELIST, 0, s_num * 2);
    while (renderService_.TechniqueExecuteNext())
        ;

    return xoffset;
}

void FONT::TempUnload()
{
    if (textureHandle_ != -1L)
        renderService_.TextureRelease(textureHandle_);
    textureHandle_ = -1L;
}

void FONT::RepeatInit()
{
    if (textureHandle_ == -1L)
        textureHandle_ = renderService_.TextureCreate(textureName_.c_str());
}
