#include <sstream>
#include <stdexcept>
#include <cassert>

#include <promxx/registry.hpp>

using namespace promxx;

#define ASSERT_THROW(EXPR, WHAT) try { \
    (EXPR); \
    assert(0); \
} catch (Error const& e) { \
    assert(e.what() == std::string(WHAT)); \
}

auto const expected_metrics =
"# HELP c1 Simple counter\n"
"# TYPE c1 counter\n"
"c1 3\n"
"# HELP c2 \n"
"# TYPE c2 counter\n"
"c2{l1=\"l1v1\",l2=\"l2v1\"} 1\n"
"c2{l1=\"l1v2\",l2=\"l2v2\"} 1\n"
"c2{l1=\"l1v2\",l2=\"l2v3\"} 0\n"
"# HELP g1 \n"
"# TYPE g1 gauge\n"
"g1 45\n"
"# HELP g2 Gauge with labels\n"
"# TYPE g2 gauge\n"
"g2{l1=\"v1\",l2=\"v2\",l3=\"v3\"} 0\n"
"# HELP h1 Simple histogram\n"
"# TYPE h1 histogram\n"
"h1_bucket{le=\"500\"} 1\n"
"h1_bucket{le=\"1500\"} 2\n"
"h1_bucket{le=\"2500\"} 3\n"
"h1_bucket{le=\"+Inf\"} 3\n"
"h1_sum 4500\n"
"h1_count 3\n"
"# HELP h2 Simple histogram with linear buckets\n"
"# TYPE h2 histogram\n"
"h2_bucket{le=\"500\"} 1\n"
"h2_bucket{le=\"1500\"} 2\n"
"h2_bucket{le=\"2500\"} 3\n"
"h2_bucket{le=\"+Inf\"} 3\n"
"h2_sum 4500\n"
"h2_count 3\n"
"# HELP h3 Simple histogram with exponential buckets\n"
"# TYPE h3 histogram\n"
"h3_bucket{le=\"10\"} 0\n"
"h3_bucket{le=\"100\"} 0\n"
"h3_bucket{le=\"1000\"} 1\n"
"h3_bucket{le=\"+Inf\"} 3\n"
"h3_sum 4500\n"
"h3_count 3\n"
"# HELP h4 Histogram with labels\n"
"# TYPE h4 histogram\n"
"h4_bucket{l1=\"l1v1\",l2=\"l2v2\",le=\"10\"} 0\n"
"h4_bucket{l1=\"l1v1\",l2=\"l2v2\",le=\"100\"} 0\n"
"h4_bucket{l1=\"l1v1\",l2=\"l2v2\",le=\"1000\"} 1\n"
"h4_bucket{l1=\"l1v1\",l2=\"l2v2\",le=\"+Inf\"} 3\n"
"h4_sum{l1=\"l1v1\",l2=\"l2v2\"} 4500\n"
"h4_count{l1=\"l1v1\",l2=\"l2v2\"} 3\n"
"h4_bucket{l1=\"l1v3\",l2=\"l2v4\",le=\"10\"} 0\n"
"h4_bucket{l1=\"l1v3\",l2=\"l2v4\",le=\"100\"} 0\n"
"h4_bucket{l1=\"l1v3\",l2=\"l2v4\",le=\"1000\"} 0\n"
"h4_bucket{l1=\"l1v3\",l2=\"l2v4\",le=\"+Inf\"} 0\n"
"h4_sum{l1=\"l1v3\",l2=\"l2v4\"} 0\n"
"h4_count{l1=\"l1v3\",l2=\"l2v4\"} 0\n";

int main()
{
    auto& c1 = add(Counter("c1", "Simple counter"));
    c1.inc();
    c1.inc(2);

    ASSERT_THROW(add(Gauge<unsigned>("c1")), "Metric 'c1' type is ambiguous")
    ASSERT_THROW(add(Counter("c1")), "Metric 'c1' has duplicate labels")

    Counter c2("c2", "", {"l1","l2"});
    auto& c2_l1v1_l2v1 = add(c2, {"l1v1","l2v1"});
    auto& c2_l1v2_l2v2 = add(c2, {"l1v2","l2v2"});
    add(c2, {"l1v2","l2v3"});
    c2_l1v1_l2v1.inc();
    c2_l1v2_l2v2.inc();

    {
        Counter c2("c2", "", {"l2","l1"});
        ASSERT_THROW(add(c2, {"l2v3","l1v2"}), "Metric 'c2' has duplicate labels")
    }
    ASSERT_THROW(Counter("c3", "", {"b","b"}), "Metric 'c3' has duplicate label names")

    auto& g1 = add(Gauge<int>("g1"));
    g1.set(42);
    g1.inc();
    g1.dec();
    g1.inc(8);
    g1.dec(5);

    Gauge<int> g2("g2", "Gauge with labels", {"l2","l3","l1"});
    auto& g2_v1_v2_v3 = add(g2, {"v2","v3","v1"});
    g2_v1_v2_v3.inc();
    g2_v1_v2_v3.dec();

    ASSERT_THROW(add(g2, {"v2","v3"}), "Key/value mismatch for metric 'g2'")

    auto& h1 = add(Histogram("h1", Buckets{500, 1500, 2500},
                             "Simple histogram"));
    h1.observe(500);
    h1.observe(1500);
    h1.observe(2500);

    auto& h2 = add(Histogram("h2", LinearBuckets{500, 1000, 3},
                             "Simple histogram with linear buckets"));
    h2.observe(500);
    h2.observe(1500);
    h2.observe(2500);

    auto& h3 = add(Histogram("h3", ExponentialBuckets{10, 10, 3},
                             "Simple histogram with exponential buckets"));
    h3.observe(500);
    h3.observe(1500);
    h3.observe(2500);

    Histogram h4("h4", ExponentialBuckets{10, 10, 3}, "Histogram with labels", {"l1","l2"});
    auto& h4_l1v1_l2v2 = add(h4, {"l1v1","l2v2"});
    h4_l1v1_l2v2.observe(500);
    h4_l1v1_l2v2.observe(1500);
    h4_l1v1_l2v2.observe(2500);

    add(h4, {"l1v3","l2v4"});

    ASSERT_THROW(Histogram("h5", Buckets{1}, "", {"le"}), "\"le\" is not allowed as label name in histogram");
    ASSERT_THROW(Histogram("h5", LinearBuckets{1, 2, 3}, "", {"le"}), "\"le\" is not allowed as label name in histogram");
    ASSERT_THROW(Histogram("h5", ExponentialBuckets{1, 2, 3}, "", {"le"}), "\"le\" is not allowed as label name in histogram");

    std::stringstream ss;
    Registry::global().flush(ss);
    assert(ss.str() == expected_metrics);
}
