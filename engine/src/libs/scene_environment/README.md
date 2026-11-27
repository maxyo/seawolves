# Scene environment

One state per frame of everything the place's light, sky and air are (`storm/environment/state.hpp`): the sun and
the moon, the direct light and its shadow strength, the sky's fill and the ground's bounce, the exposure, the sky's
radiance, the haze band at the horizon, height fog, the clouds' and the rain's state, the wind and how far it has
carried the clouds (WEATHER adds the wind over the game's time). The WEATHER entity
(`src/libs/weather`) fills it every frame and hands it to the renderer (`storm::render::SetSceneEnvironment`), which
draws the sky, lights and fogs the scene with it. Colours are linear; a light's colour carries its intensity in the
scene's units and `exposure` brings the scene to the display. The fixed-function pipeline takes its lights through
`DisplayLight`: exposed, and scaled down until no channel passes 1, so the hue stays.

## From a game's data (`storm/environment/evaluate.hpp`)

A game that ships `Resource/INI/time_of_day.json` (Caribbean Legend) is evaluated from its data:

- **Periods.** Four periods (dawn, day, sunset, night) each give the direct light, the sky's day and night
  radiance, the fill, the ground bounce, the exposure and the camera's grade. Eight transition hours pair them
  (night to dawn, dawn to day, day to sunset, sunset to night); inside a pair they blend by smoothstep, every value
  linearly but the exposure, which blends in log2. `atmosphere.json`'s haze colours and height fog periods blend the
  same way by their own transition hours.
- **Sun and moon.** The sun rises at 6 and sets at 20 on an arc whose plane leans by the place's noon elevation and
  turns by its azimuth (the lighting preset's `sun_height`, `sun_azimuth`); below the horizon it goes on round the
  circle. The moon's arc runs from 20 minus to 6 plus the overlap hours. The sun's share of the direct light hands
  over to the moon within the hand-off hours of 6 and 20; the sky's daylight within the twilight hours of them. The
  moon casts the shadows before the morning shadow hour and from the evening one; around both the shadows fade.
- **Hour table.** A place's lighting preset gives 24 hours of sun, fill, shadow, fog, exposure, bloom and emission;
  absent hours are filled between the present ones on the 24-hour circle (not the exposure, the image-based light
  or the world map's sea colours). The scripts' weather of the day names a rain preset per hour; at sea its `_sea`
  variant. A rain preset replaces the sun, fill, shadow and fog density of its hours, and none is used indoors or
  where the place does not rain.
- **A frame.** Outdoors the periods give the direct light (the sun's colour from the periods between the end of
  dawn and the start of the night, the moon's from the night) and the fill; while an hour rains its table keeps
  them. Indoors the table alone lights. Without image-based light the fill multiplies an irradiance: the periods'
  sky radiance times a constant outdoors, a constant indoors. The fog is the table's where its hours have fog
  (densities per 100 m), the height fog's for the rest, reaching the sky at its greatest distance; the haze band is
  the atmosphere's. The sky scatters the periods' day and night radiances; the sun's light on its disc is the day
  periods', the moon's the night's, on discs of that game's sky's sizes.
- **Weather.** The scripts' rows of the day (`WeatherState`, 24 hours and the next day's first) give the clouds, the
  rain's intensity and the flags of each hour. The clouds' cover is a fifth of the hour's cloud density, the scale
  the scripts themselves give a rain's intensity by. The rain clouds cover the sky in the hours that rain and grow or thin
  over the hour before or after; a rain that stopped leaves wetness that dries by a third per hour within the next.
- **Clouds.** `atmosphere.json`'s `textured_clouds` periods blend by the time of day's transition hours into the
  hour's cloud look (`CloudLook`: brightness, the textured layers' opacity, the sun's, the sky's, the twilight's and
  the moon's light on them, the light through them near the sun or the moon); the share of dawn and sunset in the
  blend is the twilight's. Its rotation speeds turn the upper, base and rain layers (WEATHER adds up the turn). The
  layers' textures come from `texture_library.json`'s default cloud preset.

## From the scripts' attributes (`storm/environment/legacy.hpp`)

A game without the data («Каждому своё») sets its weather through the WEATHER entity's attributes and its sky through
the sky entity's folders; `EvaluateLegacy` makes the same state of them:

- **Sun and moon.** The hour's weather places its light (`Weather.Sun.HeightAngle`, `AzimuthAngle`, blended between
  the hours by the scripts) and says whether it is the moon (the sun glow's `Moon`): that body stands there, the other
  below the horizon; the sky's daylight follows the sun's elevation. The sun's and the moon's discs have the half
  sizes of the sun glow's sprites at their distance.
- **Colours.** The scripts' gamma-encoded sun, ambient and fog colours, squared; no fog when it is off. The sky
  scatters Caribbean Legend's engine's own radiances (the state's defaults), and its haze band is that game's:
  8.65° high, fading over all of it, in the sky's colour 1.42° up. The scripts' fog stays off the sky.
- **Clouds.** The sky folders, one per part of the day, say what each part's painted sky showed (`LegacySkyFolder`,
  a table by folder: the cover of fair-weather clouds, rain clouds for the storms'); between two parts the cover
  blends. The rain's drops add rain clouds (a full rain at 5000 drops); the sun's shadows fade as the rain clouds
  close. The clouds look and are lit by Caribbean Legend's engine's own periods (`EngineCloudPeriods`) at its
  own transition hours; their fill is the scripts' ambient in its units (a clear noon's is 1).

`tests/scene_environment` checks the evaluation on data written there and, with `$STORM_GAME_DIR`, every preset of
an installed game for every quarter hour.
