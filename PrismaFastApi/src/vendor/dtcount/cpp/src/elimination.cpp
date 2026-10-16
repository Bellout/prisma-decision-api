// A2 — bucket elimination in the counting semiring.
//
// The endpoint count is the sum, over every complete assignment, of the
// product of the 0/1 factors. Sums and products distribute, so variables
// can be summed out one at a time: multiply the factors that mention x,
// sum x out, put the resulting table back. The order is free (the count
// does not depend on the tree's order), so it is chosen by the min-fill
// heuristic on the factors' interaction graph. Cost: for each eliminated
// variable, the size of the table over its bucket's scope times the number
// of factors multiplied — computed in advance by the same simulation.
#include <algorithm>
#include <memory>
#include <set>

#include "dtcount/algorithm.hpp"

namespace dtcount {

namespace {

struct Plan {
    std::vector<int> order;
    double cost = 0.0;
    double largest = 0.0;
};

Plan plan_of(const Problem& problem) {
    const int n = problem.size();
    std::vector<std::set<int>> adj(static_cast<std::size_t>(n));
    std::vector<std::set<int>> scopes;
    for (const auto& f : problem.factors) {
        std::set<int> s(f.scope.begin(), f.scope.end());
        for (int a : s)
            for (int b : s)
                if (a != b) adj[static_cast<std::size_t>(a)].insert(b);
        scopes.push_back(std::move(s));
    }
    auto states = [&](int v) { return static_cast<double>(problem.variables[static_cast<std::size_t>(v)].states); };
    std::vector<bool> gone(static_cast<std::size_t>(n), false);
    Plan plan;
    for (int step = 0; step < n; ++step) {
        int best = -1;
        long best_fill = 0;
        std::size_t best_degree = 0;
        for (int v = 0; v < n; ++v) {
            if (gone[static_cast<std::size_t>(v)]) continue;
            const auto& nb = adj[static_cast<std::size_t>(v)];
            long fill = 0;
            for (auto i = nb.begin(); i != nb.end(); ++i)
                for (auto j = std::next(i); j != nb.end(); ++j)
                    if (!adj[static_cast<std::size_t>(*i)].count(*j)) ++fill;
            if (best < 0 || fill < best_fill || (fill == best_fill && nb.size() < best_degree)) {
                best = v;
                best_fill = fill;
                best_degree = nb.size();
            }
        }
        // Simulate the bucket of `best` on the scopes.
        std::set<int> bucket{best};
        double factors = 0.0;
        std::vector<std::set<int>> kept;
        for (auto& s : scopes) {
            if (s.count(best)) {
                bucket.insert(s.begin(), s.end());
                ++factors;
            } else {
                kept.push_back(std::move(s));
            }
        }
        double size = 1.0;
        for (int v : bucket) size *= states(v);
        plan.cost += size * std::max(1.0, factors);
        plan.largest = std::max(plan.largest, size / states(best));
        bucket.erase(best);
        kept.push_back(bucket);
        scopes.swap(kept);
        // Eliminate `best` from the graph: connect its neighbours.
        const auto nb = adj[static_cast<std::size_t>(best)];
        for (int a : nb) {
            adj[static_cast<std::size_t>(a)].erase(best);
            for (int b : nb)
                if (a != b) adj[static_cast<std::size_t>(a)].insert(b);
        }
        adj[static_cast<std::size_t>(best)].clear();
        gone[static_cast<std::size_t>(best)] = true;
        plan.order.push_back(best);
    }
    return plan;
}

struct Table {
    std::vector<int> scope;  // ascending
    std::vector<std::size_t> strides;
    std::vector<Count> value;

    std::size_t index(const std::vector<int>& x) const {
        std::size_t i = 0;
        for (std::size_t p = 0; p < scope.size(); ++p) i += strides[p] * static_cast<std::size_t>(x[static_cast<std::size_t>(scope[p])]);
        return i;
    }
};

Table make_table(const Problem& problem, std::vector<int> scope) {
    Table t;
    t.scope = std::move(scope);
    t.strides.assign(t.scope.size(), 1);
    std::size_t size = 1;
    for (std::size_t p = t.scope.size(); p-- > 0;) {
        t.strides[p] = size;
        size *= static_cast<std::size_t>(problem.variables[static_cast<std::size_t>(t.scope[p])].states);
    }
    t.value.assign(size, Count::zero());
    return t;
}

class Elimination final : public Algorithm {
public:
    std::string name() const override { return "elimination"; }
    std::string description() const override {
        return "A2: bucket elimination in the counting semiring, min-fill order";
    }
    // Measured 3.2e7 - 8.3e7 steps/s (bench/results/2026-10-16-host-laptop.md); conservative.
    double steps_per_second() const override { return 2.5e7; }
    double estimate_cost(const Problem& problem) const override { return plan_of(problem).cost; }
    double estimate_table(const Problem& problem) const override { return plan_of(problem).largest; }

    Count run(const Problem& problem, Budget& budget) const override {
        const Plan plan = plan_of(problem);
        budget.check_table(plan.largest);

        std::vector<Table> tables;
        for (const auto& f : problem.factors) {
            std::vector<int> sorted = f.scope;
            std::sort(sorted.begin(), sorted.end());
            Table t = make_table(problem, sorted);
            // Copy the 0/1 entries across, re-indexed to the sorted scope.
            std::vector<int> x(static_cast<std::size_t>(problem.size()), 0);
            for (std::size_t i = 0; i < f.allowed.size(); ++i) {
                std::size_t rest = i;
                for (std::size_t p = 0; p < f.scope.size(); ++p) {
                    x[static_cast<std::size_t>(f.scope[p])] = static_cast<int>(rest / f.strides[p]);
                    rest %= f.strides[p];
                }
                t.value[t.index(x)] = f.allowed[i] ? Count::one() : Count::zero();
            }
            tables.push_back(std::move(t));
        }

        std::vector<int> x(static_cast<std::size_t>(problem.size()), 0);
        for (int var : plan.order) {
            std::vector<Table> bucket, kept;
            std::set<int> scope;
            for (auto& t : tables) {
                if (std::find(t.scope.begin(), t.scope.end(), var) != t.scope.end()) {
                    scope.insert(t.scope.begin(), t.scope.end());
                    bucket.push_back(std::move(t));
                } else {
                    kept.push_back(std::move(t));
                }
            }
            scope.erase(var);
            Table out = make_table(problem, std::vector<int>(scope.begin(), scope.end()));
            const int states = problem.variables[static_cast<std::size_t>(var)].states;
            for (int j : out.scope) x[static_cast<std::size_t>(j)] = 0;
            for (std::size_t a = 0; a < out.value.size(); ++a) {
                Count sum = Count::zero();
                for (int v = 0; v < states; ++v) {
                    budget.step();
                    x[static_cast<std::size_t>(var)] = v;
                    Count product = Count::one();
                    for (const auto& t : bucket) {
                        const Count& c = t.value[t.index(x)];
                        if (c.is_zero()) {
                            product = Count::zero();
                            break;
                        }
                        product *= c;
                    }
                    sum += product;
                }
                out.value[a] = sum;
                for (std::size_t p = out.scope.size(); p-- > 0;) {
                    int& digit = x[static_cast<std::size_t>(out.scope[p])];
                    if (++digit < problem.variables[static_cast<std::size_t>(out.scope[p])].states) break;
                    digit = 0;
                }
            }
            kept.push_back(std::move(out));
            tables.swap(kept);
        }
        Count result = Count::one();
        for (const auto& t : tables) result *= t.value[0];  // all scopes are empty now
        return result;
    }
};

}  // namespace

std::unique_ptr<Algorithm> make_elimination() { return std::make_unique<Elimination>(); }

}  // namespace dtcount
