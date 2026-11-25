#include "../../src/libs/ship/src/track_sampling.hpp"
#include <cstdio>
#include <vector>

#include <catch2/catch.hpp>

TEST_CASE("ship_track_sampling")
{
    using storm::ship::TrackSpacing;
    using storm::ship::TrackWidth;
    std::vector<double> reference;
    for (int scale : {1, 2, 4})
    {
        TrackSpacing spacing;
        std::vector<double> born;
        double traveled = 0;
        for (int frame = 0; frame < 300 / scale; ++frame)
        {
            const float dt = scale / 30.0f, distance = 5 * dt;
            spacing.Advance(distance, .7f, 0, 0, [&](float along, float fraction, float yaw) {
                CHECK((fraction >= 0 && fraction <= 1 && yaw == 0));
                born.push_back((traveled + along) / 5);
            });
            traveled += distance;
        }
        CHECK((born.size() == 71 && std::abs(spacing.remainder - .3) < .00001));
        for (size_t i = 0; i < born.size(); ++i)
            CHECK(std::abs(born[i] - (i + 1) * .14) < .00001);
        if (reference.empty())
            reference = born;
        else
            for (size_t i = 0; i < born.size(); ++i)
                CHECK(std::abs(born[i] - reference[i]) < .00001);
        const auto count = born.size();
        spacing.Advance(0, .7f, 0, 0, [&](float, float, float) { born.push_back(-1); });
        CHECK(born.size() == count); // no foam emission at rest
        spacing.Reset();
        spacing.Advance(.6f, .7f, 0, 0, [&](float, float, float) { born.push_back(-1); });
        CHECK(born.size() == count); // scene/teleport reset discards a previous partial interval
        std::printf("foam x%d: %zu distance samples; max birth-time error <10 us\n", scale, count);
    }
    // Several samples in a turning frame have distinct headings and birth times. Interpolate the short way
    // through +/-pi, avoiding a half-turn fan in the ribbon.
    TrackSpacing turning;
    std::vector<float> fractions, angles;
    turning.Advance(3, 1, 3.1f, -3.1f, [&](float along, float fraction, float yaw) {
        CHECK(std::abs(along - fraction * 3) < 1e-6f);
        fractions.push_back(fraction);
        angles.push_back(yaw);
    });
    CHECK((fractions.size() == 3 && fractions[0] < fractions[1] && fractions[1] < fractions[2]));
    CHECK((angles.front() > 3.1f && angles.back() < 3.19f && angles[0] < angles[1]));
    CHECK(TrackWidth(12, -3, 10, 10) == 0); // a shrinking trail cannot invert its transverse texture mapping
    CHECK(TrackWidth(3, 2, 5, 10) == 10.5f);
    CHECK(TrackWidth(3, 2, 100, 10) == 13); // bounded integral through expiry
    CHECK(TrackWidth(3, 2, 0, 0) == 0);
    // Translating both the camera and hull must not lift/sink the same foam strip.
    // Stock AIShip near/far heights are .2/.9 m; a 10 km world offset exposed the origin bug.
    struct Position
    {
        float x, y, z;
    };
    const Position origin{0, 0, 0}, eye{30, 40, 0};
    const float close = storm::ship::TrackElevation(eye, origin, .2f, .9f);
    CHECK(std::abs(close - .2035f) < 1e-6f); // unchanged at world origin
    for (const Position shift : {Position{10000, 0, 0}, Position{-8000, 0, 6000}})
    {
        const Position shifted_eye{eye.x + shift.x, eye.y + shift.y, eye.z + shift.z};
        const float translated = storm::ship::TrackElevation(shifted_eye, shift, .2f, .9f);
        std::printf("foam world translation: original=%.6f shifted=%.6f\n", close, translated);
        CHECK(std::abs(translated - close) < 1e-6f);
        // Both production ribbons share this lift: Track1 transverse .4..1.4 profile,
        // Track2 uniform 1.0. Hold WaveXZ fixed to isolate the erroneous camera factor.
        for (float profile : {.4f, 1.4f, 1.0f})
            CHECK(std::abs((2.0f + translated * profile) - (2.0f + close * profile)) < 1e-6f);
    }
    const Position hull{10000, 0, -2000};
    CHECK(storm::ship::TrackElevation(hull, hull, .2f, .9f) == .2f);
    CHECK(std::abs(storm::ship::TrackElevation(Position{13000, 4000, -2000}, hull, .2f, .9f) - .55f) < 1e-6f);
    CHECK(std::abs(storm::ship::TrackElevation(Position{22000, 0, -2000}, hull, .2f, .9f) - .9f) < 1e-6f);
    CHECK(storm::ship::TrackElevation(eye, origin, .2f, .2f) == .2f);
    // The trail's foam breaks into patches: always within 0..1, a fresh strip keeps most of it, an old one has
    // holes (patches a few metres long: strips are 1 m apart and a strip keeps its pattern for life) and the two
    // edges differ.
    {
        using storm::ship::TrackFoamKeep;
        using storm::ship::TrackFoamPattern;
        float fresh_min = 1, old_min = 1, max_step = 0, edge_diff = 0;
        for (int strip = 0; strip < 400; ++strip)
            for (int x = 0; x < 12; ++x)
            {
                const float across = x / 11.0f, along = strip * .05f;
                const float fresh = TrackFoamKeep(TrackFoamPattern(across, along, 1.3f), 0);
                const float aged = TrackFoamKeep(TrackFoamPattern(across, along, 1.3f), 1);
                CHECK((fresh >= 0 && fresh <= 1 && aged >= 0 && aged <= 1));
                fresh_min = std::min(fresh_min, fresh);
                old_min = std::min(old_min, aged);
                max_step =
                    std::max(max_step, std::abs(aged - TrackFoamKeep(TrackFoamPattern(across, along + .05f, 1.3f), 1)));
                if (x == 0)
                    edge_diff =
                        std::max(edge_diff, std::abs(aged - TrackFoamKeep(TrackFoamPattern(1, along, 1.3f), 1)));
            }
        CHECK((fresh_min >= .54f && old_min < .05f && max_step < .8f && edge_diff > .5f));
        std::printf("foam breakup: fresh min %.2f, old min %.2f, step between strips %.2f, edges differ %.2f\n",
                    fresh_min, old_min, max_step, edge_diff);
    }
    // A strip's cross-section points, laid once, run edge to edge: the first at its left edge, the last at its right,
    // in order across, each with the pattern of its own place.
    for (const int count : {4, 12})
    {
        std::vector<storm::ship::TrackFoamColumn> columns(count);
        storm::ship::TrackFoamColumns(count, 2.35f, 0.7f, 0.18f, columns.begin());
        CHECK(columns.front().offset == storm::ship::TrackFoamOffset(0, 2.35f, 0.7f, 0.18f));
        CHECK(columns.back().offset == storm::ship::TrackFoamOffset(1, 2.35f, 0.7f, 0.18f));
        CHECK((columns.front().offset < 0 && columns.back().offset > 0));
        for (int i = 1; i < count; ++i)
            CHECK(columns[i - 1].offset < columns[i].offset);
        for (int i = 0; i < count; ++i)
            CHECK(columns[i].pattern == storm::ship::TrackFoamPattern(i / (count - 1.0f), 2.35f, 0.7f));
    }
    // A trail is left out of the frame only when its box lies wholly behind one side of the view: here a view from
    // the origin along +z, 90 degrees each way.
    {
        struct Plane
        {
            float Nx, Ny, Nz, D;
        };
        struct Vec
        {
            float x, y, z;
        };
        const float h = std::sqrt(.5f);
        const Plane view[4] = {{h, 0, h, 0}, {-h, 0, h, 0}, {0, -h, h, 0}, {0, h, h, 0}};
        const auto outside = [&](Vec lo, Vec hi) { return storm::ship::TrackBoxOutside(view, 4, lo, hi); };
        CHECK(!outside({-5, -1, 50}, {5, 1, 80}));     // ahead
        CHECK(outside({-5, -1, -80}, {5, 1, -50}));    // behind
        CHECK(outside({-90, -1, 10}, {-40, 1, 30}));   // left of the view
        CHECK(outside({40, -1, 10}, {90, 1, 30}));     // right of it
        CHECK(outside({-5, 40, 10}, {5, 60, 30}));     // above it
        CHECK(!outside({-40, -1, 10}, {-5, 1, 30}));   // across the left edge
        CHECK(!outside({-5, 5, 10}, {5, 40, 20}));     // across the top edge
        CHECK(!outside({-5, -1, -100}, {5, 1, 100}));  // from behind the camera to ahead of it
        CHECK(outside({-100, -1, -10}, {100, 1, -5})); // behind, wider than the view: behind its top plane
    }
}
