// A1 — context-cached search in the tree order.
//
// The number of endpoints below a tree node at depth k depends on the path
// that led there only through the path's *context*: the earlier variables
// that share a factor with x_k or with a later variable. Two tree nodes
// with the same context have identical subtrees (the decision-analysis
// literature calls merging them coalescence; Dechter & Mateescu, AND/OR
// search spaces, AIJ 2007, call it context caching). So one table per
// depth, indexed by the context's assignment, holds every subtree count,
// filled from the leaves upwards:
//
//   T_n[] = 1
//   T_k[a] = sum over states v of x_k allowed by the factors ending at k
//            of T_{k+1}[ (a, v) restricted to ctx(k+1) ]
//
// and the answer is T_0[]. Cost: sum_k |T_k| * d_k, known in advance.
#include <memory>

#include "dtcount/algorithm.hpp"

namespace dtcount {

namespace {

struct Layout {
    std::vector<std::vector<int>> context;  // ctx(k), k = 0..n, ascending
    std::vector<double> size;               // |T_k| as a double (may be huge)
};

Layout layout_of(const Problem& problem) {
    const int n = problem.size();
    // last[j] = the largest variable index sharing a factor with j (or j).
    std::vector<int> reach(static_cast<std::size_t>(n), -1);
    for (const auto& f : problem.factors) {
        int last = f.last_variable();
        for (int j : f.scope) reach[static_cast<std::size_t>(j)] = std::max(reach[static_cast<std::size_t>(j)], last);
    }
    Layout l;
    l.context.resize(static_cast<std::size_t>(n) + 1);
    l.size.assign(static_cast<std::size_t>(n) + 1, 1.0);
    for (int k = 0; k <= n; ++k)
        for (int j = 0; j < k; ++j)
            if (reach[static_cast<std::size_t>(j)] >= k) {
                l.context[static_cast<std::size_t>(k)].push_back(j);
                l.size[static_cast<std::size_t>(k)] *= problem.variables[static_cast<std::size_t>(j)].states;
            }
    return l;
}

class ContextCache final : public Algorithm {
public:
    std::string name() const override { return "context-cache"; }
    std::string description() const override {
        return "A1: context-cached search in the tree order (coalescence)";
    }

    // Measured 6.5e7 - 7.1e7 steps/s (bench/results/2026-10-16-host-laptop.md); conservative.
    double steps_per_second() const override { return 4.0e7; }

    double estimate_cost(const Problem& problem) const override {
        Layout l = layout_of(problem);
        double cost = 0.0;
        for (int k = 0; k < problem.size(); ++k)
            cost += l.size[static_cast<std::size_t>(k)] * problem.variables[static_cast<std::size_t>(k)].states;
        return cost;
    }

    double estimate_table(const Problem& problem) const override {
        Layout l = layout_of(problem);
        double largest = 0.0;
        for (double s : l.size) largest = std::max(largest, s);
        return largest;
    }

    Count run(const Problem& problem, Budget& budget) const override {
        const int n = problem.size();
        std::vector<std::vector<const Factor*>> at(static_cast<std::size_t>(n));
        for (const auto& f : problem.factors) {
            int last = f.last_variable();
            if (last < 0) {
                if (!f.allowed[0]) return Count::zero();
            } else {
                at[static_cast<std::size_t>(last)].push_back(&f);
            }
        }
        Layout l = layout_of(problem);
        for (double s : l.size) budget.check_table(s);

        std::vector<int> x(static_cast<std::size_t>(n), 0);
        std::vector<Count> next(1, Count::one());  // T_n
        for (int k = n - 1; k >= 0; --k) {
            const auto& ctx = l.context[static_cast<std::size_t>(k)];
            const auto& ctx_next = l.context[static_cast<std::size_t>(k) + 1];
            // Strides of T_{k+1} over ctx(k+1), last variable fastest.
            std::vector<std::size_t> stride_next(ctx_next.size(), 1);
            for (std::size_t p = ctx_next.size(); p-- > 1;)
                stride_next[p - 1] = stride_next[p] * static_cast<std::size_t>(
                                                          problem.variables[static_cast<std::size_t>(ctx_next[p])].states);
            const auto size = static_cast<std::size_t>(l.size[static_cast<std::size_t>(k)]);
            const int states = problem.variables[static_cast<std::size_t>(k)].states;
            std::vector<Count> cur(size, Count::zero());

            for (int j : ctx) x[static_cast<std::size_t>(j)] = 0;
            for (std::size_t a = 0; a < size; ++a) {
                Count sum = Count::zero();
                for (int v = 0; v < states; ++v) {
                    budget.step();
                    x[static_cast<std::size_t>(k)] = v;
                    bool ok = true;
                    for (const Factor* f : at[static_cast<std::size_t>(k)])
                        if (!f->allowed[f->index(x)]) {
                            ok = false;
                            break;
                        }
                    if (!ok) continue;
                    std::size_t i = 0;
                    for (std::size_t p = 0; p < ctx_next.size(); ++p)
                        i += stride_next[p] * static_cast<std::size_t>(x[static_cast<std::size_t>(ctx_next[p])]);
                    sum += next[i];
                }
                cur[a] = sum;
                // Odometer over ctx(k), last variable fastest (matches index a).
                for (std::size_t p = ctx.size(); p-- > 0;) {
                    int& digit = x[static_cast<std::size_t>(ctx[p])];
                    if (++digit < problem.variables[static_cast<std::size_t>(ctx[p])].states) break;
                    digit = 0;
                }
            }
            next.swap(cur);
        }
        return next[0];
    }
};

}  // namespace

std::unique_ptr<Algorithm> make_context_cache() { return std::make_unique<ContextCache>(); }

}  // namespace dtcount
