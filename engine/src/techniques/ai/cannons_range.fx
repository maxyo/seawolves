technique CannonsRange
{
    pass p0
    {
        ZWriteEnable = false;
        CullMode = none;
        Lighting = false;
        AlphaBlendEnable = true;
        AlphaTestEnable = false;

        SrcBlend = srcalpha;
        DestBlend = invsrcalpha;

        ColorOp[0] = selectarg1;
        ColorArg1[0] = diffuse;

        AlphaOp[0] = selectarg1;
        AlphaArg1[0] = diffuse;

        ColorOp[1] = disable;
    }
}
