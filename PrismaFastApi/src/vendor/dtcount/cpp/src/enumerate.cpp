// A0 — the reference enumerator.
//
// Walks the decision tree depth-first in the problem's variable order and
// counts the leaves it reaches. A factor is checked as soon as its last
// variable is assigned, so a branch that cannot occur is cut where it
// starts, exactly as the asymmetric tree omits it. Time is proportional to
// the number of tree nodes visited (at most the symmetric tree's), which
// is why this is the oracle and not the default.
#include <memory>

#include "dtcount/algorithm.hpp"

namespace dtcount {

namespace {

class Enumerate final : public Algorithm {
public:
    std::string name() const override { return "enumerate"; }
    std::string description() const override {
        return "A0 reference: depth-first walk of the asymmetric tree, one step per tree node";
    }

    // Measured 2.8e8 - 4.7e8 steps/s (bench/results/2026-10-16-host-laptop.md); conservative.
    double steps_per_second() const override { return 2.5e8; }

    double estimate_cost(const Problem& problem) const override {
        // Nodes of the symmetric tree below the root: sum over depths of
        // the product of the state counts so far. An upper bound.
        double nodes = 0.0, width = 1.0;
        for (const auto& v : problem.variables) {
            width *= v.states;
            nodes += width;
        }
        return nodes;
    }

    Count run(const Problem& problem, Budget& budget) const override {
        State s{problem, budget, {}, std::vector<int>(problem.variables.size(), 0), 0};
        s.at.resize(problem.variables.size());
        for (const auto& f : problem.factors) {
            int last = f.last_variable();
            if (last < 0) {
                if (!f.allowed[0]) return Count::zero();
            } else {
                s.at[static_cast<std::size_t>(last)].push_back(&f);
            }
        }
        visit(s, 0);
        flush(s);
        return s.leaves;
    }

private:
    struct State {
        const Problem& problem;
        Budget& budget;
        std::vector<std::vector<const Factor*>> at;  // factors by their last variable
        std::vector<int> assignment;
        unsigned long long small = 0;  // leaves since the last flush into `leaves`
        Count leaves = Count::zero();
    };

    static void flush(State& s) {
        s.leaves += Count(s.small);
        s.small = 0;
    }

    static void visit(State& s, int k) {
        if (k == s.problem.size()) {
            if (++s.small == ~0ULL) flush(s);
            return;
        }
        const auto& factors = s.at[static_cast<std::size_t>(k)];
        const int states = s.problem.variables[static_cast<std::size_t>(k)].states;
        for (int v = 0; v < states; ++v) {
            s.budget.step();
            s.assignment[static_cast<std::size_t>(k)] = v;
            bool ok = true;
            for (const Factor* f : factors)
                if (!f->allowed[f->index(s.assignment)]) {
                    ok = false;
                    break;
                }
            if (ok) visit(s, k + 1);
        }
    }
};

}  // namespace

std::unique_ptr<Algorithm> make_enumerate() { return std::make_unique<Enumerate>(); }

}  // namespace dtcount
