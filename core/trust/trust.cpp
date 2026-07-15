#include "trust.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace smo {

// Big-endian helpers
namespace {
void write_u64(Bytes& out, uint64_t v) {
    for (int i = 7; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void write_u32(Bytes& out, uint32_t v) {
    for (int i = 3; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void write_u16(Bytes& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void write_double(Bytes& out, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    write_u64(out, bits);
}

void write_bytes(Bytes& out, const Bytes& data) {
    write_u32(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

uint64_t read_u64(BytesView data, size_t& off) {
    uint64_t v = 0;
    for (int i = 0; i < 8 && off < data.size(); ++i)
        v = (v << 8) | data[off++];
    return v;
}

uint32_t read_u32(BytesView data, size_t& off) {
    uint32_t v = 0;
    for (int i = 0; i < 4 && off < data.size(); ++i)
        v = (v << 8) | data[off++];
    return v;
}

double read_double(BytesView data, size_t& off) {
    uint64_t bits = read_u64(data, off);
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

Bytes read_bytes(BytesView data, size_t& off) {
    uint32_t len = read_u32(data, off);
    if (off + len > data.size()) return {};
    Bytes b(data.begin() + static_cast<ptrdiff_t>(off),
            data.begin() + static_cast<ptrdiff_t>(off + len));
    off += len;
    return b;
}
} // anonymous namespace

// ===========================================================================
// to_string
// ===========================================================================
const char* to_string(TrustLevel l) noexcept {
    switch (l) {
        case TrustLevel::None:     return "None";
        case TrustLevel::Low:      return "Low";
        case TrustLevel::Medium:   return "Medium";
        case TrustLevel::High:     return "High";
        case TrustLevel::Absolute: return "Absolute";
        default:                   return "Unknown";
    }
}

// ===========================================================================
// compute_composite
// ===========================================================================
double compute_composite(const TrustComponents& c) noexcept {
    // Default weights per §XI
    constexpr double w_citizen     = 0.2;
    constexpr double w_execution   = 0.5;
    constexpr double w_witness     = 0.2;
    constexpr double w_consistency = 0.1;

    double s = (c.citizen * w_citizen) +
               (c.execution * w_execution) +
               (c.witness * w_witness) +
               (c.consistency * w_consistency);

    if (s > 1.0) s = 1.0;
    if (s < 0.0) s = 0.0;
    return s;
}

// ===========================================================================
// TrustScore
// ===========================================================================
TrustLevel TrustScore::level() const noexcept {
    return TrustManager::compute_trust_level(composite);
}

Bytes TrustScore::serialize() const {
    Bytes out;
    out.insert(out.end(), node_id.value.begin(), node_id.value.end());
    write_double(out, components.citizen);
    write_double(out, components.execution);
    write_double(out, components.witness);
    write_double(out, components.consistency);
    write_double(out, composite);
    write_u64(out, static_cast<uint64_t>(last_updated));
    return out;
}

Result<TrustScore> TrustScore::deserialize(BytesView data) {
    TrustScore s;
    size_t off = 0;
    if (off + 32 > data.size())
        return SMO_ERR_TRUST(200, Info, RetrySafe, None, "truncated trust score data");
    std::memcpy(s.node_id.value.data(), data.data(), 32);
    off += 32;
    s.components.citizen     = read_double(data, off);
    s.components.execution   = read_double(data, off);
    s.components.witness     = read_double(data, off);
    s.components.consistency = read_double(data, off);
    s.composite              = read_double(data, off);
    s.last_updated           = static_cast<int64_t>(read_u64(data, off));
    return s;
}

// ===========================================================================
// Attestation
// ===========================================================================
Bytes Attestation::serialize() const {
    Bytes out;
    out.insert(out.end(), witness_id.value.begin(), witness_id.value.end());
    out.insert(out.end(), subject_id.value.begin(), subject_id.value.end());
    write_double(out, claimed_score);
    write_u64(out, static_cast<uint64_t>(timestamp));
    write_bytes(out, signature);
    return out;
}

Result<Attestation> Attestation::deserialize(BytesView data) {
    Attestation a;
    size_t off = 0;
    if (off + 64 > data.size())
        return SMO_ERR_TRUST(206, Error, NoRetry, None, "truncated attestation");
    std::memcpy(a.witness_id.value.data(), data.data(), 32);
    off += 32;
    std::memcpy(a.subject_id.value.data(), data.data() + off, 32);
    off += 32;
    a.claimed_score = read_double(data, off);
    a.timestamp     = static_cast<int64_t>(read_u64(data, off));
    a.signature     = read_bytes(data, off);
    return a;
}

// ===========================================================================
// TrustDigest
// ===========================================================================
Bytes TrustDigest::serialize() const {
    Bytes out;
    out.insert(out.end(), origin.value.begin(), origin.value.end());
    write_u64(out, static_cast<uint64_t>(sequence));
    write_u64(out, static_cast<uint64_t>(timestamp));
    write_u32(out, static_cast<uint32_t>(scores.size()));
    for (const auto& s : scores) {
        Bytes ser = s.serialize();
        write_u32(out, static_cast<uint32_t>(ser.size()));
        out.insert(out.end(), ser.begin(), ser.end());
    }
    return out;
}

Result<TrustDigest> TrustDigest::deserialize(BytesView data) {
    TrustDigest d;
    size_t off = 0;
    if (off + 32 > data.size())
        return SMO_ERR_TRUST(203, Warn, NoRetry, None, "truncated digest origin");
    std::memcpy(d.origin.value.data(), data.data(), 32);
    off += 32;
    d.sequence  = static_cast<int64_t>(read_u64(data, off));
    d.timestamp = static_cast<int64_t>(read_u64(data, off));
    uint32_t n = read_u32(data, off);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t slen = read_u32(data, off);
        if (off + slen > data.size()) break;
        auto s = TrustScore::deserialize(data.subspan(off, slen));
        if (!s) break;
        d.scores.push_back(std::move(s.value()));
        off += slen;
    }
    return d;
}

// ===========================================================================
// TrustManager
// ===========================================================================

uint64_t TrustManager::node_id_key(NodeID id) noexcept {
    uint64_t h = 0;
    for (size_t i = 0; i < id.value.size(); i += 8) {
        uint64_t w = 0;
        for (size_t j = 0; j < 8 && (i + j) < id.value.size(); ++j)
            w = (w << 8) | id.value[i + j];
        h ^= w;
    }
    return h;
}

TrustLevel TrustManager::compute_trust_level(double score) noexcept {
    if (score >= 0.9) return TrustLevel::Absolute;
    if (score >= 0.7) return TrustLevel::High;
    if (score >= 0.4) return TrustLevel::Medium;
    if (score >= 0.2) return TrustLevel::Low;
    return TrustLevel::None;
}

void TrustManager::record_success(NodeID node, double weight, int64_t now) {
    auto& ts = scores_[node_id_key(node)];
    ts.node_id = node;
    ts.components.execution += 0.01 * weight;
    if (ts.components.execution > 1.0) ts.components.execution = 1.0;
    if (ts.components.execution < 0.0) ts.components.execution = 0.0;
    ts.composite = compute_composite(ts.components);
    ts.last_updated = now;
}

void TrustManager::record_failure(NodeID node, double weight, int64_t now) {
    auto& ts = scores_[node_id_key(node)];
    ts.node_id = node;
    ts.components.execution -= 0.02 * weight;
    if (ts.components.execution < 0.0) ts.components.execution = 0.0;
    ts.composite = compute_composite(ts.components);
    ts.last_updated = now;
}

void TrustManager::record_offline(NodeID node, int64_t now) {
    auto& ts = scores_[node_id_key(node)];
    ts.node_id = node;
    ts.components.citizen -= config_.citizen_penalty_offline;
    if (ts.components.citizen < 0.0) ts.components.citizen = 0.0;
    ts.composite = compute_composite(ts.components);
    ts.last_updated = now;
}

Result<double> TrustManager::get_score(NodeID node) const {
    auto it = scores_.find(node_id_key(node));
    if (it == scores_.end()) {
        if (is_trust_anchor(node))
            return 1.0;
        return SMO_ERR_TRUST(200, Info, RetrySafe, None, "no trust data");
    }
    return it->second.composite;
}

Result<TrustScore> TrustManager::get_record(NodeID node) const {
    auto it = scores_.find(node_id_key(node));
    if (it == scores_.end()) {
        if (is_trust_anchor(node)) {
            TrustScore ts;
            ts.node_id = node;
            ts.composite = 1.0;
            ts.components.citizen = 1.0;
            ts.components.execution = 1.0;
            ts.components.witness = 1.0;
            ts.components.consistency = 1.0;
            return ts;
        }
        return SMO_ERR_TRUST(200, Info, RetrySafe, None, "no trust data");
    }
    return it->second;
}

std::vector<TrustScore> TrustManager::all_scores() const {
    std::vector<TrustScore> result;
    for (const auto& [key, ts] : scores_)
        result.push_back(ts);
    return result;
}

void TrustManager::add_trust_anchor(TrustAnchor anchor) {
    for (auto& a : anchors_) {
        if (a.node_id == anchor.node_id) {
            a = anchor;
            return;
        }
    }
    anchors_.push_back(std::move(anchor));
}

bool TrustManager::remove_trust_anchor(NodeID node) {
    auto it = std::remove_if(anchors_.begin(), anchors_.end(),
        [&](const TrustAnchor& a) { return a.node_id == node; });
    if (it == anchors_.end()) return false;
    anchors_.erase(it, anchors_.end());
    return true;
}

bool TrustManager::is_trust_anchor(NodeID node) const {
    for (const auto& a : anchors_)
        if (a.node_id == node) return true;
    return false;
}

std::vector<TrustAnchor> TrustManager::trust_anchors() const {
    return anchors_;
}

Result<void> TrustManager::verify_attestation(const Attestation& att,
                                                int64_t now,
                                                int64_t max_age) const
{
    if (att.timestamp <= 0)
        return SMO_ERR_TRUST(208, Warn, NoRetry, None, "attestation missing timestamp");
    if (now - att.timestamp > max_age)
        return SMO_ERR_TRUST(208, Warn, NoRetry, None, "attestation expired");
    if (att.claimed_score < 0.0 || att.claimed_score > 1.0)
        return SMO_ERR_TRUST(206, Error, NoRetry, None, "attestation score out of range");
    if (att.signature.empty())
        return SMO_ERR_TRUST(206, Error, NoRetry, None, "attestation missing signature");
    return {};
}

void TrustManager::apply_attestation(const Attestation& att) {
    auto& ts = scores_[node_id_key(att.subject_id)];
    ts.node_id = att.subject_id;
    // Blend: 70% existing, 30% attestation
    ts.components.witness = (ts.components.witness * 0.7) + (att.claimed_score * 0.3);
    if (ts.components.witness > 1.0) ts.components.witness = 1.0;
    ts.composite = compute_composite(ts.components);
    ts.last_updated = att.timestamp;
}

TrustDigest TrustManager::produce_digest(NodeID origin, int64_t now) {
    TrustDigest d;
    d.origin    = origin;
    d.sequence  = ++digest_seq_;
    d.timestamp = now;
    d.scores    = all_scores();
    return d;
}

Result<void> TrustManager::apply_digest(const TrustDigest& digest) {
    if (digest.scores.empty())
        return SMO_ERR_TRUST(203, Warn, NoRetry, None, "empty digest");
    for (const auto& score : digest.scores) {
        auto k = node_id_key(score.node_id);
        auto it = scores_.find(k);
        if (it == scores_.end()) {
            scores_[k] = score;
        } else if (score.last_updated > it->second.last_updated) {
            it->second = score;
        }
    }
    return {};
}

void TrustManager::tick(int64_t now) {
    // Decay scores over time (half-life model)
    for (auto& [key, ts] : scores_) {
        if (ts.last_updated <= 0) continue;
        int64_t elapsed = now - ts.last_updated;
        if (elapsed <= 0) continue;

        // Convert elapsed to days
        double days = static_cast<double>(elapsed) / 86400000000000.0;
        if (days <= 0) continue;

        // Decay factor = 0.5 ^ (days / half_life_days)
        double factor = std::pow(0.5, days / config_.decay_half_life_days);

        ts.components.citizen     *= factor;
        ts.components.execution   *= factor;
        ts.components.witness     *= factor;
        ts.components.consistency *= factor;
        ts.composite = compute_composite(ts.components);
    }
}

Bytes TrustManager::serialize() const {
    Bytes out;
    // Config
    write_double(out, config_.weight_citizen);
    write_double(out, config_.weight_execution);
    write_double(out, config_.weight_witness);
    write_double(out, config_.weight_consistency);
    write_double(out, config_.decay_half_life_days);
    write_double(out, config_.citizen_penalty_offline);
    write_double(out, config_.requester_penalty_rejected);
    write_double(out, config_.requester_penalty_no_authority);

    write_u64(out, static_cast<uint64_t>(digest_seq_));

    // Scores
    write_u32(out, static_cast<uint32_t>(scores_.size()));
    for (const auto& [key, ts] : scores_) {
        Bytes ser = ts.serialize();
        write_u32(out, static_cast<uint32_t>(ser.size()));
        out.insert(out.end(), ser.begin(), ser.end());
    }

    // Anchors
    write_u32(out, static_cast<uint32_t>(anchors_.size()));
    for (const auto& a : anchors_) {
        out.insert(out.end(), a.node_id.value.begin(), a.node_id.value.end());
        write_bytes(out, a.public_key);
        write_u64(out, static_cast<uint64_t>(a.added_at));
    }

    return out;
}

Result<TrustManager> TrustManager::deserialize(BytesView data) {
    TrustManager tm;
    size_t off = 0;

    tm.config_.weight_citizen      = read_double(data, off);
    tm.config_.weight_execution    = read_double(data, off);
    tm.config_.weight_witness      = read_double(data, off);
    tm.config_.weight_consistency  = read_double(data, off);
    tm.config_.decay_half_life_days= read_double(data, off);
    tm.config_.citizen_penalty_offline  = read_double(data, off);
    tm.config_.requester_penalty_rejected= read_double(data, off);
    tm.config_.requester_penalty_no_authority= read_double(data, off);

    tm.digest_seq_ = static_cast<int64_t>(read_u64(data, off));

    // Scores
    uint32_t n_scores = read_u32(data, off);
    for (uint32_t i = 0; i < n_scores; ++i) {
        uint32_t slen = read_u32(data, off);
        if (off + slen > data.size()) break;
        auto ts = TrustScore::deserialize(data.subspan(off, slen));
        if (!ts) break;
        auto ts_val = std::move(ts.value());
        tm.scores_[tm.node_id_key(ts_val.node_id)] = std::move(ts_val);
        off += slen;
    }

    // Anchors
    uint32_t n_anchors = read_u32(data, off);
    for (uint32_t i = 0; i < n_anchors; ++i) {
        TrustAnchor a;
        if (off + 32 > data.size()) break;
        std::memcpy(a.node_id.value.data(), data.data() + off, 32);
        off += 32;
        a.public_key = read_bytes(data, off);
        a.added_at   = static_cast<int64_t>(read_u64(data, off));
        tm.anchors_.push_back(std::move(a));
    }

    return tm;
}

} // namespace smo
