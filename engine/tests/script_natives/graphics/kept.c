// After options.c, the next start of the same game: its choices were kept in its settings folder.
#libriary "GraphicsOptionsScriptLib"

native int Graphics_GetOption(string name);

int test_kept()
{
    return Graphics_GetOption("sun_shadows") == 0 && Graphics_GetOption("anti_aliasing") == 1 &&
           Graphics_GetOption("texture_quality") == 1 && Graphics_GetOption("render_scale") == 100;
}
