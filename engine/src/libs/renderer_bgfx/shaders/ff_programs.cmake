# The lit and baked scene programs of the fixed-function emulator and the programs specialised for single stage setups.
# The only definition: CMake compiles the fragment variants from it and generates the C++ table Lighting and the shim
# choose from and the setups' constants the specialised programs read (cmake/ff_program_table.cmake).

# The product blocks of include/ff_fragment.sh. A family's programs compile FX_FF_<block> for each block it names and
# nothing else, so the lamps' code is only in the programs of the draws the lamps light.
set(ff_blocks LAMPS AO TOWN_WALL WET LAYER)

# name | entry (lit: fs_ff_lit.sc, baked: fs_ff_baked.sc, pbr: fs_ff_pbr.sc) | blocks. A draw takes the narrowest
# family of its entry that holds every block it reads (src/effects/lighting.cpp ChooseProgram).
#   lit          lit draws without lamps or the AO volume
#   lit_lamps    lit draws lit by the place's lamps or in the AO volume (with the room light)
#   baked        baked colours without lamps or the AO volume (a town's walls relit by our sun)
#   baked_lamps  baked colours relit by the lamps or in the AO volume
#   pbr          a game's own materials (renderer_bgfx/README.md, "Materials") without lamps or the AO volume
#   pbr_lamps    a game's own materials lit by the lamps or in the AO volume
#   pbr_layer    a game's own layered materials (a mask and three layers over the base), without lamps or the AO volume
#   pbr_layer_lamps  the layered materials lit by the lamps or in the AO volume
# Each family adds 12 programs to the «Высокая» switch (6 axis variants and their opaque twins), so the small blocks
# (AO, town wall, rain) ride with a family instead of having their own.
set(ff_families
    "lit|lit|WET"
    "lit_lamps|lit|LAMPS,AO,WET"
    "baked|baked|TOWN_WALL,WET"
    "baked_lamps|baked|LAMPS,AO,TOWN_WALL,WET"
    "pbr|pbr|"
    "pbr_lamps|pbr|LAMPS,AO"
    "pbr_layer|pbr|LAYER"
    "pbr_layer_lamps|pbr|LAMPS,AO,LAYER")

# Every family in every combination of the axes, each value "letters|defines". A program is named
# fs_ff_<family>[_<specular letters><relief letters><shadow letters>]. Linear light and the soft sun filter are
# run-time switches of these programs (fx_lighting.sh), not variants.
set(ff_axis_specular "|" "s|FX_LIGHT_SPECULAR")
set(ff_axis_relief "|" "n|FX_LIGHT_NORMALMAP" "np|FX_LIGHT_NORMALMAP,FX_LIGHT_PARALLAX")
# h: the presets with sun shadows draw all their lit and baked draws with it.
set(ff_axis_shadow "|" "h|FX_LIGHT_SHADOW")
# An entry may compile some values of an axis only (ff_axis_<axis>_<entry>). A game's own materials always have their
# highlights (a metal is black without them), and their normal maps are the game's, with no height for the parallax.
set(ff_axis_specular_pbr "s|FX_LIGHT_SPECULAR")
set(ff_axis_relief_pbr "|" "n|FX_LIGHT_NORMALMAP")

# The plain entries (shaders/<name>.sc) whose opaque twins are compiled; the families' programs all have one.
set(ff_plain_twins fs_ff fs_ff_dyn fs_ff_cube0 fs_ff_cube1 fs_ff_cube2)

# The interior kinds: a family's programs for an interior's draws, which hold no town wall and no rain and take no sun
# shadow (Lighting::prepare), so those blocks are left out. name | entry | blocks, as a family. An interior draw whose
# family holds every block of a kind takes the kind's specialised program; a kind has no general programs.
set(ff_room_families
    "baked_room|baked|LAMPS,AO")

# Stage setups that have specialised programs: id | one field per stage, "colour op,arg1,arg2,arg0,alpha op,arg1,arg2,
# arg0,temp,projected,bound" (D3DTOP and D3DTA values, an argument + 16 complemented, + 32 alpha replicated; temp: the
# result goes to D3DTA_TEMP; projected: D3DTTFF_PROJECTED without pre-transformed vertices; bound: a texture is set).
# A draw whose stages are exactly a setup's (shim Device::ff_stages) takes the setup's programs: its rows are constants
# there (ff_stages.sh FF_SETUP). A setup and a row are listed for a share of the shaded samples a place draws with
# them (at least 5 % somewhere); the match is by the codes alone, whatever draws them.
set(ff_setups
    # 2x diffuse * texture, alpha diffuse * texture
    "1|5,0,2,1,4,0,2,1,0,0,1"
    # texture * diffuse blended into a second texture by the texture factor's alpha, alpha ops disabled
    "2|4,2,0,1,1,0,2,1,0,0,1|14,2,1,1,1,2,1,1,0,0,1"
    # the texture with alpha diffuse * texture, 2x a second texture (a lightmap), 2x diffuse
    "3|3,0,2,1,4,0,2,1,0,0,1|5,1,2,1,1,2,1,1,0,0,1|5,1,0,1,1,2,1,1,0,0,1"
    # texture * diffuse, alpha too
    "4|4,0,2,1,4,0,2,1,0,0,1"
    # the diffuse colour and alpha alone, a texture bound and unread
    "5|2,0,2,1,2,0,2,1,0,0,1")

# Specialised programs: plain (fs_ff.sc), ground (fs_ff_dyn.sc: plain under moving casters' sun shadows) or a family or
# kind | its axis letters (specular, relief, shadow) | setup id.
# Named <the general program's name>_u<setup>; each has its opaque twin.
set(ff_specialised
    "plain||1" "plain||2" "plain||3" "plain||4" "plain||5"
    # the sky and lightmapped geometry under the shadows of moving casters
    "ground||2" "ground||3"
    # an interior's baked walls, floors and furniture: «Высокая» and «Средняя», relief off, on, on with gloss, and
    # the normal maps without the height maps
    "baked_room||1" "baked_room|np|1" "baked_room|snp|1" "baked_room|n|1" "baked_room|sn|1"
    # an interior's lightmapped walls and floors: relief off, on, and the normal maps with gloss without the heights
    "baked_room||3" "baked_room|np|3" "baked_room|sn|3"
    # baked models and lightmapped geometry on «Низкая» with the normal maps on (no lamps, no sun shadow)
    "baked|np|1" "baked|np|3"
    # outdoors under the lamps (a town at night, a deck): «Высокая» with gloss and relief, without relief, without the
    # heights, «Средняя»
    "baked_lamps|snph|1" "baked_lamps|sh|1" "baked_lamps|h|1" "baked_lamps|snh|1" "baked_lamps|nph|1"
    "baked_lamps|snph|3" "baked_lamps|h|3" "baked_lamps|snh|3" "baked_lamps|nph|3"
    # outdoors by day (a town, an island seen from the sea): the same presets, and lightmapped geometry without gloss
    # without the heights
    "baked|snph|1" "baked|h|1" "baked|snh|1" "baked|nph|1" "baked|snph|3" "baked|snh|3" "baked|nph|3" "baked|nh|3")
