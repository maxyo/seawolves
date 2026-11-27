// After options.c, another game (its own settings folder): none of those choices.
#libriary "GraphicsOptionsScriptLib"

native int Graphics_GetOption(string name);

int test_own_options()
{
    return Graphics_GetOption("sun_shadows") == 2 && Graphics_GetOption("anti_aliasing") == 3 &&
           Graphics_GetOption("texture_quality") == 2;
}
