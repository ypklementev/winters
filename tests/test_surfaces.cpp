#include "mini_test.h"
#include "sim/surfaces.h"
#include "test_helpers.h"

using namespace sim;

TEST(spans_subtract) {
    std::vector<Span> s{{0, 100}};
    s = subtractSpan(s, 20, 30);
    REQUIRE(s.size() == 2);
    CHECK(s[0].left == 0 && s[0].right == 20);
    CHECK(s[1].left == 30 && s[1].right == 100);
    s = subtractSpan(s, -10, 25);
    REQUIRE(s.size() == 1);
    CHECK(s[0].left == 30);
    s = subtractSpan(s, 0, 1000);
    CHECK(s.empty());
}

TEST(edge_spans_clip_to_monitors_and_occluders) {
    std::vector<Monitor> mons{th::makeMonitor(0, 0, 1000, 800)};
    // Edge partly off-screen on the right, one window above covers 300..500.
    std::vector<RectF> occ{{300, 100, 500, 400}};
    auto spans = visibleEdgeSpans(200, 1200, 200, occ, mons, 6);
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].left == 200 && spans[0].right == 300);
    CHECK(spans[1].left == 500 && spans[1].right == 1000);
}

TEST(edge_spans_occluder_must_cross_the_edge) {
    std::vector<Monitor> mons{th::makeMonitor(0, 0, 1000, 800)};
    // Entirely below the edge: doesn't hide it. Entirely above with a gap: neither.
    std::vector<RectF> occ{{0, 300, 1000, 400}, {0, 50, 1000, 150}};
    auto spans = visibleEdgeSpans(0, 1000, 200, occ, mons, 6);
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].right - spans[0].left == 1000);
    // Bottom touching the edge (window sitting on top) does hide it.
    occ = {{100, 50, 200, 200}};
    spans = visibleEdgeSpans(0, 1000, 200, occ, mons, 6);
    CHECK(spans.size() == 2);
}

TEST(edge_spans_maximized_window_has_no_edge) {
    std::vector<Monitor> mons{th::makeMonitor(0, 0, 1000, 800)};
    CHECK(visibleEdgeSpans(0, 1000, 0, {}, mons, 6).empty());
    CHECK(visibleEdgeSpans(0, 1000, 3, {}, mons, 6).empty());
    CHECK(!visibleEdgeSpans(0, 1000, 20, {}, mons, 6).empty());
}

TEST(edge_spans_merge_adjacent_monitors) {
    std::vector<Monitor> mons{th::makeMonitor(0, 0, 1000, 800), th::makeMonitor(1000, 0, 2000, 800)};
    auto spans = visibleEdgeSpans(500, 1500, 300, {}, mons, 6);
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].left == 500 && spans[0].right == 1500);
}
