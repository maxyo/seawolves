// Matrix builder contract over math_api.h (GLM and storm::math builders).
#ifdef GLM_ENABLE_EXPERIMENTAL
#define STORM_TEST_CALLER_EXPERIMENTAL
#endif
#include "math_api.h"
#if defined(GLM_ENABLE_EXPERIMENTAL) && !defined(STORM_TEST_CALLER_EXPERIMENTAL)
#error "The math boundary leaked GLM experimental opt-in into the caller"
#endif
#include "sample_log.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include <catch2/catch.hpp>

using mt::Mat4;
using mt::Vec3;

static void near(double actual, double expected, const char *reason)
{
    INFO(reason);
    REQUIRE(std::abs(actual - expected) <= 5e-5 * (1 + std::abs(expected)));
}
static void vectorNear(const Vec3 &actual, const Vec3 &expected, const char *reason)
{
    near(actual.x, expected.x, reason);
    near(actual.y, expected.y, reason);
    near(actual.z, expected.z, reason);
}
static void matrixNear(const Mat4 &actual, const Mat4 &expected, const char *reason)
{
    for (int i = 0; i < 16; ++i)
        near(mt::data(actual)[i], mt::data(expected)[i], reason);
}
static void contracts()
{
    const float x = .37f, y = -.61f, z = .83f;
    const Mat4 rx = mt::rotationX(x), ry = mt::rotationY(y), rz = mt::rotationZ(z);
    Mat4 built = mt::eulerMatrix(x, y, z);
    matrixNear(built, mt::mulRow(mt::mulRow(rz, rx), ry), "legacy ZXY Euler composition");
    built = mt::eulerMatrix(x, y, z, 7, 8, 9);
    vectorNear(mt::transformPoint(built, Vec3(0.f)), Vec3(7.f, 8.f, 9.f), "Euler position survives rotation");
    matrixNear(mt::translation(7, 8, 9), mt::eulerMatrix(0, 0, 0, 7, 8, 9), "translation builder");
    vectorNear(mt::transformPoint(mt::scaling(2, 3, 4), Vec3(1.f, 1.f, 1.f)), Vec3(2.f, 3.f, 4.f), "scale builder");

    Mat4 mirror = mt::mirrorMatrix(0, 1, 0, 2);
    REQUIRE(std::signbit(mt::at(mirror, 1, 0))); // mirror retains negative-zero off-diagonal
    vectorNear(mt::transformPoint(mirror, Vec3(1.f, 5.f, 3.f)), Vec3(1.f, -1.f, 3.f), "plane D convention");
    vectorNear(mt::transformPoint(mirror, Vec3(7.f, 2.f, -4.f)), Vec3(7.f, 2.f, -4.f),
               "points on mirror plane stay fixed");
    matrixNear(mt::mulRow(mirror, mirror), mt::identity(), "normalized reflection is involutory");
    mirror = mt::mirrorMatrix(0, 2, 0, 3);
    vectorNear(mt::transformPoint(mirror, Vec3(1.f, 5.f, 3.f)), Vec3(1.f, -23.f, 3.f),
               "nonunit normal must not be normalized");

    Mat4 view;
    REQUIRE(!mt::viewMatrix(view, Vec3(2.f, 3.f, 4.f), Vec3(2.f, 3.f, 4.f), Vec3(0.f))); // coincident eye/target status
    vectorNear(mt::transformPoint(view, Vec3(2.f, 3.f, 4.f)), Vec3(0.f), "coincident eye/target translation");
    REQUIRE(
        mt::viewMatrix(view, Vec3(0.f), Vec3(0.f, 1.f, 0.f), Vec3(0.f, 1.f, 0.f))); // parallel up remains successful
    matrixNear(view, mt::identity(), "parallel vertical up retains identity basis");
    REQUIRE(mt::viewMatrix(view, Vec3(0.f, 0.f, -5.f), Vec3(0.f), Vec3(0.f))); // zero up fallback status
    vectorNear(mt::transformPoint(view, Vec3(0.f)), Vec3(0.f, 0.f, 5.f), "zero up fallback looks along positive Z");

    Mat4 inversePosition = mt::eulerMatrix(x, y, z);
    mt::setInversePosition(inversePosition, 7, 8, 9);
    vectorNear(mt::transformPoint(inversePosition, Vec3(7.f, 8.f, 9.f)), Vec3(0.f),
               "inverse position cancels basis transform");
    Mat4 shifted = mt::eulerMatrix(x, y, z, 3, 4, 5);
    mt::moveInversePosition(shifted, 7, 8, 9);
    vectorNear(mt::transformPoint(shifted, Vec3(7.f, 8.f, 9.f)), Vec3(3.f, 4.f, 5.f),
               "inverse move retains previous translation");

    Mat4 projection = mt::projectionMatrix(1.2f, 1920, 1080, .25f, 250.f);
    near(mt::data(projection)[0], 1 / std::tan(1.2f * .5f), "legacy horizontal FOV");
    near(mt::data(projection)[5], 1 / std::tan((1080.f / 1920.f) * 1.2f * .5f), "linear vertical FOV policy");
    Vec3 points[] = {Vec3(0.f, 0.f, .25f), Vec3(0.f, 0.f, 250.f)};
    mt::ProjPoint output[2];
    mt::projectPoints(projection, points, output, 2, 960, 540, sizeof(Vec3), sizeof(mt::ProjPoint));
    near(output[0].z, 0, "LH D3D near depth zero");
    near(output[1].z, 1, "LH D3D far depth one");
    projection = mt::projectionMatrix(1.2f, 1920, 1080, 1, 1);
    REQUIRE((std::isinf(mt::data(projection)[10]) &&
             std::isinf(mt::data(projection)[14]))); // equal depth planes retain infinities
    REQUIRE((mt::data(projection)[3] == 0 && mt::data(projection)[11] == 1 &&
             mt::data(projection)[15] == 0)); // singular projection fixed fields
}
static std::size_t randomized()
{
    std::mt19937 random(0x4255494c);
    std::uniform_real_distribution<float> angle(-30.f, 30.f), coordinate(-100.f, 100.f);
    SampleLog log;
    auto record = [&](const Mat4 &matrix, SampleLog::Where at = SampleLog::Where::current()) {
        log.floats(mt::data(matrix), 16, at);
    };
    auto point = [&] {
        const float x = coordinate(random), y = coordinate(random), z = coordinate(random);
        return Vec3(x, y, z);
    };
    for (int i = 0; i < 4000; ++i)
    {
        const float ax = angle(random), ay = angle(random), az = angle(random);
        const auto position = point();
        Mat4 matrix;
        matrix = mt::eulerMatrix(ax, ay, az, position.x, position.y, position.z);
        record(matrix);
        matrix = mt::rotationX(ax);
        record(matrix);
        matrix = mt::rotationY(ay);
        record(matrix);
        matrix = mt::rotationZ(az);
        record(matrix);
        mt::setInversePosition(matrix, position.x, position.y, position.z);
        record(matrix);
        mt::moveInversePosition(matrix, position.z, position.x, position.y);
        record(matrix);
        const auto normal = point();
        const float planeDistance = coordinate(random);
        matrix = mt::mirrorMatrix(normal.x, normal.y, normal.z, planeDistance);
        record(matrix);
        const auto target = point(), up = point();
        log.scalar(mt::viewMatrix(matrix, position, target, up));
        record(matrix);
        // Include random non-affine bases for the generic inverse-position helpers.
        for (int j = 0; j < 16; ++j)
            mt::data(matrix)[j] = coordinate(random);
        mt::setInversePosition(matrix, position.x, position.y, position.z);
        record(matrix);
        mt::moveInversePosition(matrix, position.z, position.x, position.y);
        record(matrix);
        const float nearPlane = .1f + .01f * std::abs(coordinate(random));
        const float farPlane = nearPlane + .5f + 100 * std::abs(coordinate(random));
        const float viewAngle = .1f + .05f * std::abs(angle(random));
        const float width = 100 + std::abs(coordinate(random));
        const float height = 100 + std::abs(coordinate(random));
        matrix = mt::projectionMatrix(viewAngle, width, height, nearPlane, farPlane);
        record(matrix);
        const float sx = coordinate(random), sy = coordinate(random), sz = coordinate(random);
        matrix = mt::scaling(sx, sy, sz);
        record(matrix);
        matrix = mt::translation(position.x, position.y, position.z);
        record(matrix);
    }
    // Degenerate, parallel, near-parallel, huge and non-finite inputs.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (Vec3 from : {Vec3(0.f), Vec3(1.f, 2.f, 3.f)})
    {
        for (Vec3 direction :
             {Vec3(0.f), Vec3(0.f, 1.f, 0.f), Vec3(1e-12f, 1.f, 0.f), Vec3(0.f, 0.f, 1.f), Vec3(1e20f), Vec3(nan)})
        {
            for (Vec3 up : {Vec3(0.f), Vec3(0.f, 1.f, 0.f), Vec3(0.f, 0.f, 1.f), Vec3(1e-12f, 1.f, 0.f)})
            {
                Mat4 view;
                log.scalar(mt::viewMatrix(view, from, from + direction, up));
                record(view);
            }
        }
    }
    for (float angle : {0.f, 1e-20f, 1e10f, inf, nan})
    {
        record(mt::rotationX(angle));
        record(mt::rotationY(angle));
        record(mt::rotationZ(angle));
        record(mt::eulerMatrix(angle, .2f, -.3f, 4, 5, 6));
    }
    for (float near : {0.f, 1.f, -1.f, inf, nan})
    {
        for (float far : {0.f, 1.f, -1.f, inf, nan})
            record(mt::projectionMatrix(1.2f, 1920, 1080, near, far));
    }
    return log.values.size();
}
TEST_CASE("glm_math.builders_check")
{
    contracts();
    const std::size_t samples = randomized();
    std::cout << "PASS: Euler order, view degeneracy, mirror plane, legacy FOV/LH depth and " << samples
              << " randomized builder samples\n";
}
