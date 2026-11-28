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
  on p + D(p) = x, the same samples and bilinear interpolation the renderer displaces the mesh with.
