#include "contract_definition.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>

namespace smo {

// ── Minimal JSON writer (canonical: sorted keys, no extra whitespace) ──────

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

static std::string key(const std::string& k) {
    return "\"" + json_escape(k) + "\":";
}

static void append_param(std::ostringstream& os, const ContractParameter& p) {
    os << "{";
    os << key("name") << "\"" << json_escape(p.name) << "\",";
    os << key("type") << "\"" << json_escape(p.type) << "\",";
    os << key("required") << (p.required ? "true" : "false");
    if (p.default_value.has_value()) {
        os << "," << key("default_value") << "\""
           << json_escape(p.default_value.value()) << "\"";
    }
    os << "}";
}

static void append_hints(std::ostringstream& os, const CompilerHints& h) {
    os << "{";
    os << key("max_parallelism") << h.max_parallelism << ",";
    os << key("timeout_sec") << h.timeout_sec << ",";
    os << key("idempotent") << (h.idempotent ? "true" : "false");
    os << "}";
}

std::string ContractDefinition::to_canonical_json() const {
    std::ostringstream os;
    os << "{";

    os << key("capabilities_required") << "{";
    bool first = true;
    for (const auto& [cap, level] : capabilities_required) {
        if (!first) os << ",";
        first = false;
        os << "\"" << json_escape(cap) << "\":" << level;
    }
    os << "},";

    os << key("category") << "\"" << json_escape(category) << "\",";
    os << key("compiler_hints");
    append_hints(os, compiler_hints);
    os << ",";

    os << key("contract_version") << "\"" << json_escape(contract_version) << "\",";
    os << key("description") << "\"" << json_escape(description) << "\",";
    os << key("name") << "\"" << json_escape(name) << "\",";
    os << key("opcode") << "\"" << json_escape(opcode) << "\",";

    os << key("parameters") << "[";
    first = true;
    for (const auto& p : parameters) {
        if (!first) os << ",";
        first = false;
        append_param(os, p);
    }
    os << "],";

    os << key("publisher") << "\"" << json_escape(publisher) << "\",";
    os << key("semver") << "\"" << json_escape(semver) << "\",";

    if (signature.empty()) {
        os << key("signature") << "null";
    } else {
        os << key("signature") << "\"" << json_escape(signature) << "\"";
    }

    os << "}";
    return os.str();
}

ContractID ContractDefinition::compute_id() const {
    auto json = to_canonical_json();
    return ContractID::compute(json);
}

// ── Minimal JSON parser ───────────────────────────────────────────────────

namespace {

struct JsonParser {
    std::string_view s;
    size_t pos = 0;

    void skip_ws() {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' ||
               s[pos] == '\r' || s[pos] == '\t')) pos++;
    }

    char peek() {
        skip_ws();
        return pos < s.size() ? s[pos] : '\0';
    }

    char consume() {
        skip_ws();
        return pos < s.size() ? s[pos++] : '\0';
    }

    bool match(char c) {
        skip_ws();
        if (pos < s.size() && s[pos] == c) { pos++; return true; }
        return false;
    }

    std::string parse_string() {
        if (consume() != '"') return {};
        std::string out;
        while (pos < s.size()) {
            char c = s[pos++];
            if (c == '"') return out;
            if (c == '\\' && pos < s.size()) {
                char n = s[pos++];
                switch (n) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    default: out += n;
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    bool parse_bool() {
        if (s.substr(pos, 4) == "true")  { pos += 4; return true; }
        if (s.substr(pos, 5) == "false") { pos += 5; return false; }
        return false;
    }

    uint32_t parse_number() {
        uint32_t val = 0;
        while (pos < s.size() && std::isdigit(s[pos]))
            val = val * 10 + (s[pos++] - '0');
        return val;
    }

    Result<std::vector<ContractParameter>> parse_params() {
        std::vector<ContractParameter> params;
        if (!match('[')) return params;
        if (match(']')) return params;
        while (true) {
            ContractParameter p;
            if (!match('{')) break;
            while (true) {
                auto k = parse_string();
                if (k.empty()) break;
                if (!match(':')) break;
                if (k == "name") p.name = parse_string();
                else if (k == "type") p.type = parse_string();
                else if (k == "required") p.required = parse_bool();
                else if (k == "default_value") p.default_value = parse_string();
                else if (k == "description") p.description = parse_string();
                else { /* skip unknown */ parse_string(); }
                if (!match(',')) break;
            }
            match('}');
            params.push_back(std::move(p));
            if (!match(',')) break;
        }
        match(']');
        return params;
    }

    Result<CompilerHints> parse_hints() {
        CompilerHints h;
        if (!match('{')) return h;
        while (true) {
            auto k = parse_string();
            if (k.empty()) break;
            if (!match(':')) break;
            if (k == "max_parallelism") h.max_parallelism = parse_number();
            else if (k == "timeout_sec") h.timeout_sec = parse_number();
            else if (k == "idempotent") h.idempotent = parse_bool();
            else { /* skip */ parse_number(); }
            if (!match(',')) break;
        }
        match('}');
        return h;
    }

    Result<std::map<std::string, uint32_t>> parse_capmap() {
        std::map<std::string, uint32_t> m;
        if (!match('{')) return m;
        while (true) {
            auto k = parse_string();
            if (k.empty()) break;
            if (!match(':')) break;
            m[k] = parse_number();
            if (!match(',')) break;
        }
        match('}');
        return m;
    }
};

} // namespace

Result<ContractDefinition> ContractDefinition::from_canonical_json(
    std::string_view json) {
    ContractDefinition def;
    JsonParser p{json, 0};
    if (!p.match('{'))
        return SMO_ERR_CRYPTO(8, Error, NoRetry, None, "expected '{'");
    while (true) {
        auto k = p.parse_string();
        if (k.empty()) break;
        if (!p.match(':'))
            return SMO_ERR_CRYPTO(8, Error, NoRetry, None, "expected ':'");
        if (k == "contract_version") def.contract_version = p.parse_string();
        else if (k == "category") def.category = p.parse_string();
        else if (k == "opcode") def.opcode = p.parse_string();
        else if (k == "name") def.name = p.parse_string();
        else if (k == "description") def.description = p.parse_string();
        else if (k == "publisher") def.publisher = p.parse_string();
        else if (k == "semver") def.semver = p.parse_string();
        else if (k == "parameters") {
            auto res = p.parse_params();
            if (res) def.parameters = std::move(res.value());
        }
        else if (k == "compiler_hints") {
            auto res = p.parse_hints();
            if (res) def.compiler_hints = std::move(res.value());
        }
        else if (k == "capabilities_required") {
            auto res = p.parse_capmap();
            if (res) def.capabilities_required = std::move(res.value());
        }
        else if (k == "signature") {
            if (p.peek() == 'n') { p.pos += 4; def.signature.clear(); }
            else def.signature = p.parse_string();
        }
        else { return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                                      "unknown field in contract"); }
        if (!p.match(',')) break;
    }
    p.match('}');
    def.contract_id = def.compute_id();
    return def;
}

} // namespace smo
