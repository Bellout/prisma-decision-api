// dtcount — count the endpoints of an asymmetric decision tree.
//
// Reads one problem ("dtcount-problem 1", see README.md) from a file or
// stdin and prints one JSON object. Exit status 0 for every counting
// outcome (ok, too_large, timeout); 2 for a usage or input error, with
// {"error": ...} on stdout and the message on stderr.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "dtcount/algorithm.hpp"

// A vendored copy carries the commit it was copied from (vendor.sh).
#if __has_include("dtcount/source_stamp.hpp")
#include "dtcount/source_stamp.hpp"
#endif
#ifndef DTCOUNT_SOURCE
#define DTCOUNT_SOURCE "unstamped"
#endif

namespace {

constexpr const char* kVersion = "1.0.0";

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string number(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

std::string result_fields(const dtcount::Result& r) {
    std::ostringstream o;
    const bool ok = r.status == dtcount::Status::Ok;
    o << "\"algorithm\": " << quote(r.algorithm) << ", \"status\": " << quote(dtcount::status_name(r.status))
      << ", \"ep\": " << (ok && r.endpoints.is_exact() ? quote(r.endpoints.to_decimal()) : "null")
      << ", \"ep_float\": " << (ok ? quote(r.endpoints.to_float_string()) : "null")
      << ", \"exact\": " << (ok && r.endpoints.is_exact() ? "true" : "false")
      << ", \"elapsed_ms\": " << number(r.elapsed_ms) << ", \"cost_estimate\": " << number(r.cost_estimate)
      << ", \"table_estimate\": " << number(r.table_estimate) << ", \"predicted_ms\": " << number(r.predicted_ms)
      << ", \"steps\": " << r.steps;
    return o.str();
}

std::string problem_fields(const dtcount::Problem& p) {
    std::ostringstream o;
    const dtcount::Count sym = p.symmetric_count();
    o << "\"nodes\": " << p.size() << ", \"decisions\": " << p.decisions()
      << ", \"uncertainties\": " << p.uncertainties() << ", \"factors\": " << p.factors.size()
      << ", \"ep_sym\": " << (sym.is_exact() ? quote(sym.to_decimal()) : "null")
      << ", \"ep_sym_float\": " << quote(sym.to_float_string());
    return o.str();
}

int usage_error(const std::string& message) {
    std::cerr << "dtcount: " << message << "\n";
    std::cout << "{\"dtcount\": " << quote(kVersion) << ", \"error\": " << quote(message) << "}\n";
    return 2;
}

void help() {
    std::cout << "usage: dtcount [--algo NAME|all] [--budget-ms MS] [--max-cost STEPS]\n"
                 "               [--max-table ENTRIES] [--no-precheck] FILE|-\n"
                 "       dtcount --list | --version | --help\n\n"
                 "Counts the endpoints of an asymmetric decision tree: the complete\n"
                 "assignments of a problem's variables for which every 0/1 factor is 1.\n"
                 "Prints one JSON object. Default algorithm: "
              << dtcount::default_algorithm() << ".\n";
}

}  // namespace

int main(int argc, char** argv) {
    dtcount::Limits limits;
    std::string algo = dtcount::default_algorithm();
    std::string path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) return nullptr;
            (void)flag;
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            help();
            return 0;
        } else if (a == "--version") {
            std::cout << "{\"dtcount\": " << quote(kVersion) << ", \"source\": " << quote(DTCOUNT_SOURCE)
                      << "}\n";
            return 0;
        } else if (a == "--list") {
            std::cout << "{\"default\": " << quote(dtcount::default_algorithm()) << ", \"algorithms\": [";
            bool first = true;
            for (const auto& name : dtcount::algorithm_names()) {
                auto alg = dtcount::make_algorithm(name);
                std::cout << (first ? "" : ", ") << "{\"name\": " << quote(name)
                          << ", \"description\": " << quote(alg->description()) << "}";
                first = false;
            }
            std::cout << "]}\n";
            return 0;
        } else if (a == "--no-precheck") {
            limits.precheck = false;
        } else if (a == "--algo") {
            const char* v = value("--algo");
            if (!v) return usage_error("--algo needs a name");
            algo = v;
        } else if (a == "--budget-ms" || a == "--max-cost" || a == "--max-table") {
            const char* v = value(a.c_str());
            char* end = nullptr;
            double d = v ? std::strtod(v, &end) : 0.0;
            if (!v || *end != '\0' || d <= 0) return usage_error(a + " needs a positive number");
            (a == "--budget-ms" ? limits.budget_ms : a == "--max-cost" ? limits.max_cost : limits.max_table) = d;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            return usage_error("unknown option " + a);
        } else if (path.empty()) {
            path = a;
        } else {
            return usage_error("more than one input file");
        }
    }
    if (path.empty()) return usage_error("no input file (use - for stdin)");

    dtcount::Problem problem;
    try {
        problem = path == "-" ? dtcount::read_problem(std::cin) : dtcount::read_problem_file(path);
    } catch (const dtcount::ProblemError& e) {
        return usage_error(e.what());
    }

    std::vector<std::string> names;
    if (algo == "all") {
        names = dtcount::algorithm_names();
    } else {
        if (!dtcount::make_algorithm(algo)) return usage_error("unknown algorithm " + algo);
        names.push_back(algo);
    }

    std::vector<dtcount::Result> results;
    for (const auto& name : names) results.push_back(dtcount::count_endpoints(problem, *dtcount::make_algorithm(name), limits));

    std::cout << "{\"dtcount\": " << quote(kVersion) << ", " << problem_fields(problem) << ", \"budget_ms\": "
              << number(limits.budget_ms) << ", ";
    if (names.size() == 1) {
        std::cout << result_fields(results[0]) << "}\n";
        return 0;
    }
    // --algo all: every algorithm that finished must report the same count.
    bool agree = true;
    const dtcount::Result* first_ok = nullptr;
    for (const auto& r : results) {
        if (r.status != dtcount::Status::Ok) continue;
        if (!first_ok)
            first_ok = &r;
        else if (!r.endpoints.same_as(first_ok->endpoints))
            agree = false;
    }
    std::cout << "\"agree\": " << (agree ? "true" : "false") << ", \"results\": [";
    for (std::size_t i = 0; i < results.size(); ++i) std::cout << (i ? ", " : "") << "{" << result_fields(results[i]) << "}";
    std::cout << "]}\n";
    return 0;
}
