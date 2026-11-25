#include "effects/loading_upload.hpp"
#include <cstdio>
#include <cstdlib>

#include <catch2/catch.hpp>

using namespace storm::effects;
TEST_CASE("loading_upload")
{
    const LoadingUploadScope none{7, false};
    REQUIRE((!none.active() && !none.accepts(7, true))); // no presented loading cover grants no loading batch
    REQUIRE(!LoadingUploadScope{0, true}.active());      // no load grants no loading batch
    const LoadingUploadScope scope{7, true};
    REQUIRE((scope.active() && scope.accepts(7, true))); // the presented cover admits the selected entries of this load
    REQUIRE(
        (!scope.accepts(6, true) && !scope.accepts(7, false))); // old scene and nonselected entry get no extra uploads

    UploadBatch ordinary(false);
    REQUIRE(ordinary.allows(100, 0)); // first gameplay upload permitted
    ordinary.submitted(100);
    REQUIRE(ordinary.allows(100, 9000)); // second gameplay upload preserves prior policy
    ordinary.submitted(100);
    REQUIRE(!ordinary.allows(1, 0)); // gameplay remains exactly two uploads even with pending work
    UploadBatch bytes(true);
    bytes.submitted(UploadBatch::kLoadingBytes - 10);
    REQUIRE(bytes.allows(10, 0)); // exact remaining loading byte budget permitted
    bytes.submitted(10);
    REQUIRE(!bytes.allows(1, 0)); // next byte beyond loading budget deferred
    UploadBatch time(true);
    time.submitted(1);
    REQUIRE((time.allows(1, 3.999) && !time.allows(1, 4.0) &&
             !time.allows(1, 1000))); // loading batch stops at CPU-submit deadline between nonpreemptible uploads
    UploadBatch count(true);
    for (unsigned i = 0; i < UploadBatch::kLoadingUploads; ++i)
    {
        REQUIRE(count.allows(1, 0)); // bounded loading count advances
        count.submitted(1);
    }
    REQUIRE(!count.allows(1, 0)); // hard texture count limits zero-time tiny uploads
    UploadBatch oversized(true);
    REQUIRE(oversized.allows(UploadBatch::kLoadingBytes + 1, 1000)); // oversized singleton cannot deadlock readiness
    oversized.submitted(UploadBatch::kLoadingBytes + 1);
    REQUIRE(!oversized.allows(1, 0)); // oversized singleton ends loading batch
    std::puts("PASS: loading upload admission, byte/time/count bounds, singleton progress, gameplay cap");
}
