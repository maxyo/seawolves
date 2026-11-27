// The graphics options' natives (renderer/src/graphics_options.hpp) of a fresh game: what they list, read and set.
#libriary "GraphicsOptionsScriptLib"

native int Graphics_GetOption(string name);
native int Graphics_SetOption(string name, int value);
native void Graphics_ListOptions(ref list);

object list;

int test_defaults()
{
    // The game's own engine.ini has full_screen; the rest are the desktop's defaults.
    return Graphics_GetOption("full_screen") == 0 && Graphics_GetOption("sun_shadows") == 2 &&
           Graphics_GetOption("render_scale") == 100 && Graphics_GetOption("anti_aliasing") == 3;
}

int test_list()
{
    Graphics_ListOptions(&list);
    if (GetAttributesNum(&list) < 20)
        return 0;
    aref aa;
    makearef(aa, list.anti_aliasing);
    if (aa.kind != "choice" || sti(aa.value) != 3 || sti(aa.default) != 3 || sti(aa.start) != 0)
        return 0;
    if (sti(aa.choices.v0) != 0 || sti(aa.choices.v3) != 3)
        return 0;
    if (list.render_scale.kind != "number" || sti(list.render_scale.min) != 50 || sti(list.render_scale.max) != 100)
        return 0;
    // A legacy game: the engine makes its texture maps, so their options are its own.
    return CheckAttribute(&list, "texture_normal_maps") && sti(list.texture_quality.start) == 1;
}

int test_set()
{
    if (!Graphics_SetOption("sun_shadows", 0) || Graphics_GetOption("sun_shadows") != 0)
        return 0;
    if (!Graphics_SetOption("anti_aliasing", 1) || Graphics_GetOption("anti_aliasing") != 1)
        return 0;
    return Graphics_SetOption("texture_quality", 1) && Graphics_GetOption("texture_quality") == 1;
}

int test_refused()
{
    // A value the option does not have is refused and the option keeps its value.
    if (Graphics_SetOption("sun_shadows", 3) || Graphics_SetOption("anisotropic_filtering", 3))
        return 0;
    return Graphics_GetOption("sun_shadows") == 0 && Graphics_GetOption("anisotropic_filtering") == 16;
}

int test_unknown()
{
    // An option the engine does not have: -1 and refused, each an error in the log.
    return Graphics_GetOption("no_such_option") == -1 && !Graphics_SetOption("no_such_option", 1);
}
