#ifndef PROMXX_HPP
#define PROMXX_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

namespace promxx
{

using Unsigned = unsigned long long;

namespace detail
{

struct NoCopyMove
{
    NoCopyMove() = default;
    NoCopyMove(NoCopyMove const&) = delete;
    NoCopyMove& operator = (NoCopyMove const&) = delete;
    NoCopyMove(NoCopyMove&&) = delete;
    NoCopyMove& operator = (NoCopyMove&&) = delete;
};

template<class T>
struct AtomicValue: NoCopyMove
{
    std::atomic<T> v_{0};
};

using KeyValueI = std::pair<std::string, std::size_t>;

class MetricMeta
{
    friend class Metric;

    std::string name_;
    std::vector<KeyValueI> keys_;
    std::string help_;

public:
    MetricMeta(std::string name, std::vector<std::string> keys, std::string help);
};

template<class T>
struct AutoBuckets
{
    Unsigned start;
    double delta;
    std::size_t count;
};

}; // namespace detail

class Counter: public detail::MetricMeta
{
public:
    Counter(std::string name,
            std::string help = {},
            std::vector<std::string> keys = {})
        : detail::MetricMeta(std::move(name), std::move(keys), std::move(help)) {}
};

template<class T>
class Gauge: public detail::MetricMeta
{
public:
    Gauge(std::string name,
          std::string help = {},
          std::vector<std::string> keys = {})
        : detail::MetricMeta(std::move(name), std::move(keys), std::move(help)) {}
};

using Buckets = std::vector<Unsigned>;
using LinearBuckets = detail::AutoBuckets<class Linear>;
using ExponentialBuckets = detail::AutoBuckets<class Exponential>;

class Histogram: public detail::MetricMeta
{
    Buckets bounds_;

public:
    Histogram(std::string name, Buckets bounds,
              std::string help = {},
              std::vector<std::string> keys = {});

    Histogram(std::string name, LinearBuckets lb,
              std::string help = {},
              std::vector<std::string> keys = {});

    Histogram(std::string name, ExponentialBuckets eb,
              std::string help = {},
              std::vector<std::string> keys = {});

    Buckets const& bounds() const noexcept { return bounds_; }
};

//
// Counter
// https://prometheus.io/docs/concepts/metric_types/#counter
//
class ICounter: protected detail::AtomicValue<Unsigned>
{
public:
    void inc(Unsigned d = 1) noexcept { this->v_ += d; }
};

//
// Gauge
// https://prometheus.io/docs/concepts/metric_types/#gauge
//
template<class T>
class IGauge: protected detail::AtomicValue<T>
{
public:
    void inc(T d = 1) noexcept { this->v_ += d; }
    void dec(T d = 1) noexcept { this->v_ -= d; }
    void set(T v) noexcept { this->v_ = v; }
};

//
// Histogram
// https://prometheus.io/docs/concepts/metric_types/#histogram
//
class IHistogram: detail::NoCopyMove
{
public:
    void observe(Unsigned v) noexcept;

protected:
    IHistogram(Buckets const& bounds);

    std::vector<std::pair<Unsigned, Unsigned>> buckets_;
    Unsigned sum_ = 0;
    Unsigned count_ = 0;
    mutable std::mutex mtx_;
};

namespace detail
{

class Metric
{
    std::string name_;
    std::string type_;
    std::string help_;
    std::string labels_;

protected:
    std::ostream& header(std::ostream& os,
                         std::string const& suffix = {},
                         std::string const& extkey = {}) const;

public:
    Metric(std::string type, MetricMeta const& mm,
           std::vector<std::string> const& values);

    virtual ~Metric();

    std::string const& name() const noexcept { return name_; }
    std::string const& type() const noexcept { return type_; }
    std::string const& help() const noexcept { return help_; }
    std::string const& labels() const noexcept { return labels_; }

    virtual void flush(std::ostream& os) const = 0;
};

template<class T>
struct MetricImpl;

template<>
struct MetricImpl<Counter> final: Metric, ICounter
{
    using base = ICounter;

    MetricImpl(Counter const& c, std::vector<std::string> const& values)
        : Metric("counter", c, values) {}

    void flush(std::ostream& os) const override
    {
        header(os) << ' ' << this->v_ << '\n';
    }
};

template<class T>
struct MetricImpl<Gauge<T>> final: Metric, IGauge<T>
{
    using base = IGauge<T>;

    MetricImpl(Gauge<T> const& g, std::vector<std::string> const& values)
        : Metric("gauge", g, values) {}

    void flush(std::ostream& os) const override
    {
        header(os) << ' ' << this->v_ << '\n';
    }
};

template<>
struct MetricImpl<Histogram> final: Metric, IHistogram
{
    using base = IHistogram;

    MetricImpl(Histogram const& h, std::vector<std::string> const& values)
        : Metric("histogram", h, values)
        , IHistogram(h.bounds())
    {}

    void flush(std::ostream& os) const override;
};

} // namespace detail

class Registry: detail::NoCopyMove
{
    struct Data;
    std::unique_ptr<Data> data_;

    void push(std::shared_ptr<detail::Metric> m);

public:
    static Registry& global();

    Registry();
    ~Registry();

    template<class T>
    typename detail::MetricImpl<T>::base &
    add(T const& t, std::vector<std::string> const& values = {})
    {
        auto m = std::make_shared<detail::MetricImpl<T>>(t, values);
        push(m);
        return *m;
    }

    void flush(std::ostream& os) const;
};

template<class T>
typename detail::MetricImpl<T>::base &
add(T const& t, std::vector<std::string> const& values = {})
{
    return Registry::global().add(t, values);
}

} // namespace promxx

#endif