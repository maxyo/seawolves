technique Sea2
{
    pass p0
    {
        FogTableMode = none;
        FogVertexMode = none;
        CullMode = none;
        ColorVertex = true;
        SpecularEnable = false;
        SrcBlend = one;
        DestBlend = srcalpha;
        Lighting = false;
        LocalViewer = false;
        NormalizeNormals = false;

        AlphaBlendEnable = true;
        AlphaTestEnable = false;

        VertexShader = SEA2_VERTEX_SHADER;
        PixelShader = SEA2_PIXEL_SHADER;
    }
}
