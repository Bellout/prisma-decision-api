#include <functional>
#include <utility>

#include "dtcount/algorithm.hpp"

namespace dtcount {

namespace {

using Factory = std::function<std::unique_ptr<Algorithm>()>;

// Registration order: the reference first. Add new algorithms here.
const std::vector<std::pair<std::string, Factory>>& registry() {
    static const std::vector<std::pair<std::string, Factory>> entries = {
        {"enumerate", make_enumerate},
        {"context-cache", make_context_cache},
        {"elimination", make_elimination},
    };
    return entries;
}

}  // namespace

std::vector<std::string> algorithm_names() {
    std::vector<std::string> names;
    for (const auto& e : registry()) names.push_back(e.first);
    return names;
}

// Chosen by the M2 benchmark (bench/results/2026-10-16-host-laptop.md):
// worst 0.45 ms on 2000 corpus-sized models, against 57 ms (A1) and 294 ms (A0).
std::string default_algorithm() { return "elimination"; }

std::unique_ptr<Algorithm> make_algorithm(const std::string& name) {
    for (const auto& e : registry())
        if (e.first == name) return e.second();
    return nullptr;
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::TooLarge: return "too_large";
        case Status::Timeout: return "timeout";
    }
    return "unknown";
}

Result count_endpoints(const Problem& problem, const Algorithm& algorithm, const Limits& limits) {
    Result r;
    r.algorithm = algorithm.name();
    Budget budget(limits);
    r.cost_estimate = algorithm.estimate_cost(problem);
    r.table_estimate = algorithm.estimate_table(problem);
    r.predicted_ms = r.cost_estimate / algorithm.steps_per_second() * 1000.0;
    if (r.cost_estimate > limits.max_cost || r.table_estimate > limits.max_table ||
        (limits.precheck && r.predicted_ms > limits.budget_ms)) {
        r.status = Status::TooLarge;
    } else {
        try {
            r.endpoints = algorithm.run(problem, budget);
        } catch (const BudgetExceeded& e) {
            r.status = e.reason() == BudgetExceeded::Reason::Timeout ? Status::Timeout : Status::TooLarge;
        }
    }
    r.elapsed_ms = budget.elapsed_ms();
    r.steps = budget.steps();
    return r;
}

}  // namespace dtcount
