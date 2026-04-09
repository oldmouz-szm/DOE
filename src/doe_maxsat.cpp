#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string Trim(const std::string &s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

std::vector<std::string> Split(const std::string &s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, delim)) {
        out.push_back(Trim(tok));
    }
    return out;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

bool StartsWith(const std::string &s, const std::string &prefix) {
    return s.rfind(prefix, 0) == 0;
}

enum class GateType {
    INPUT,
    BUF,
    NOT,
    AND,
    NAND,
    OR,
    NOR,
    XOR,
    XNOR,
    UNDEF
};

GateType ParseGateType(const std::string &s) {
    if (s == "BUF" || s == "BUFF") return GateType::BUF;
    if (s == "NOT") return GateType::NOT;
    if (s == "AND") return GateType::AND;
    if (s == "NAND") return GateType::NAND;
    if (s == "OR") return GateType::OR;
    if (s == "NOR") return GateType::NOR;
    if (s == "XOR") return GateType::XOR;
    if (s == "XNOR") return GateType::XNOR;
    return GateType::UNDEF;
}

bool IsGateTypeComponent(GateType t) {
    return t != GateType::INPUT && t != GateType::UNDEF;
}

std::optional<int> ControllingValue(GateType t) {
    if (t == GateType::AND || t == GateType::NAND) return 0;
    if (t == GateType::OR || t == GateType::NOR) return 1;
    return std::nullopt;
}

struct Node {
    std::string name;
    GateType type = GateType::UNDEF;
    std::vector<int> fanins;
    std::vector<int> fanouts;
    bool is_input_decl = false;
    bool is_output_decl = false;
};

Node MakeDefaultNode(const std::string &name) {
    Node n;
    n.name = name;
    return n;
}

struct Circuit {
    std::vector<Node> nodes;
    std::unordered_map<std::string, int> name_to_id;
    std::vector<int> input_ids;
    std::vector<int> output_ids;

    int EnsureNode(const std::string &name) {
        auto it = name_to_id.find(name);
        if (it != name_to_id.end()) {
            return it->second;
        }
        int id = static_cast<int>(nodes.size());
        nodes.push_back(MakeDefaultNode(name));
        name_to_id[name] = id;
        return id;
    }

    int GetNode(const std::string &name) const {
        auto it = name_to_id.find(name);
        if (it == name_to_id.end()) {
            return -1;
        }
        return it->second;
    }
};

Circuit ParseBench(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open bench file: " + path);
    }
    Circuit c;
    std::string line;
    while (std::getline(in, line)) {
        auto hash_pos = line.find('#');
        if (hash_pos != std::string::npos) {
            line = line.substr(0, hash_pos);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        if (StartsWith(line, "INPUT(")) {
            auto l = line.find('(');
            auto r = line.find(')');
            if (l == std::string::npos || r == std::string::npos || r <= l + 1) {
                throw std::runtime_error("Malformed INPUT line: " + line);
            }
            std::string n = Trim(line.substr(l + 1, r - l - 1));
            int id = c.EnsureNode(n);
            c.nodes[id].type = GateType::INPUT;
            c.nodes[id].is_input_decl = true;
            continue;
        }

        if (StartsWith(line, "OUTPUT(")) {
            auto l = line.find('(');
            auto r = line.find(')');
            if (l == std::string::npos || r == std::string::npos || r <= l + 1) {
                throw std::runtime_error("Malformed OUTPUT line: " + line);
            }
            std::string n = Trim(line.substr(l + 1, r - l - 1));
            int id = c.EnsureNode(n);
            c.nodes[id].is_output_decl = true;
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("Malformed gate line: " + line);
        }
        std::string lhs = Trim(line.substr(0, eq));
        std::string rhs = Trim(line.substr(eq + 1));

        auto l = rhs.find('(');
        auto r = rhs.rfind(')');
        if (l == std::string::npos || r == std::string::npos || r <= l + 1) {
            throw std::runtime_error("Malformed RHS gate line: " + line);
        }

        std::string op = Trim(rhs.substr(0, l));
        auto gt = ParseGateType(op);
        if (gt == GateType::UNDEF) {
            throw std::runtime_error("Unsupported gate type: " + op);
        }

        std::string args = rhs.substr(l + 1, r - l - 1);
        auto fanin_names = Split(args, ',');
        if (fanin_names.empty()) {
            throw std::runtime_error("Gate without fanins: " + line);
        }

        int gid = c.EnsureNode(lhs);
        c.nodes[gid].type = gt;
        c.nodes[gid].fanins.clear();
        for (const auto &fn : fanin_names) {
            int fid = c.EnsureNode(fn);
            c.nodes[gid].fanins.push_back(fid);
        }
    }

    for (int i = 0; i < static_cast<int>(c.nodes.size()); ++i) {
        c.nodes[i].fanouts.clear();
    }

    for (int gid = 0; gid < static_cast<int>(c.nodes.size()); ++gid) {
        auto &g = c.nodes[gid];
        if (!IsGateTypeComponent(g.type)) {
            continue;
        }
        for (int fid : g.fanins) {
            c.nodes[fid].fanouts.push_back(gid);
        }
    }

    c.input_ids.clear();
    c.output_ids.clear();
    for (int i = 0; i < static_cast<int>(c.nodes.size()); ++i) {
        if (c.nodes[i].is_input_decl) c.input_ids.push_back(i);
        if (c.nodes[i].is_output_decl) c.output_ids.push_back(i);
    }

    if (c.input_ids.empty() || c.output_ids.empty()) {
        throw std::runtime_error("Circuit has no inputs or outputs: " + path);
    }

    return c;
}

std::vector<int> TopologicalOrder(const Circuit &c) {
    std::vector<int> indeg(c.nodes.size(), 0);
    for (int i = 0; i < static_cast<int>(c.nodes.size()); ++i) {
        if (!IsGateTypeComponent(c.nodes[i].type)) continue;
        for (int fi : c.nodes[i].fanins) {
            (void)fi;
            indeg[i]++;
        }
    }

    std::vector<int> q;
    q.reserve(c.nodes.size());
    for (int i = 0; i < static_cast<int>(c.nodes.size()); ++i) {
        if (indeg[i] == 0) q.push_back(i);
    }

    std::vector<int> order;
    order.reserve(c.nodes.size());
    size_t head = 0;
    while (head < q.size()) {
        int u = q[head++];
        order.push_back(u);
        for (int v : c.nodes[u].fanouts) {
            indeg[v]--;
            if (indeg[v] == 0) q.push_back(v);
        }
    }
    return order;
}

bool EvalGate(GateType t, const std::vector<int> &in_vals) {
    if (t == GateType::BUF) return in_vals.at(0);
    if (t == GateType::NOT) return !in_vals.at(0);
    if (t == GateType::AND) {
        return std::all_of(in_vals.begin(), in_vals.end(), [](int v) { return v == 1; });
    }
    if (t == GateType::NAND) {
        return !std::all_of(in_vals.begin(), in_vals.end(), [](int v) { return v == 1; });
    }
    if (t == GateType::OR) {
        return std::any_of(in_vals.begin(), in_vals.end(), [](int v) { return v == 1; });
    }
    if (t == GateType::NOR) {
        return !std::any_of(in_vals.begin(), in_vals.end(), [](int v) { return v == 1; });
    }
    if (t == GateType::XOR) {
        if (in_vals.size() != 2) {
            throw std::runtime_error("XOR currently supports arity 2 only");
        }
        return in_vals[0] ^ in_vals[1];
    }
    if (t == GateType::XNOR) {
        if (in_vals.size() != 2) {
            throw std::runtime_error("XNOR currently supports arity 2 only");
        }
        return !(in_vals[0] ^ in_vals[1]);
    }
    throw std::runtime_error("EvalGate on invalid type");
}

std::optional<int> TryInferGateValue(GateType t, const std::vector<std::optional<int>> &in_vals) {
    if (t == GateType::BUF) {
        if (in_vals[0].has_value()) return in_vals[0].value();
        return std::nullopt;
    }
    if (t == GateType::NOT) {
        if (in_vals[0].has_value()) return 1 - in_vals[0].value();
        return std::nullopt;
    }

    if (t == GateType::AND || t == GateType::NAND || t == GateType::OR || t == GateType::NOR) {
        auto ctrl = ControllingValue(t).value();
        bool has_ctrl = false;
        bool all_known = true;
        for (const auto &ov : in_vals) {
            if (!ov.has_value()) {
                all_known = false;
                continue;
            }
            if (ov.value() == ctrl) {
                has_ctrl = true;
            }
        }

        if (has_ctrl) {
            if (t == GateType::AND) return 0;
            if (t == GateType::NAND) return 1;
            if (t == GateType::OR) return 1;
            if (t == GateType::NOR) return 0;
        }

        if (all_known) {
            std::vector<int> vals;
            vals.reserve(in_vals.size());
            for (const auto &ov : in_vals) vals.push_back(ov.value());
            return EvalGate(t, vals) ? 1 : 0;
        }
        return std::nullopt;
    }

    if (t == GateType::XOR || t == GateType::XNOR) {
        if (in_vals.size() != 2) {
            throw std::runtime_error("XOR/XNOR currently supports arity 2 only");
        }
        if (!in_vals[0].has_value() || !in_vals[1].has_value()) {
            return std::nullopt;
        }
        std::vector<int> vals = {in_vals[0].value(), in_vals[1].value()};
        return EvalGate(t, vals) ? 1 : 0;
    }

    return std::nullopt;
}

struct Observation {
    std::string id;
    std::unordered_map<int, int> inputs;
    std::unordered_map<int, int> outputs;
};

Observation MakeObservation(const std::string &id) {
    Observation o;
    o.id = id;
    return o;
}

bool IsDigits(const std::string &s) {
    if (s.empty()) return false;
    for (char ch : s) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

int ResolveSignalName(const std::string &raw_name,
                      const Circuit &c,
                      const std::string &kind) {
    std::string name = Trim(raw_name);
    int nid = c.GetNode(name);
    if (nid >= 0) return nid;

    // Compat mode for files that use i1..iN and o1..oM aliases.
    if (name.size() >= 2 && (name[0] == 'i' || name[0] == 'I') && IsDigits(name.substr(1))) {
        int idx = std::stoi(name.substr(1));
        if (idx >= 1 && idx <= static_cast<int>(c.input_ids.size())) {
            return c.input_ids[idx - 1];
        }
    }
    if (name.size() >= 2 && (name[0] == 'o' || name[0] == 'O') && IsDigits(name.substr(1))) {
        int idx = std::stoi(name.substr(1));
        if (idx >= 1 && idx <= static_cast<int>(c.output_ids.size())) {
            return c.output_ids[idx - 1];
        }
    }

    throw std::runtime_error("Unknown " + kind + " signal in observation: " + raw_name);
}

void ParseAssignmentToken(const std::string &tok,
                          const Circuit &c,
                          std::unordered_map<int, int> &dest,
                          const std::string &kind) {
    auto eq = tok.find('=');
    if (eq == std::string::npos) {
        throw std::runtime_error("Malformed " + kind + " assignment token: " + tok);
    }
    std::string name = Trim(tok.substr(0, eq));
    std::string v = Trim(tok.substr(eq + 1));
    int nid = ResolveSignalName(name, c, kind);
    if (v != "0" && v != "1") {
        throw std::runtime_error("Observation value must be 0 or 1, got: " + v);
    }
    dest[nid] = (v == "1") ? 1 : 0;
}

void ParseKVList(const std::string &tail,
                 const Circuit &c,
                 std::unordered_map<int, int> &dest,
                 const std::string &kind) {
    std::string normalized = tail;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    auto toks = Split(normalized, ' ');
    for (const auto &t : toks) {
        if (t.empty()) continue;
        ParseAssignmentToken(t, c, dest, kind);
    }
}

std::vector<Observation> ParseObservations(const std::string &path, const Circuit &c) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open observation file: " + path);
    }

    std::string base_id;
    {
        std::string fname = fs::path(path).filename().string();
        auto dot = fname.rfind('.');
        if (dot != std::string::npos) fname = fname.substr(0, dot);
        base_id = fname;
    }

    std::vector<Observation> obs;
    std::optional<Observation> cur;

    enum class Section { NONE, INPUTS, OUTPUTS };
    Section section = Section::NONE;

    std::string line;
    while (std::getline(in, line)) {
        std::string raw_line = Trim(line);
        if (raw_line.empty()) {
            continue;
        }

        std::string lower_raw = ToLower(raw_line);
        if (StartsWith(lower_raw, "# inputs") || StartsWith(lower_raw, "inputs:")) {
            if (!cur.has_value()) {
                cur = MakeObservation(base_id);
            }
            section = Section::INPUTS;
            continue;
        }
        if (StartsWith(lower_raw, "# outputs") || StartsWith(lower_raw, "outputs:")) {
            if (!cur.has_value()) {
                cur = MakeObservation(base_id);
            }
            section = Section::OUTPUTS;
            continue;
        }

        auto hash_pos = line.find('#');
        if (hash_pos != std::string::npos) {
            line = line.substr(0, hash_pos);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        if (StartsWith(line, "SCENARIO")) {
            if (cur.has_value()) {
                obs.push_back(cur.value());
            }
            std::stringstream ss(line);
            std::string kw;
            std::string id;
            ss >> kw >> id;
            if (id.empty()) id = base_id + "_" + std::to_string(obs.size());
            cur = MakeObservation(id);
            section = Section::NONE;
            continue;
        }

        if (!cur.has_value()) {
            cur = MakeObservation(base_id);
        }

        if (StartsWith(line, "IN ") || StartsWith(line, "INPUT ")) {
            size_t p = line.find(' ');
            ParseKVList(Trim(line.substr(p + 1)), c, cur->inputs, "input");
            section = Section::INPUTS;
            continue;
        }
        if (StartsWith(line, "OUT ") || StartsWith(line, "OUTPUT ")) {
            size_t p = line.find(' ');
            ParseKVList(Trim(line.substr(p + 1)), c, cur->outputs, "output");
            section = Section::OUTPUTS;
            continue;
        }
        if (line == "END") {
            obs.push_back(cur.value());
            cur.reset();
            section = Section::NONE;
            continue;
        }

        if (line.find('=') != std::string::npos && section != Section::NONE) {
            if (section == Section::INPUTS) {
                ParseAssignmentToken(line, c, cur->inputs, "input");
            } else {
                ParseAssignmentToken(line, c, cur->outputs, "output");
            }
            continue;
        }

        throw std::runtime_error("Unknown observation line: " + line);
    }

    if (cur.has_value()) {
        obs.push_back(cur.value());
    }

    if (obs.empty()) {
        throw std::runtime_error("No scenarios parsed from: " + path);
    }

    return obs;
}

std::unordered_map<int, int> Simulate(const Circuit &c, const std::unordered_map<int, int> &input_assign) {
    auto order = TopologicalOrder(c);
    std::unordered_map<int, int> vals = input_assign;

    for (int nid : order) {
        const auto &n = c.nodes[nid];
        if (n.type == GateType::INPUT) {
            if (!vals.count(nid)) {
                throw std::runtime_error("Missing primary input assignment for node " + n.name);
            }
            continue;
        }
        if (!IsGateTypeComponent(n.type)) {
            continue;
        }
        std::vector<int> in_vals;
        in_vals.reserve(n.fanins.size());
        for (int fi : n.fanins) {
            auto it = vals.find(fi);
            if (it == vals.end()) {
                throw std::runtime_error("Cannot evaluate gate " + n.name + ": unknown fanin value");
            }
            in_vals.push_back(it->second);
        }
        vals[nid] = EvalGate(n.type, in_vals) ? 1 : 0;
    }

    return vals;
}

void WriteScenarios(const Circuit &c,
                    const std::string &out_path,
                    int count,
                    int min_errors,
                    int max_errors,
                    uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> bit01(0, 1);

    if (count <= 0) throw std::runtime_error("count must be > 0");
    if (min_errors <= 0) throw std::runtime_error("min_errors must be > 0");
    if (max_errors < min_errors) throw std::runtime_error("max_errors < min_errors");

    std::ofstream out(out_path);
    if (!out) {
        throw std::runtime_error("Cannot open output observation file: " + out_path);
    }

    int out_count = static_cast<int>(c.output_ids.size());
    min_errors = std::min(min_errors, out_count);
    max_errors = std::min(max_errors, out_count);
    std::uniform_int_distribution<int> err_dist(min_errors, max_errors);

    for (int i = 0; i < count; ++i) {
        std::unordered_map<int, int> in;
        for (int pid : c.input_ids) {
            in[pid] = bit01(rng);
        }
        auto sim = Simulate(c, in);

        int k = err_dist(rng);
        std::vector<int> out_ids = c.output_ids;
        std::shuffle(out_ids.begin(), out_ids.end(), rng);

        std::unordered_map<int, int> out_obs;
        for (int j = 0; j < static_cast<int>(c.output_ids.size()); ++j) {
            int oid = out_ids[j];
            int v = sim.at(oid);
            if (j < k) v = 1 - v;
            out_obs[oid] = v;
        }

        out << "SCENARIO s" << i << "\n";
        out << "IN ";
        for (int pid : c.input_ids) {
            out << c.nodes[pid].name << "=" << in[pid] << " ";
        }
        out << "\n";
        out << "OUT ";
        for (int oid : c.output_ids) {
            out << c.nodes[oid].name << "=" << out_obs[oid] << " ";
        }
        out << "\nEND\n\n";
    }
}

struct CNF {
    std::vector<std::vector<int>> hard;
    std::vector<std::vector<int>> soft;
    int max_var = 0;

    void AddHard(std::vector<int> c) {
        for (int lit : c) max_var = std::max(max_var, std::abs(lit));
        hard.push_back(std::move(c));
    }

    void AddSoft(std::vector<int> c) {
        for (int lit : c) max_var = std::max(max_var, std::abs(lit));
        soft.push_back(std::move(c));
    }
};

struct EncodedInstance {
    CNF cnf;
    std::unordered_map<int, std::string> ab_var_to_component;
};

struct VarManager {
    int next_var = 1;
    std::unordered_map<int, int> node_var;
    std::unordered_map<int, int> ab_var;

    int GetNodeVar(int nid) {
        auto it = node_var.find(nid);
        if (it != node_var.end()) return it->second;
        int v = next_var++;
        node_var[nid] = v;
        return v;
    }

    int GetAbVar(int nid) {
        auto it = ab_var.find(nid);
        if (it != ab_var.end()) return it->second;
        int v = next_var++;
        ab_var[nid] = v;
        return v;
    }
};

using EdgeKey = uint64_t;

EdgeKey MkEdgeKey(int u, int v) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(u)) << 32) |
           static_cast<uint32_t>(v);
}

struct DOEState {
    std::vector<bool> filtered_node;
    std::unordered_set<EdgeKey> blocked_edges;
    std::unordered_set<EdgeKey> filtered_edges;
    std::vector<bool> dominated_gate;
    std::vector<std::optional<int>> known_val;
};

std::vector<bool> ComputeReachableToSink(const Circuit &c,
                                         const std::vector<bool> &filtered_node,
                                         const std::unordered_set<EdgeKey> &filtered_edges) {
    std::vector<bool> reachable(c.nodes.size(), false);
    std::vector<int> stack;
    for (int oid : c.output_ids) {
        if (!filtered_node[oid]) {
            reachable[oid] = true;
            stack.push_back(oid);
        }
    }

    while (!stack.empty()) {
        int u = stack.back();
        stack.pop_back();
        for (int fi : c.nodes[u].fanins) {
            if (filtered_node[fi]) continue;
            if (filtered_edges.count(MkEdgeKey(fi, u))) continue;
            if (!reachable[fi]) {
                reachable[fi] = true;
                stack.push_back(fi);
            }
        }
    }

    return reachable;
}

// Lengauer-Tarjan algorithm for computing immediate dominators
// Based on: "A Fast Algorithm for Finding Dominators in a Flowgraph" (1979)
// This implements the near-linear time algorithm as described in the paper.

struct LTState {
    std::vector<int> parent;
    std::vector<int> ancestor;
    std::vector<int> child;
    std::vector<int> vertex;
    std::vector<int> label;
    std::vector<int> semi;
    std::vector<int> dom;
    std::vector<std::vector<int>> pred;
    std::vector<std::vector<int>> bucket;
    int n = 0;

    void Init(int n_nodes) {
        n = n_nodes;
        parent.assign(n, -1);
        ancestor.assign(n, -1);
        child.assign(n, -1);
        vertex.assign(n + 1, -1);
        label.assign(n, -1);
        semi.assign(n, -1);
        dom.assign(n, -1);
        pred.assign(n, {});
        bucket.assign(n, {});
    }

    void Link(int v, int w) {
        ancestor[w] = v;
    }

    int Eval(int v) {
        if (ancestor[v] == -1) {
            return v;
        }
        Compress(v);
        if (semi[label[ancestor[v]]] >= semi[label[v]]) {
            return label[v];
        }
        return label[ancestor[v]];
    }

    void Compress(int v) {
        if (ancestor[ancestor[v]] != -1) {
            Compress(ancestor[v]);
            if (semi[label[ancestor[v]]] < semi[label[v]]) {
                label[v] = label[ancestor[v]];
            }
            ancestor[v] = ancestor[ancestor[v]];
        }
    }
};

// Build a directed graph from circuit for dominator computation
// Returns: (node_id -> dfs_index mapping, dfs_index -> node_id mapping, adjacency list)
std::tuple<std::unordered_map<int, int>, std::vector<int>, std::vector<std::vector<int>>> 
BuildDominatorGraph(
    const Circuit &c,
    const std::vector<bool> &filtered_node,
    const std::unordered_set<EdgeKey> &filtered_edges,
    const std::vector<bool> &reachable) {
    
    std::vector<int> active_nodes;
    active_nodes.reserve(c.nodes.size() + 1);
    for (int i = 0; i < static_cast<int>(c.nodes.size()); ++i) {
        if (filtered_node[i]) continue;
        if (!reachable[i]) continue;
        if (!IsGateTypeComponent(c.nodes[i].type)) continue;
        active_nodes.push_back(i);
    }

    int sink_idx = static_cast<int>(active_nodes.size());
    active_nodes.push_back(-1);

    std::unordered_map<int, int> node_to_idx;
    for (int i = 0; i < static_cast<int>(active_nodes.size()); ++i) {
        node_to_idx[active_nodes[i]] = i;
    }

    std::vector<std::vector<int>> rev_adj(active_nodes.size());
    for (int i = 0; i < static_cast<int>(active_nodes.size()) - 1; ++i) {
        int nid = active_nodes[i];
        for (int fo : c.nodes[nid].fanouts) {
            if (filtered_node[fo]) continue;
            if (!reachable[fo]) continue;
            if (filtered_edges.count(MkEdgeKey(nid, fo))) continue;
            auto it = node_to_idx.find(fo);
            if (it != node_to_idx.end()) {
                rev_adj[it->second].push_back(i);
            }
        }
        if (c.nodes[nid].is_output_decl) {
            rev_adj[sink_idx].push_back(i);
        }
    }

    return {node_to_idx, active_nodes, rev_adj};
}

// Compute immediate dominators using Lengauer-Tarjan algorithm
// Returns: map from node_id to its immediate dominator node_id (or -1 if no idom)
std::unordered_map<int, int> ComputeImmediateDominatorsLT(
    const Circuit &c,
    const std::vector<bool> &filtered_node,
    const std::unordered_set<EdgeKey> &filtered_edges,
    const std::vector<bool> &reachable) {
    
    auto [node_to_idx, idx_to_node, rev_adj] = BuildDominatorGraph(c, filtered_node, filtered_edges, reachable);
    int n = idx_to_node.size();
    
    if (n <= 1) {
        return {};
    }

    int sink_idx = n - 1;

    LTState st;
    st.Init(n);
    
    std::vector<bool> visited(n, false);
    int dfs_num = 0;
    
    std::function<void(int)> Dfs = [&](int v) {
        visited[v] = true;
        dfs_num++;
        st.semi[v] = dfs_num;
        st.vertex[dfs_num] = v;
        st.label[v] = v;
        
        for (int w : rev_adj[v]) {
            if (!visited[w]) {
                st.parent[w] = v;
                Dfs(w);
            }
            st.pred[w].push_back(v);
        }
    };
    
    Dfs(sink_idx);

    for (int i = dfs_num; i >= 2; --i) {
        int w = st.vertex[i];
        
        for (int v : st.pred[w]) {
            int u = st.Eval(v);
            if (st.semi[u] < st.semi[w]) {
                st.semi[w] = st.semi[u];
            }
        }
        st.bucket[st.vertex[st.semi[w]]].push_back(w);
        st.Link(st.parent[w], w);
        
        for (int v : st.bucket[st.parent[w]]) {
            int u = st.Eval(v);
            st.dom[v] = (st.semi[u] < st.semi[v]) ? u : st.parent[w];
        }
        st.bucket[st.parent[w]].clear();
    }
    
    for (int i = 2; i <= dfs_num; ++i) {
        int w = st.vertex[i];
        if (st.dom[w] != st.vertex[st.semi[w]]) {
            st.dom[w] = st.dom[st.dom[w]];
        }
    }
    
    std::unordered_map<int, int> idom;
    for (int i = 0; i < n - 1; ++i) {
        int node_id = idx_to_node[i];
        if (st.dom[i] >= 0 && st.dom[i] < n) {
            int dom_idx = st.dom[i];
            if (dom_idx == sink_idx) {
                idom[node_id] = -1;
            } else {
                idom[node_id] = idx_to_node[dom_idx];
            }
        } else {
            idom[node_id] = -1;
        }
    }
    
    return idom;
}

std::unordered_map<int, std::unordered_set<int>> ComputeDominators(
    const Circuit &c,
    const std::vector<bool> &filtered_node,
    const std::unordered_set<EdgeKey> &filtered_edges,
    const std::vector<bool> &reachable) {
    
    // Use Lengauer-Tarjan algorithm for near-linear time complexity
    auto idom = ComputeImmediateDominatorsLT(c, filtered_node, filtered_edges, reachable);
    
    // Convert immediate dominators to full dominator sets
    std::unordered_map<int, std::unordered_set<int>> dom;
    const int SINK = -1;
    dom[SINK] = {SINK};
    
    for (const auto &kv : idom) {
        int node = kv.first;
        int immediate_dom = kv.second;
        
        std::unordered_set<int> dset = {node};
        int cur = immediate_dom;
        while (cur != -1 && cur != SINK) {
            dset.insert(cur);
            auto it = idom.find(cur);
            if (it == idom.end()) break;
            cur = it->second;
        }
        dset.insert(SINK);
        dom[node] = std::move(dset);
    }
    
    return dom;
}

void AddGateCNF(const Circuit &c,
                int gid,
                const std::vector<int> &fanin_vars,
                int y,
                std::vector<std::vector<int>> &clauses) {
    auto t = c.nodes[gid].type;
    if (t == GateType::BUF) {
        clauses.push_back({-y, fanin_vars[0]});
        clauses.push_back({y, -fanin_vars[0]});
        return;
    }
    if (t == GateType::NOT) {
        clauses.push_back({-y, -fanin_vars[0]});
        clauses.push_back({y, fanin_vars[0]});
        return;
    }
    if (t == GateType::AND) {
        for (int x : fanin_vars) clauses.push_back({-y, x});
        std::vector<int> c1 = {y};
        for (int x : fanin_vars) c1.push_back(-x);
        clauses.push_back(std::move(c1));
        return;
    }
    if (t == GateType::NAND) {
        for (int x : fanin_vars) clauses.push_back({y, x});
        std::vector<int> c1 = {-y};
        for (int x : fanin_vars) c1.push_back(-x);
        clauses.push_back(std::move(c1));
        return;
    }
    if (t == GateType::OR) {
        std::vector<int> c1 = {-y};
        for (int x : fanin_vars) c1.push_back(x);
        clauses.push_back(std::move(c1));
        for (int x : fanin_vars) clauses.push_back({y, -x});
        return;
    }
    if (t == GateType::NOR) {
        for (int x : fanin_vars) clauses.push_back({-y, -x});
        std::vector<int> c1 = {y};
        for (int x : fanin_vars) c1.push_back(x);
        clauses.push_back(std::move(c1));
        return;
    }
    if (t == GateType::XOR) {
        if (fanin_vars.size() != 2) {
            throw std::runtime_error("XOR with arity != 2 is not supported in this implementation");
        }
        int a = fanin_vars[0], b = fanin_vars[1];
        clauses.push_back({y, a, b});
        clauses.push_back({y, -a, -b});
        clauses.push_back({-y, a, -b});
        clauses.push_back({-y, -a, b});
        return;
    }
    if (t == GateType::XNOR) {
        if (fanin_vars.size() != 2) {
            throw std::runtime_error("XNOR with arity != 2 is not supported in this implementation");
        }
        int a = fanin_vars[0], b = fanin_vars[1];
        clauses.push_back({-a, -b, y});
        clauses.push_back({a, b, y});
        clauses.push_back({a, -b, -y});
        clauses.push_back({-a, b, -y});
        return;
    }
    throw std::runtime_error("Unsupported gate type during CNF encoding");
}

void AddGateCNFWithConstants(const Circuit &c,
                              int gid,
                              const std::vector<std::optional<int>> &fin_opt,
                              int y,
                              std::vector<std::vector<int>> &clauses) {
    std::vector<int> fin;
    std::unordered_map<int, int> const_map;
    int tmp_var = 1000000;
    for (size_t i = 0; i < fin_opt.size(); ++i) {
        if (!fin_opt[i].has_value()) continue;
        int v = fin_opt[i].value();
        if (v == 1 || v == -1) {
            int tv = tmp_var++;
            fin.push_back(tv);
            const_map[tv] = v;
        } else {
            fin.push_back(v);
        }
    }

    AddGateCNF(c, gid, fin, y, clauses);

    for (auto &cl : clauses) {
        std::vector<int> new_cl;
        bool tautology = false;
        for (int lit : cl) {
            int var = std::abs(lit);
            auto it = const_map.find(var);
            if (it != const_map.end()) {
                int const_val = it->second;
                bool lit_sign = (lit < 0);
                bool const_sign = (const_val < 0);
                bool same = (lit_sign == const_sign);
                if (same) {
                    tautology = true;
                    break;
                }
            } else {
                new_cl.push_back(lit);
            }
        }
        if (tautology) {
            cl.clear();
        } else {
            cl = std::move(new_cl);
        }
    }

    clauses.erase(
        std::remove_if(clauses.begin(), clauses.end(),
                       [](const std::vector<int> &cl) { return cl.empty(); }),
        clauses.end());
}

DOEState RunDOE(const Circuit &c,
                const Observation &obs,
                int max_iters,
                bool legacy_outputs_as_known) {
    DOEState s;
    s.filtered_node.assign(c.nodes.size(), false);
    s.dominated_gate.assign(c.nodes.size(), false);
    s.known_val.assign(c.nodes.size(), std::nullopt);

    for (const auto &kv : obs.inputs) {
        s.known_val[kv.first] = kv.second;
    }
    if (legacy_outputs_as_known) {
        for (const auto &kv : obs.outputs) {
            s.known_val[kv.first] = kv.second;
        }
    }

    // Track dominator sets from previous iteration for change detection
    std::unordered_map<int, std::unordered_set<int>> prev_dom;

    for (int it = 0; it < max_iters; ++it) {
        auto reachable = ComputeReachableToSink(c, s.filtered_node, s.filtered_edges);
        auto dom = ComputeDominators(c, s.filtered_node, s.filtered_edges, reachable);

        bool dom_changed = false;
        if (it > 0) {
            for (const auto &kv : dom) {
                auto it_prev = prev_dom.find(kv.first);
                if (it_prev == prev_dom.end() || it_prev->second != kv.second) {
                    dom_changed = true;
                    break;
                }
            }
            if (!dom_changed) {
                for (const auto &kv : prev_dom) {
                    if (!dom.count(kv.first)) {
                        dom_changed = true;
                        break;
                    }
                }
            }
        }
        prev_dom = dom;

        std::vector<bool> dominated_new(c.nodes.size(), false);
        for (int gid = 0; gid < static_cast<int>(c.nodes.size()); ++gid) {
            if (s.filtered_node[gid]) continue;
            if (!reachable[gid]) continue;
            if (!IsGateTypeComponent(c.nodes[gid].type)) continue;
            auto it_dom = dom.find(gid);
            if (it_dom == dom.end()) continue;
            bool has_strict_dom = false;
            for (int d : it_dom->second) {
                if (d != gid && d != -1) {
                    has_strict_dom = true;
                    break;
                }
            }
            if (has_strict_dom) {
                dominated_new[gid] = true;
            }
        }

        bool new_dominated_found = false;
        for (int gid = 0; gid < static_cast<int>(c.nodes.size()); ++gid) {
            if (dominated_new[gid] && !s.dominated_gate[gid]) {
                new_dominated_found = true;
            }
            if (dominated_new[gid]) s.dominated_gate[gid] = true;
        }

        bool changed_backbone = true;
        while (changed_backbone) {
            changed_backbone = false;
            for (int gid = 0; gid < static_cast<int>(c.nodes.size()); ++gid) {
                if (s.filtered_node[gid]) continue;
                if (!s.dominated_gate[gid]) continue;
                if (!IsGateTypeComponent(c.nodes[gid].type)) continue;
                if (s.known_val[gid].has_value()) continue;

                std::vector<std::optional<int>> in_vals;
                in_vals.reserve(c.nodes[gid].fanins.size());
                for (int fi : c.nodes[gid].fanins) {
                    if (s.filtered_node[fi] || s.filtered_edges.count(MkEdgeKey(fi, gid))) {
                        in_vals.push_back(std::nullopt);
                    } else {
                        in_vals.push_back(s.known_val[fi]);
                    }
                }
                auto v = TryInferGateValue(c.nodes[gid].type, in_vals);
                if (v.has_value()) {
                    s.known_val[gid] = v.value();
                    changed_backbone = true;
                }
            }
        }

        auto blocked_before = s.blocked_edges;
        for (int gid = 0; gid < static_cast<int>(c.nodes.size()); ++gid) {
            if (s.filtered_node[gid]) continue;
            if (!IsGateTypeComponent(c.nodes[gid].type)) continue;
            auto ctrl_opt = ControllingValue(c.nodes[gid].type);
            if (!ctrl_opt.has_value()) continue;
            int ctrl = ctrl_opt.value();

            bool has_ctrl_known = false;
            for (int fi : c.nodes[gid].fanins) {
                if (s.filtered_node[fi]) continue;
                if (s.filtered_edges.count(MkEdgeKey(fi, gid))) continue;
                if (!s.known_val[fi].has_value()) continue;
                if (s.known_val[fi].value() == ctrl) {
                    has_ctrl_known = true;
                    break;
                }
            }
            if (!has_ctrl_known) continue;

            for (int fi : c.nodes[gid].fanins) {
                if (s.filtered_node[fi]) continue;
                if (s.filtered_edges.count(MkEdgeKey(fi, gid))) continue;
                if (!s.known_val[fi].has_value()) {
                    s.blocked_edges.insert(MkEdgeKey(fi, gid));
                }
            }
        }

        std::vector<bool> filtered_new = s.filtered_node;
        auto filtered_edges_new = s.filtered_edges;

        bool changed_filter = true;
        while (changed_filter) {
            changed_filter = false;

            for (int u = 0; u < static_cast<int>(c.nodes.size()); ++u) {
                for (int v : c.nodes[u].fanouts) {
                    EdgeKey e = MkEdgeKey(u, v);
                    bool should_filter = s.blocked_edges.count(e) || filtered_new[v];
                    if (should_filter && !filtered_edges_new.count(e)) {
                        filtered_edges_new.insert(e);
                        changed_filter = true;
                    }
                }
            }

            for (int u = 0; u < static_cast<int>(c.nodes.size()); ++u) {
                if (filtered_new[u]) continue;
                if (c.nodes[u].fanouts.empty()) continue;

                bool all_fanout_filtered = true;
                for (int v : c.nodes[u].fanouts) {
                    if (!filtered_edges_new.count(MkEdgeKey(u, v))) {
                        all_fanout_filtered = false;
                        break;
                    }
                }
                if (all_fanout_filtered) {
                    filtered_new[u] = true;
                    changed_filter = true;
                }
            }
        }

        // Comprehensive change detection as per Algorithm 1 in the paper:
        // Check for changes in: blocked edges, filtered nodes, filtered edges,
        // dominator sets, and newly discovered dominated gates
        bool changed = false;
        if (blocked_before != s.blocked_edges) changed = true;
        if (filtered_new != s.filtered_node) changed = true;
        if (filtered_edges_new != s.filtered_edges) changed = true;
        if (dom_changed) changed = true;
        if (new_dominated_found) changed = true;

        s.filtered_node = std::move(filtered_new);
        s.filtered_edges = std::move(filtered_edges_new);

        // If no changes detected, we've reached a fixpoint - exit loop
        if (!changed) {
            break;
        }
    }

    return s;
}

EncodedInstance BuildDOEWCNF(const Circuit &c, const Observation &obs, const DOEState &s) {
    EncodedInstance enc;
    CNF &cnf = enc.cnf;
    VarManager vm;

    for (int gid = 0; gid < static_cast<int>(c.nodes.size()); ++gid) {
        if (s.filtered_node[gid]) continue;
        if (!IsGateTypeComponent(c.nodes[gid].type)) continue;

        if (s.known_val[gid].has_value()) continue;

        int y = vm.GetNodeVar(gid);

        std::vector<std::optional<int>> fin_opt;
        for (int fi : c.nodes[gid].fanins) {
            if (s.filtered_node[fi]) continue;
            if (s.filtered_edges.count(MkEdgeKey(fi, gid))) continue;
            if (s.known_val[fi].has_value()) {
                fin_opt.push_back(s.known_val[fi].value() ? 1 : -1);
            } else {
                auto it_in = obs.inputs.find(fi);
                if (it_in != obs.inputs.end()) {
                    fin_opt.push_back(it_in->second ? 1 : -1);
                } else {
                    fin_opt.push_back(vm.GetNodeVar(fi));
                }
            }
        }

        std::vector<int> fin;
        bool has_const = false;
        for (const auto &f : fin_opt) {
            if (!f.has_value()) continue;
            int v = f.value();
            if (v == 1 || v == -1) {
                has_const = true;
            } else {
                fin.push_back(v);
            }
        }

        if (fin.empty() && !has_const) continue;

        std::vector<std::vector<int>> fclauses;
        if (has_const) {
            AddGateCNFWithConstants(c, gid, fin_opt, y, fclauses);
        } else {
            AddGateCNF(c, gid, fin, y, fclauses);
        }

        if (s.dominated_gate[gid]) {
            for (auto &cl : fclauses) cnf.AddHard(std::move(cl));
        } else {
            int ab = vm.GetAbVar(gid);
            enc.ab_var_to_component[ab] = c.nodes[gid].name;
            for (auto &cl : fclauses) {
                cl.push_back(ab);
                cnf.AddHard(std::move(cl));
            }
            cnf.AddSoft({-ab});
        }
    }

    for (const auto &kv : obs.outputs) {
        int nid = kv.first;
        if (s.filtered_node[nid]) continue;
        if (s.known_val[nid].has_value()) continue;
        int lit = vm.GetNodeVar(nid);
        cnf.AddHard({kv.second ? lit : -lit});
    }

    cnf.max_var = std::max(cnf.max_var, vm.next_var - 1);
    return enc;
}

void WriteWCNF(const CNF &cnf, const std::string &path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot write WCNF file: " + path);
    }

    int64_t top = static_cast<int64_t>(cnf.soft.size()) + 1;
    out << "p wcnf " << cnf.max_var << " " << (cnf.hard.size() + cnf.soft.size()) << " " << top << "\n";

    for (const auto &cl : cnf.hard) {
        out << top << " ";
        for (int lit : cl) out << lit << " ";
        out << "0\n";
    }
    for (const auto &cl : cnf.soft) {
        out << "1 ";
        for (int lit : cl) out << lit << " ";
        out << "0\n";
    }
}

struct SolveResult {
    bool solved = false;
    bool optimal = false;
    int cost = -1;
    double seconds = 0.0;
    std::vector<int> model;
    int num_diagnoses = 1;
};

SolveResult RunSolver(const std::string &solver_cmd, const std::string &wcnf_path) {
    auto t0 = std::chrono::steady_clock::now();
    std::string cmd = solver_cmd + " " + wcnf_path + " 2>&1";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run solver command: " + cmd);
    }

    char buffer[65536];
    std::string output;
    std::string line_accum;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        line_accum += buffer;
        if (line_accum.back() != '\n') continue;
        output += line_accum;
        line_accum.clear();
    }
    if (!line_accum.empty()) output += line_accum;
    int rc = pclose(pipe);
    auto t1 = std::chrono::steady_clock::now();

    SolveResult r;
    r.seconds = std::chrono::duration<double>(t1 - t0).count();

    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty()) continue;
        if (line.rfind("o ", 0) == 0) {
            std::stringstream ls(line.substr(2));
            int c = -1;
            ls >> c;
            if (c >= 0) {
                r.cost = c;
                r.optimal = true;
                r.solved = true;
            }
        }
        if (line.find("OPTIMUM") != std::string::npos || line.find("OPTIMAL") != std::string::npos) {
            r.optimal = true;
            r.solved = true;
        }
        if (line.find("UNSAT") != std::string::npos || line.find("SAT") != std::string::npos) {
            r.solved = true;
        }
        if (line.rfind("v ", 0) == 0) {
            std::vector<int> model;
            std::stringstream ls(line.substr(2));
            int lit = 0;
            while (ls >> lit) {
                if (lit == 0) break;
                model.push_back(lit);
            }
            if (r.model.empty()) {
                r.model = model;
            }
        }
        if (line.rfind("c num_diagnoses ", 0) == 0) {
            std::stringstream ls(line.substr(16));
            ls >> r.num_diagnoses;
        }
    }

    if (rc != 0 && !r.solved) {
        throw std::runtime_error("Solver exited with non-zero code and no parseable result. Command: " + cmd + "\nOutput:\n" + output);
    }

    return r;
}

std::string Join(const std::vector<std::string> &items, const std::string &sep) {
    if (items.empty()) return "-";
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += sep;
        out += items[i];
    }
    return out;
}

void EnsureDir(const std::string &p) {
    fs::create_directories(p);
}

std::string GetArg(const std::unordered_map<std::string, std::string> &args,
                   const std::string &key,
                   const std::string &def = "") {
    auto it = args.find(key);
    if (it == args.end()) return def;
    return it->second;
}

int GetArgInt(const std::unordered_map<std::string, std::string> &args,
              const std::string &key,
              int def) {
    auto it = args.find(key);
    if (it == args.end()) return def;
    return std::stoi(it->second);
}

std::unordered_map<std::string, std::string> ParseArgs(int argc, char **argv, int start) {
    std::unordered_map<std::string, std::string> out;
    for (int i = start; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) != 0) continue;
        std::string k = a.substr(2);
        std::string v = "1";
        if (i + 1 < argc) {
            std::string n = argv[i + 1];
            if (n.rfind("--", 0) != 0) {
                v = n;
                ++i;
            }
        }
        out[k] = v;
    }
    return out;
}

void Usage() {
    std::cout
        << "Usage:\n"
        << "  doe_maxsat generate --bench <bench> --out <obs.txt> --count <N> --seed <S> --min-output-errors <a> --max-output-errors <b>\n"
        << "  doe_maxsat encode --bench <bench> --obs <obs.txt> --outdir <dir> [--max-iters 2] [--legacy-outputs-as-known 0|1]\n"
        << "  doe_maxsat run --bench <bench> --obs <obs.txt> --solver <solver_cmd> --out <results.csv> [--max-iters 2] [--legacy-outputs-as-known 0|1]\n";
}

}  // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            Usage();
            return 1;
        }

        std::string mode = argv[1];
        auto args = ParseArgs(argc, argv, 2);

        if (mode == "generate") {
            std::string bench = GetArg(args, "bench");
            std::string out = GetArg(args, "out");
            int count = GetArgInt(args, "count", 100);
            int min_e = GetArgInt(args, "min-output-errors", 1);
            int max_e = GetArgInt(args, "max-output-errors", 1);
            uint64_t seed = static_cast<uint64_t>(std::stoull(GetArg(args, "seed", "1")));

            if (bench.empty() || out.empty()) {
                throw std::runtime_error("generate requires --bench and --out");
            }

            Circuit c = ParseBench(bench);
            WriteScenarios(c, out, count, min_e, max_e, seed);
            std::cout << "Generated " << count << " scenarios to " << out << "\n";
            return 0;
        }

        if (mode == "encode") {
            std::string bench = GetArg(args, "bench");
            std::string obs_path = GetArg(args, "obs");
            std::string outdir = GetArg(args, "outdir");
            int max_iters = GetArgInt(args, "max-iters", 2);
            bool legacy_outputs_as_known = GetArgInt(args, "legacy-outputs-as-known", 0) != 0;

            if (bench.empty() || obs_path.empty() || outdir.empty()) {
                throw std::runtime_error("encode requires --bench --obs --outdir");
            }

            Circuit c = ParseBench(bench);
            auto obs = ParseObservations(obs_path, c);
            EnsureDir(outdir);

            std::ofstream meta(fs::path(outdir) / "meta.csv");
            meta << "scenario,hard,soft,vars,filtered_nodes,blocked_edges\n";

            for (size_t i = 0; i < obs.size(); ++i) {
                DOEState st = RunDOE(c, obs[i], max_iters, legacy_outputs_as_known);
                EncodedInstance enc = BuildDOEWCNF(c, obs[i], st);
                CNF &cnf = enc.cnf;

                std::stringstream fn;
                fn << "scenario_" << std::setw(5) << std::setfill('0') << i << ".wcnf";
                std::string p = (fs::path(outdir) / fn.str()).string();
                WriteWCNF(cnf, p);

                int filtered_nodes = 0;
                for (bool x : st.filtered_node) if (x) filtered_nodes++;
                meta << obs[i].id << "," << cnf.hard.size() << "," << cnf.soft.size() << "," << cnf.max_var
                     << "," << filtered_nodes << "," << st.blocked_edges.size() << "\n";

                std::ofstream mapf(fs::path(outdir) / (fn.str() + ".map"));
                mapf << "ab_var,component\n";
                for (const auto &kv : enc.ab_var_to_component) {
                    mapf << kv.first << "," << kv.second << "\n";
                }
            }

            std::cout << "Encoded " << obs.size() << " scenarios into " << outdir << "\n";
            return 0;
        }

        if (mode == "run") {
            std::string bench = GetArg(args, "bench");
            std::string obs_path = GetArg(args, "obs");
            std::string solver = GetArg(args, "solver");
            std::string out_csv = GetArg(args, "out");
            int max_iters = GetArgInt(args, "max-iters", 2);
            bool legacy_outputs_as_known = GetArgInt(args, "legacy-outputs-as-known", 0) != 0;
            bool print_table = GetArgInt(args, "print-table", 1) != 0;
            bool enum_all = GetArgInt(args, "enum-all", 0) != 0;
            int enum_limit = GetArgInt(args, "enum-limit", 0);

            if (bench.empty() || obs_path.empty() || solver.empty()) {
                throw std::runtime_error("run requires --bench --obs --solver");
            }

            Circuit c = ParseBench(bench);
            auto obs = ParseObservations(obs_path, c);

            fs::path tmp_dir = fs::temp_directory_path() / "doe_maxsat_tmp";
            fs::create_directories(tmp_dir);

            std::optional<std::ofstream> out_file;
            if (!out_csv.empty()) {
                out_file.emplace(out_csv);
                (*out_file) << "obs,solved,optimal,cost,seconds,hard,soft,vars,diag_size,diag_components\n";
            }

            if (print_table) {
                std::cout << std::left
                          << std::setw(14) << "obs" << "|"
                          << std::setw(8) << "solved" << "|"
                          << std::setw(8) << "optimal" << "|"
                          << std::setw(6) << "cost" << "|"
                          << std::setw(10) << "seconds" << "|"
                          << std::setw(8) << "hard" << "|"
                          << std::setw(8) << "soft" << "|"
                          << std::setw(8) << "vars" << "|"
                          << std::setw(10) << "diag_size" << "|"
                          << "diag_components"
                          << "\n";
                std::cout << "--------------+--------+--------+------+----------+--------+--------+--------+----------+----------------\n";
            }

            for (size_t i = 0; i < obs.size(); ++i) {
                DOEState st = RunDOE(c, obs[i], max_iters, legacy_outputs_as_known);
                EncodedInstance enc = BuildDOEWCNF(c, obs[i], st);
                CNF &cnf = enc.cnf;

                fs::path wcnf = tmp_dir / ("scenario_" + std::to_string(i) + ".wcnf");
                WriteWCNF(cnf, wcnf.string());

                auto ExtractDiag = [&](const std::vector<int> &model) -> std::vector<std::string> {
                    std::unordered_set<int> model_set(model.begin(), model.end());
                    std::vector<std::string> faulty;
                    for (const auto &kv : enc.ab_var_to_component) {
                        if (model_set.count(kv.first)) {
                            faulty.push_back(kv.second);
                        }
                    }
                    std::sort(faulty.begin(), faulty.end());
                    return faulty;
                };

                if (!enum_all) {
                    SolveResult sr = RunSolver(solver, wcnf.string());
                    auto faulty = ExtractDiag(sr.model);
                    std::string faulty_str = Join(faulty, "|");

                    if (out_file.has_value()) {
                        (*out_file) << obs[i].id << "," << (sr.solved ? 1 : 0) << "," << (sr.optimal ? 1 : 0)
                                    << "," << sr.cost << "," << std::fixed << std::setprecision(6) << sr.seconds << ","
                                    << cnf.hard.size() << "," << cnf.soft.size() << "," << cnf.max_var << ","
                                    << faulty.size() << "," << faulty_str << "\n";
                    }
                    if (print_table) {
                        std::cout << std::left
                                  << std::setw(14) << obs[i].id << "|"
                                  << std::setw(8) << (sr.solved ? 1 : 0) << "|"
                                  << std::setw(8) << (sr.optimal ? 1 : 0) << "|"
                                  << std::setw(6) << sr.cost << "|"
                                  << std::setw(10) << std::fixed << std::setprecision(6) << sr.seconds << "|"
                                  << std::setw(8) << cnf.hard.size() << "|"
                                  << std::setw(8) << cnf.soft.size() << "|"
                                  << std::setw(8) << cnf.max_var << "|"
                                  << std::setw(10) << faulty.size() << "|"
                                  << faulty_str << "\n";
                    }
                } else {
                    std::string full_solver = solver + " --enum-all";
                    if (enum_limit > 0) {
                        full_solver += " --enum-limit " + std::to_string(enum_limit);
                    }

                    auto t0 = std::chrono::steady_clock::now();
                    std::string cmd = full_solver + " " + wcnf.string() + " 2>&1";

                    FILE *pipe = popen(cmd.c_str(), "r");
                    if (!pipe) {
                        throw std::runtime_error("Failed to run solver command: " + cmd);
                    }

                    bool solved = false;
                    bool optimal = false;
                    int cost = -1;
                    int diag_idx = 0;

                    char buf[65536];
                    std::string line_accum;

                    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
                        line_accum += buf;
                        if (line_accum.back() != '\n') continue;
                        std::string line = Trim(line_accum);
                        line_accum.clear();
                        if (line.empty()) continue;

                        if (line.rfind("o ", 0) == 0) {
                            std::stringstream ls(line.substr(2));
                            int c = -1;
                            ls >> c;
                            if (c >= 0) {
                                cost = c;
                                optimal = true;
                                solved = true;
                            }
                        }
                        if (line.find("OPTIMUM") != std::string::npos || line.find("OPTIMAL") != std::string::npos) {
                            optimal = true;
                            solved = true;
                        }
                        if (line.find("UNSAT") != std::string::npos || line.find("SAT") != std::string::npos) {
                            solved = true;
                        }

                        if (line.rfind("v ", 0) == 0) {
                            std::vector<int> model;
                            std::stringstream ls(line.substr(2));
                            int lit = 0;
                            while (ls >> lit) {
                                if (lit == 0) break;
                                model.push_back(lit);
                            }
                            diag_idx++;
                            auto faulty = ExtractDiag(model);
                            std::string faulty_str = Join(faulty, "|");
                            std::string id = obs[i].id + "[" + std::to_string(diag_idx) + "]";
                            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

                            if (out_file.has_value()) {
                                (*out_file) << id << "," << (solved ? 1 : 0) << "," << (optimal ? 1 : 0)
                                            << "," << cost << "," << std::fixed << std::setprecision(6) << elapsed << ","
                                            << cnf.hard.size() << "," << cnf.soft.size() << "," << cnf.max_var << ","
                                            << faulty.size() << "," << faulty_str << "\n";
                            }
                            if (print_table) {
                                std::cout << std::left
                                          << std::setw(14) << id << "|"
                                          << std::setw(8) << (solved ? 1 : 0) << "|"
                                          << std::setw(8) << (optimal ? 1 : 0) << "|"
                                          << std::setw(6) << cost << "|"
                                          << std::setw(10) << std::fixed << std::setprecision(6) << elapsed << "|"
                                          << std::setw(8) << cnf.hard.size() << "|"
                                          << std::setw(8) << cnf.soft.size() << "|"
                                          << std::setw(8) << cnf.max_var << "|"
                                          << std::setw(10) << faulty.size() << "|"
                                          << faulty_str << "\n";
                            }
                        }
                    }
                    if (!line_accum.empty()) {
                        std::string line = Trim(line_accum);
                        if (line.rfind("v ", 0) == 0) {
                            std::vector<int> model;
                            std::stringstream ls(line.substr(2));
                            int lit = 0;
                            while (ls >> lit) {
                                if (lit == 0) break;
                                model.push_back(lit);
                            }
                            diag_idx++;
                            auto faulty = ExtractDiag(model);
                            std::string faulty_str = Join(faulty, "|");
                            std::string id = obs[i].id + "[" + std::to_string(diag_idx) + "]";
                            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                            if (out_file.has_value()) {
                                (*out_file) << id << "," << (solved ? 1 : 0) << "," << (optimal ? 1 : 0)
                                            << "," << cost << "," << std::fixed << std::setprecision(6) << elapsed << ","
                                            << cnf.hard.size() << "," << cnf.soft.size() << "," << cnf.max_var << ","
                                            << faulty.size() << "," << faulty_str << "\n";
                            }
                            if (print_table) {
                                std::cout << std::left
                                          << std::setw(14) << id << "|"
                                          << std::setw(8) << (solved ? 1 : 0) << "|"
                                          << std::setw(8) << (optimal ? 1 : 0) << "|"
                                          << std::setw(6) << cost << "|"
                                          << std::setw(10) << std::fixed << std::setprecision(6) << elapsed << "|"
                                          << std::setw(8) << cnf.hard.size() << "|"
                                          << std::setw(8) << cnf.soft.size() << "|"
                                          << std::setw(8) << cnf.max_var << "|"
                                          << std::setw(10) << faulty.size() << "|"
                                          << faulty_str << "\n";
                            }
                        }
                    }
                    pclose(pipe);
                    auto t1 = std::chrono::steady_clock::now();
                    double seconds = std::chrono::duration<double>(t1 - t0).count();

                    if (diag_idx == 0) {
                        if (print_table) {
                            std::cout << std::left
                                      << std::setw(14) << obs[i].id << "|"
                                      << std::setw(8) << (solved ? 1 : 0) << "|"
                                      << std::setw(8) << (optimal ? 1 : 0) << "|"
                                      << std::setw(6) << cost << "|"
                                      << std::setw(10) << std::fixed << std::setprecision(6) << seconds << "|"
                                      << std::setw(8) << cnf.hard.size() << "|"
                                      << std::setw(8) << cnf.soft.size() << "|"
                                      << std::setw(8) << cnf.max_var << "|"
                                      << std::setw(10) << 0 << "|"
                                      << "-\n";
                        }
                    }

                    std::cout << "  " << diag_idx << " diagnoses enumerated in " << std::fixed << std::setprecision(2) << seconds << "s\n";
                }
            }

            if (!out_csv.empty()) {
                std::cout << "Solved " << obs.size() << " scenarios; results written to " << out_csv << "\n";
            } else {
                std::cout << "Solved " << obs.size() << " scenarios\n";
            }
            return 0;
        }

        Usage();
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }
}
