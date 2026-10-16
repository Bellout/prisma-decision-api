// The counting problem: what dtcount knows about a decision problem.
//
// Nothing here is specific to any decision tool. A problem is a list of
// variables — the decision and uncertainty nodes, in the order of the
// decision tree — and a list of 0/1 factors over them. A factor's entry is
// 1 where the combination of states can occur and 0 where it cannot: the
// zero pattern of a probability table, or the forbidden combinations of a
// restriction. The number of endpoints of the asymmetric tree is the
// number of complete assignments for which every factor is 1.
#pragma once

#include <cstdint>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dtcount/count.hpp"

namespace dtcount {

enum class VarKind { Decision, Uncertainty };

struct Variable {
    std::string name;
    VarKind kind = VarKind::Uncertainty;
    int states = 1;
};

enum class FactorKind { Probability, Restriction };

struct Factor {
    FactorKind kind = FactorKind::Probability;
    std::vector<int> scope;         // variable indices, as written
    std::vector<uint8_t> allowed;   // row-major over scope, last variable fastest
    std::vector<std::size_t> strides;  // stride of each scope position in `allowed`

    // Index into `allowed` for a full assignment (indexed by variable).
    std::size_t index(const std::vector<int>& assignment) const {
        std::size_t i = 0;
        for (std::size_t p = 0; p < scope.size(); ++p)
            i += strides[p] * static_cast<std::size_t>(assignment[scope[p]]);
        return i;
    }
    // The largest variable index in the scope (-1 for an empty scope).
    int last_variable() const;
};

struct Problem {
    std::vector<Variable> variables;
    std::vector<Factor> factors;

    int size() const { return static_cast<int>(variables.size()); }
    int decisions() const;
    int uncertainties() const;
    // The endpoint count of the symmetric tree: the product of state counts.
    Count symmetric_count() const;
    // Throws ProblemError if a factor is inconsistent with the variables.
    void validate() const;
};

class ProblemError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Reads the text format "dtcount-problem 1" (see README.md). Throws
// ProblemError with the line number on malformed input.
Problem read_problem(std::istream& in);
Problem read_problem_file(const std::string& path);
void write_problem(std::ostream& out, const Problem& problem);

}  // namespace dtcount
