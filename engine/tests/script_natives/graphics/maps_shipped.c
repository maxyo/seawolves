// profile: Caribbean Legend
// A game that ships its own texture maps has no options for making them.
#libriary "GraphicsOptionsScriptLib"

native void Graphics_ListOptions(ref list);
native int Graphics_GetOption(string name);
native int Graphics_SetOption(string name, int value);

object list;

int test_no_texture_maps()
{
    Graphics_ListOptions(&list);
    return !CheckAttribute(&list, "texture_normal_maps") && CheckAttribute(&list, "sun_shadows");
}

int test_no_texture_map_option()
{
    return Graphics_GetOption("texture_normal_maps") == -1 && !Graphics_SetOption("texture_normal_maps", 1);
}
