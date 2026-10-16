// The algorithm interface and the registry of competing implementations.
//
// Every algorithm answers the same question — the number of complete
// assignments for which every factor is 1 — and must agree with the
// reference enumerator on the differential suite before it may become the
// default. To add one: implement Algorithm, register it in registry.cpp,
// and run `dtcount --algo all` plus the pytest suite.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "dtcount/budget.hpp"
#include "dtcount/count.hpp"
#include "dtcount/problem.hpp"

namespace dtcount {

class Algorithm {
public:
    virtual ~Algorithm() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    // Elementary steps the run will take (an upper bound for enumeration,
    // exact for the table-based algorithms). Computed without counting.
    virtual double estimate_cost(const Problem& problem) const = 0;
    // Calibrated elementary steps per second on the reference machine
    // (bench/results), used to predict the run time before running.
    virtual double steps_per_second() const = 0;
    // The largest table the run will allocate, in entries (0 if none).
    virtual double estimate_table(const Problem& problem) const { (void)problem; return 0.0; }
    // Counts; throws BudgetExceeded when the budget runs out.
    virtual Count run(const Problem& problem, Budget& budget) const = 0;
};

std::unique_ptr<Algorithm> make_enumerate();      // A0
std::unique_ptr<Algorithm> make_context_cache();  // A1
std::unique_ptr<Algorithm> make_elimination();    // A2

// Names in registration order; the first is the reference (A0).
std::vector<std::string> algorithm_names();
std::string default_algorithm();
// nullptr for an unknown name.
std::unique_ptr<Algorithm> make_algorithm(const std::string& name);

enum class Status { Ok, TooLarge, Timeout };
const char* status_name(Status status);

struct Result {
    std::string algorithm;
    Status status = Status::Ok;
    Count endpoints;
    double cost_estimate = 0.0;
    double table_estimate = 0.0;
    double predicted_ms = 0.0;
    double elapsed_ms = 0.0;
    uint64_t steps = 0;
};

// Predicts the run time from the cost estimate and refuses (TooLarge)
// when it exceeds the budget, or the cost or table ceilings; otherwise
// runs under the deadline.
Result count_endpoints(const Problem& problem, const Algorithm& algorithm, const Limits& limits);

}  // namespace dtcount
