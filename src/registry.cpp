#include <promxx/registry.hpp>

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <memory>

namespace promxx
{
namespace
{

std::string const LE = "le";
std::string const QUANTILE = "quantile";

std::vector<std::string> histogram_keys(std::vector<std::string> keys)
{
    if (std::find(keys.begin(), keys.end(), LE) != keys.end())
        throw Error{"\"le\" is not allowed as label name in histogram"};
    return keys;
}

} // namespace

Histogram::Histogram(std::string name, Buckets bounds,
                     std::string help, std::vector<std::string> keys)
    : detail::MetricMeta(name, histogram_keys(std::move(keys)), std::move(help))
    , bounds_(std::move(bounds))
{
    if (!bounds_.empty()) {
        auto next = bounds_.begin();
        auto prev = next++;
        while (next != bounds_.end())
            if (!(*prev++ < *next++))
                throw Error{"Histogram '" + name + "' buckets must be in increasing order"};
    }
}

Histogram::Histogram(std::string name, LinearBuckets lb,
                     std::string help, std::vector<std::string> keys)
    : detail::MetricMeta(name, histogram_keys(std::move(keys)), std::move(help))
{
    if (lb.delta < 1)
        throw Error{"Histogram '" + name + "' delta must be not less than 1"};
    if (lb.count > 0) {
        Unsigned le = lb.start;
        bounds_.reserve(lb.count);
        bounds_.push_back(le);
        for (std::size_t i = 1; i < lb.count; ++i) {
            if (le > Unsigned(-1) - lb.delta)
                throw Error{"Histogram '" + name + "' boundaries overflow"};
            le += lb.delta;
            bounds_.push_back(le);
        }
    }
}

Histogram::Histogram(std::string name, ExponentialBuckets eb,
                     std::string help, std::vector<std::string> keys)
    : detail::MetricMeta(name, histogram_keys(std::move(keys)), std::move(help))
{
    if (eb.delta <= 1)
        throw Error{"Histogram '" + name + "' delta must be greater than 1"};
    if (eb.count > 0) {
        Unsigned le = eb.start;
        bounds_.reserve(eb.count);
        bounds_.push_back(le);
        for (std::size_t i = 1; i < eb.count; ++i) {
            if (le > std::floor(Unsigned(-1) / eb.delta))
                throw Error{"Histogram '" + name + "' boundaries overflow"};
            le = std::floor(le * eb.delta);
            if (le == bounds_.back())
                throw Error{"Histogram '" + name + "' got duplicate buckets, try to increase the delta"};
            bounds_.push_back(le);
        }
    }
}

void IHistogram::observe(Unsigned v) noexcept
{
    std::lock_guard<std::mutex> lock{mtx_};
    sum_ += v;
    count_ += 1;
    // TODO reset all if sum or count overflows
    auto it = std::lower_bound(buckets_.begin(), buckets_.end(), v,
        [](std::pair<Unsigned, Unsigned> b, Unsigned x){
            return b.first < x;
        });
    for (; it != buckets_.end(); ++it)
        it->second += 1;
}

IHistogram::IHistogram(Buckets const& bounds)
{
    buckets_.reserve(bounds.size());
    for (auto le: bounds)
        buckets_.emplace_back(le, 0);
}

namespace detail
{
namespace
{

std::string const _BUCKET = "_bucket";
std::string const _SUM = "_sum";
std::string const _COUNT = "_count";

bool key_value_i_lt(KeyValueI const& lhs, KeyValueI const& rhs)
{ return lhs.first < rhs.first; }

bool key_value_i_eq(KeyValueI const& lhs, KeyValueI const& rhs)
{ return lhs.first == rhs.first; }

} // namespace

Metric::Metric(std::string type, MetricMeta const& mm,
               std::vector<std::string> const& values)
    : name_(mm.name_)
    , type_(std::move(type))
    , help_(mm.help_)
{
    if (mm.keys_.size() != values.size())
        throw Error{"Key/value mismatch for metric '" + name_ + "'"};

    auto it = mm.keys_.begin();
    if (it != mm.keys_.end()) {
        labels_ += it->first;
        labels_ += "=\"";
        labels_ += values.at(it->second);
        labels_ += '"';
        while (++it != mm.keys_.end()) {
            labels_ += ',';
            labels_ += it->first;
            labels_ += "=\"";
            labels_ += values.at(it->second);
            labels_ += '"';
        }
    }
}

Metric::~Metric() = default;

std::ostream& Metric::header(std::ostream& os, std::string const& suffix,
                             std::string const& extkey) const
{
    os << name_ << suffix;
    if (!labels_.empty() && !extkey.empty())
        os << '{' << labels_ << ',' << extkey;
    else if (!labels_.empty())
        os << '{' << labels_ << '}'; // header is ready
    else if (!extkey.empty())
        os << '{' << extkey;
    return os;
}

void MetricImpl<Histogram>::flush(std::ostream& os) const
{
    std::lock_guard<std::mutex> lock{mtx_};
    for (auto b: buckets_)
        header(os, _BUCKET, LE) << "=\"" << b.first << "\"} " << b.second << '\n';
    header(os, _BUCKET, LE) << "=\"+Inf\"} " << count_ << '\n';
    header(os, _SUM) << ' ' << sum_ << '\n';
    header(os, _COUNT) << ' ' << count_ << '\n';
}

MetricMeta::MetricMeta(std::string name, std::vector<std::string> keys,
                       std::string help)
    : name_(std::move(name))
    , help_(std::move(help))
{
    keys_.reserve(keys.size());
    for (auto& k: keys)
        keys_.emplace_back(std::move(k), keys_.size());

    std::sort(keys_.begin(), keys_.end(), &key_value_i_lt);
    if (std::adjacent_find(keys_.begin(), keys_.end(), &key_value_i_eq) != keys_.end())
        throw Error{"Metric '" + name_ + "' has duplicate label names"};
}

} // namespace detail

struct Registry::Data
{
    using MetricList = std::list<std::unique_ptr<detail::Metric>>;
    std::map<std::string, MetricList> map_;
    std::mutex mtx_;
};

Registry& Registry::global()
{
    static Registry r;
    return r;
}

Registry::Registry()
{
    data_ = new Data();
}

Registry::~Registry()
{
    delete data_;
}

void Registry::push(detail::Metric *ptr)
{
    std::unique_ptr<detail::Metric> m{ptr};
    std::lock_guard<std::mutex > lock{data_->mtx_};

    auto r = data_->map_.emplace(m->name(), Data::MetricList());
    auto& list = r.first->second;
    if (!list.empty()) {
        if (m->type() != list.front()->type())
            throw Error{"Metric '" + m->name() + "' type is ambiguous"};
        for (auto const& v: list)
            if (m->labels() == v->labels())
                throw Error{"Metric '" + m->name() + "' has duplicate labels"};
    }
    list.push_back(std::move(m));
}

void Registry::flush(std::ostream& os) const
{
    std::lock_guard<std::mutex> lock{data_->mtx_};

    for (auto& kv: data_->map_) {
        auto& list = kv.second;
        // write header from the first metric in the group
        auto& m = list.front();
        os << "# HELP " << m->name() << ' ' << m->help() << '\n';
        os << "# TYPE " << m->name() << ' ' << m->type() << '\n';
        for (auto& m: list)
            m->flush(os);
    }
}

} // namespace promxx