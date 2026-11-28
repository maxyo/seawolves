# Sea

The sea entity (`SEA`, `src/sea.cpp`): the water's surface, its reflection cubes, the heights ships and everything
else afloat read (`SEA_BASE::WaveXZ`), and the underwater view.

## Wave field (`src/ocean_*`)

The waves are a statistical wave field in the manner of Tessendorf's "Simulating Ocean Water" (2001):

- `ocean_spectrum` turns what a game's weather sets on the sea (`Sea.Sea2.*`, `Sea.MaxSeaHeight`) into two JONSWAP
  wave systems (Hasselmann et al. 1973; the directional form and cos-2s spreading as in Horvath 2015): a swell from
  the first Sea2 wave and a wind sea from the second, each with the significant height and peak wavelength the
  game's own height frames gave that wave. Both travel with the wind. The spectrum is spread over cascades — square
  patches of 997, 173 and 31 m, each owning one band of wavenumbers — as fixed Gaussian draws times the spectrum's
  square root, normalised on the samples so each system's height is exact.
- `ocean_field` moves every wave by deep-water dispersion (w = sqrt(g k)) to the sea's clock and transforms each
  cascade to heights and horizontal (choppy) displacements (`ocean_fft`: libavutil's `av_tx`). The field depends
  only on the weather's numbers and the clock. Cascades are independent, one thread each.
- The surface over a point is that of the displaced water landing there: `Field::Height` finds it by Newton steps
  on p + D(p) = x, the same samples and bilinear interpolation the renderer displaces the mesh with
  (`Field::Point`: the displacement and its derivatives, from which the normal).
- Each cascade's slopes (the height's and the displacement's own derivatives, central differences) are made with its
  samples, for the pixel shader.

## The sea over time (`src/ocean_driver`, `src/ocean_worker`)

`SEA::Realize` hands the driver the frame's time step, the weather's keys and the layout the graphics preset asks
(`storm::render::CurrentEffectsSea`: Низкая 2 cascades of 64², Средняя 3 of 64², Высокая 3 of 128², the option
`fx_sea_cascade_size` 256 on desktops). Two fields take turns: the one drawn and read by `WaveXZ` this frame, and
the one the worker thread makes meanwhile for the next frame (one helper per cascade), at the sea clock of this
frame. So everything afloat reads the field the screen shows, a frame behind the clock, as the old sea's frames
were. A weather change builds the new spectrum on the worker and eases into it over seconds: the draws are the same,
only each wave's amplitude moves from the old spectrum's to the new one's. A new layout, and any change while a
scene loads, is made at once. The wind: the waves travel downwind; the game's wind angle a blows towards
(sin a, cos a) (its flags and rain), which is the field's direction pi/2 - a.

The frame-work line of the renderer's log carries the worker's milliseconds per sea frame and the bytes uploaded
(`sea_field_ms`, the game thread's wait for it `sea_wait_ms`, `sea_field_kb`).

## On the GPU

- The samples, the cascades stacked in one RGBA32F texture (`include/sea_surface_gpu.hpp`), read with texelFetch in
  the vertex shader (`shaders/include/sea_surface.sh` of the renderer): a mesh point of the still water is moved by
  the sum of the cascades' bilinear samples and takes the normal of their derivatives, as `Field::Point`. RGBA32F
  sampled without filtering is core in OpenGL ES 3.0 and mandatory in Vulkan, the format the skinned draws' bone
  palettes use.
- The slopes, one slice per cascade of an RGBA16F volume, filtered in the pixel shader (RGBA16F is filterable in
  OpenGL ES 3.0 and Vulkan); each cascade's slopes fade as a pixel covers more of its samples.
- Before the vertex shader displaces the sea on a device, the sea checks it there (`storm/render/sea_surface_check`
  of the renderer): a fixed field of the same layout through the same shader code, read back and compared with
  `Field::Point`. Until it passed, and for good when it failed (an error in the log), the CPU displaces the
  vertices (`SEA::DisplaceBlock`) the same way.
- The coast foam strands (`island`) float on the same texture: their points move with the water.
