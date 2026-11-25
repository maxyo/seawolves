// The town walls' relight (src/effects/town_light.hpp, lighting.cpp bind_town_wall): [0] the lightmaps' tone a, b, c and
// w its share for this draw (0 off; it fades in 0 -> 1 after it came on, TownLight::wall_weight); [1] towards the live sun (view space); [2] towards the sun the game baked with, w how far the light may rise above the game's (fx::RelightLift); [3], [4] the
// game's sky per bin of ours (bin centres (i + 0.5) / 8).
uniform vec4 u_fxTownWall[5];
float ffWallSky(float k)
{
    float x = clamp(k * 8.0 - 0.5, 0.0, 7.0);
    float i = min(floor(x), 6.0);
    int at = int(i);
    float lo = at < 4 ? u_fxTownWall[3][at] : u_fxTownWall[4][at - 4];
    float hi = at + 1 < 4 ? u_fxTownWall[3][at + 1] : u_fxTownWall[4][at - 3];
    return max(k, mix(lo, hi, x - i));
}
