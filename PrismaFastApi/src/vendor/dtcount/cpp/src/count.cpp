#include "dtcount/count.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dtcount {

namespace {
constexpr u128 kMax = ~static_cast<u128>(0);
}

Count& Count::operator+=(const Count& o) {
    approx_ += o.approx_;
    if (exact_ok_ && o.exact_ok_) {
        if (exact_ > kMax - o.exact_)
            exact_ok_ = false;
        else
            exact_ += o.exact_;
    } else {
        exact_ok_ = false;
    }
    return *this;
}

Count& Count::operator*=(const Count& o) {
    approx_ *= o.approx_;
    if (exact_ok_ && o.exact_ok_) {
        if (exact_ != 0 && o.exact_ > kMax / exact_)
            exact_ok_ = false;
        else
            exact_ *= o.exact_;
    } else if (is_zero() || o.is_zero()) {
        // Zero times anything is an exact zero.
        exact_ = 0;
        approx_ = 0.0L;
        exact_ok_ = true;
    } else {
        exact_ok_ = false;
    }
    return *this;
}

bool Count::same_as(const Count& o) const {
    if (exact_ok_ && o.exact_ok_) return exact_ == o.exact_;
    long double a = approx_, b = o.approx_;
    long double scale = std::max(std::fabs(a), std::fabs(b));
    return scale == 0.0L || std::fabs(a - b) <= 1e-12L * scale;
}

std::string u128_to_decimal(u128 v) {
    if (v == 0) return "0";
    std::string s;
    while (v > 0) {
        s.push_back(static_cast<char>('0' + static_cast<int>(v % 10)));
        v /= 10;
    }
    std::reverse(s.begin(), s.end());
    return s;
}

std::string Count::to_decimal() const { return exact_ok_ ? u128_to_decimal(exact_) : std::string(); }

std::string Count::to_float_string() const {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.15Lg", approx_);
    return buf;
}

}  // namespace dtcount
