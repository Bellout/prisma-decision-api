// Endpoint counts: exact while they fit in 128 bits, approximate beyond.
//
// A count carries an exact unsigned 128-bit value and a long double
// approximation side by side. Arithmetic keeps both; the exact value is
// marked lost on the first overflow and the approximation carries on.
#pragma once

#include <string>

namespace dtcount {

using u128 = unsigned __int128;

class Count {
public:
    Count() = default;
    explicit Count(unsigned long long v) : exact_(v), approx_(static_cast<long double>(v)) {}

    static Count zero() { return Count(0); }
    static Count one() { return Count(1); }

    bool is_exact() const { return exact_ok_; }
    u128 exact() const { return exact_; }
    long double approx() const { return approx_; }
    bool is_zero() const { return exact_ok_ ? exact_ == 0 : approx_ == 0.0L; }

    Count& operator+=(const Count& o);
    Count& operator*=(const Count& o);
    friend Count operator+(Count a, const Count& b) { return a += b; }
    friend Count operator*(Count a, const Count& b) { return a *= b; }

    // Equal as counts: both exact and equal, or (when either is
    // approximate) equal to within a relative 1e-12.
    bool same_as(const Count& o) const;

    // Decimal digits of the exact value; empty when it is not exact.
    std::string to_decimal() const;
    // The approximation in scientific notation, 15 significant digits.
    std::string to_float_string() const;

private:
    u128 exact_ = 0;
    long double approx_ = 0.0L;
    bool exact_ok_ = true;
};

std::string u128_to_decimal(u128 v);

}  // namespace dtcount
