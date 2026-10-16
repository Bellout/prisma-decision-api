// The text format "dtcount-problem 1".
//
//   dtcount-problem 1
//   # comment lines and blank lines are ignored
//   var <name> <D|U> <states>                 one per variable, in tree order
//   factor <P|R> <k> <i1> ... <ik> <bits>     bits: prod(states) characters of 0/1,
//                                             row-major over the scope, last fastest
//
// Variables are referred to by their 0-based position among the `var`
// lines. A factor with k = 0 has a single bit: 0 makes the whole problem
// empty. Names are single tokens and are only echoed, never interpreted.
#include <algorithm>
#include <fstream>
#include <sstream>

#include "dtcount/problem.hpp"

namespace dtcount {

namespace {

[[noreturn]] void fail(int line, const std::string& message) {
    throw ProblemError("line " + std::to_string(line) + ": " + message);
}

}  // namespace

int Factor::last_variable() const {
    int last = -1;
    for (int v : scope) last = std::max(last, v);
    return last;
}

int Problem::decisions() const {
    int n = 0;
    for (const auto& v : variables) n += v.kind == VarKind::Decision;
    return n;
}

int Problem::uncertainties() const {
    int n = 0;
    for (const auto& v : variables) n += v.kind == VarKind::Uncertainty;
    return n;
}

Count Problem::symmetric_count() const {
    Count c = Count::one();
    for (const auto& v : variables) c *= Count(static_cast<unsigned long long>(v.states));
    return c;
}

void Problem::validate() const {
    for (std::size_t i = 0; i < variables.size(); ++i)
        if (variables[i].states < 1)
            throw ProblemError("variable " + variables[i].name + " has no states");
    for (std::size_t f = 0; f < factors.size(); ++f) {
        const Factor& factor = factors[f];
        std::size_t entries = 1;
        for (std::size_t p = 0; p < factor.scope.size(); ++p) {
            int v = factor.scope[p];
            if (v < 0 || v >= size())
                throw ProblemError("factor " + std::to_string(f) + " names variable " +
                                   std::to_string(v) + ", which does not exist");
            for (std::size_t q = 0; q < p; ++q)
                if (factor.scope[q] == v)
                    throw ProblemError("factor " + std::to_string(f) + " names variable " +
                                       std::to_string(v) + " twice");
            entries *= static_cast<std::size_t>(variables[v].states);
        }
        if (factor.allowed.size() != entries || factor.strides.size() != factor.scope.size())
            throw ProblemError("factor " + std::to_string(f) + " has " +
                               std::to_string(factor.allowed.size()) + " entries, expected " +
                               std::to_string(entries));
    }
}

Problem read_problem(std::istream& in) {
    Problem problem;
    std::string text;
    int line = 0;
    bool header = false;
    while (std::getline(in, text)) {
        ++line;
        std::istringstream ls(text);
        std::string word;
        if (!(ls >> word) || word[0] == '#') continue;
        if (!header) {
            int version = 0;
            if (word != "dtcount-problem" || !(ls >> version))
                fail(line, "expected the header 'dtcount-problem 1'");
            if (version != 1) fail(line, "unsupported format version " + std::to_string(version));
            header = true;
            continue;
        }
        if (word == "var") {
            Variable v;
            std::string kind;
            if (!(ls >> v.name >> kind >> v.states)) fail(line, "expected: var <name> <D|U> <states>");
            if (kind == "D")
                v.kind = VarKind::Decision;
            else if (kind == "U")
                v.kind = VarKind::Uncertainty;
            else
                fail(line, "variable kind must be D or U, not '" + kind + "'");
            if (v.states < 1) fail(line, "a variable needs at least one state");
            if (!problem.factors.empty()) fail(line, "all var lines must come before the factors");
            problem.variables.push_back(v);
        } else if (word == "factor") {
            Factor f;
            std::string kind;
            int k = 0;
            if (!(ls >> kind >> k) || k < 0) fail(line, "expected: factor <P|R> <k> <scope...> <bits>");
            if (kind == "P")
                f.kind = FactorKind::Probability;
            else if (kind == "R")
                f.kind = FactorKind::Restriction;
            else
                fail(line, "factor kind must be P or R, not '" + kind + "'");
            f.scope.resize(static_cast<std::size_t>(k));
            for (int& v : f.scope) {
                if (!(ls >> v)) fail(line, "scope shorter than k");
                if (v < 0 || v >= problem.size())
                    fail(line, "scope names variable " + std::to_string(v) + ", which does not exist");
            }
            std::string bits;
            if (!(ls >> bits)) fail(line, "missing the 0/1 table");
            std::size_t size = 1;
            f.strides.assign(f.scope.size(), 1);
            for (std::size_t p = f.scope.size(); p-- > 0;) {
                f.strides[p] = size;
                size *= static_cast<std::size_t>(problem.variables[f.scope[p]].states);
            }
            if (bits.size() != size)
                fail(line, "table has " + std::to_string(bits.size()) + " entries, expected " +
                               std::to_string(size));
            f.allowed.resize(size);
            for (std::size_t i = 0; i < size; ++i) {
                if (bits[i] != '0' && bits[i] != '1') fail(line, "table entries must be 0 or 1");
                f.allowed[i] = bits[i] == '1';
            }
            problem.factors.push_back(std::move(f));
        } else {
            fail(line, "unknown line '" + word + "'");
        }
    }
    if (!header) throw ProblemError("empty input: expected the header 'dtcount-problem 1'");
    problem.validate();
    return problem;
}

Problem read_problem_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ProblemError("cannot open " + path);
    return read_problem(in);
}

void write_problem(std::ostream& out, const Problem& problem) {
    out << "dtcount-problem 1\n";
    for (const auto& v : problem.variables)
        out << "var " << v.name << ' ' << (v.kind == VarKind::Decision ? 'D' : 'U') << ' '
            << v.states << '\n';
    for (const auto& f : problem.factors) {
        out << "factor " << (f.kind == FactorKind::Probability ? 'P' : 'R') << ' ' << f.scope.size();
        for (int v : f.scope) out << ' ' << v;
        out << ' ';
        for (uint8_t a : f.allowed) out << (a ? '1' : '0');
        out << '\n';
    }
}

}  // namespace dtcount
