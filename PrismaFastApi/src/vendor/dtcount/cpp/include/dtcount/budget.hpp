// The time and size budget every algorithm runs under.
//
// Two limits: a cost ceiling checked against the algorithm's estimate
// before it starts (and against table sizes as it allocates), and a
// wall-clock deadline checked every 2^16 elementary steps while it runs.
// Exceeding either ends the run with a status instead of a count.
#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace dtcount {

struct Limits {
    double budget_ms = 3000.0;    // wall-clock deadline
    double max_cost = 1e300;      // optional absolute ceiling on elementary steps
    double max_table = 4194304;   // entries in any one cached table (2^22)
    bool precheck = true;         // refuse when the predicted time exceeds budget_ms
};

class BudgetExceeded : public std::runtime_error {
public:
    enum class Reason { Timeout, TooLarge };
    BudgetExceeded(Reason reason, const char* what) : std::runtime_error(what), reason_(reason) {}
    Reason reason() const { return reason_; }

private:
    Reason reason_;
};

class Budget {
public:
    explicit Budget(const Limits& limits)
        : limits_(limits),
          start_(std::chrono::steady_clock::now()),
          deadline_(start_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::chrono::duration<double, std::milli>(limits.budget_ms))) {}

    const Limits& limits() const { return limits_; }

    // Call once per elementary step; checks the clock every 2^16 steps.
    void step() {
        if ((++steps_ & 0xFFFF) == 0) check_clock();
    }
    void check_clock() const {
        if (std::chrono::steady_clock::now() > deadline_)
            throw BudgetExceeded(BudgetExceeded::Reason::Timeout, "deadline passed");
    }
    void check_table(double entries) const {
        if (entries > limits_.max_table)
            throw BudgetExceeded(BudgetExceeded::Reason::TooLarge, "table too large");
    }
    uint64_t steps() const { return steps_; }
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
            .count();
    }

private:
    Limits limits_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point deadline_;
    uint64_t steps_ = 0;
};

}  // namespace dtcount
