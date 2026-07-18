#include "governance.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>

namespace smo {

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

uint64_t read_u64(BytesView& data, size_t& offset) {
    uint64_t v = 0;
    for (int i = 0; i < 8 && offset < data.size(); ++i)
        v = (v << 8) | data[offset++];
    return v;
}

uint32_t read_u32(BytesView& data, size_t& offset) {
    uint32_t v = 0;
    for (int i = 0; i < 4 && offset < data.size(); ++i)
        v = (v << 8) | data[offset++];
    return v;
}

uint16_t read_u16(BytesView& data, size_t& offset) {
    uint16_t v = 0;
    for (int i = 0; i < 2 && offset < data.size(); ++i)
        v = static_cast<uint16_t>((v << 8) | data[offset++]);
    return v;
}

} // anonymous namespace

const char* to_string(GovernanceTier t) noexcept {
    switch (t) {
        case GovernanceTier::Membership:   return "Membership";
        case GovernanceTier::Constitution: return "Constitution";
        case GovernanceTier::Unanimous:    return "Unanimous";
        default:                           return "Unknown";
    }
}

const char* to_string(GovernanceLevel l) noexcept {
    switch (l) {
        case GovernanceLevel::Local:     return "Local";
        case GovernanceLevel::Authority: return "Authority";
        case GovernanceLevel::Policy:    return "Policy";
        case GovernanceLevel::Critical:  return "Critical";
        case GovernanceLevel::Genesis:   return "Genesis";
        default:                         return "Unknown";
    }
}

const char* to_string(GovernanceAction a) noexcept {
    switch (a) {
        case GovernanceAction::AddAuthority:        return "AddAuthority";
        case GovernanceAction::RemoveAuthority:     return "RemoveAuthority";
        case GovernanceAction::SuspendAuthority:    return "SuspendAuthority";
        case GovernanceAction::ResumeAuthority:     return "ResumeAuthority";
        case GovernanceAction::ChangeMaximum:       return "ChangeMaximum";
        case GovernanceAction::ChangeMinimum:       return "ChangeMinimum";
        case GovernanceAction::ChangeQuorum:        return "ChangeQuorum";
        case GovernanceAction::ChangePolicy:        return "ChangePolicy";
        case GovernanceAction::UpdateManifest:      return "UpdateManifest";
        case GovernanceAction::UpgradeRuntime:      return "UpgradeRuntime";
        case GovernanceAction::ChangeCipherSuite:   return "ChangeCipherSuite";
        case GovernanceAction::ChangeGovernanceRules: return "ChangeGovernanceRules";
        case GovernanceAction::DestroyMesh:         return "DestroyMesh";
        case GovernanceAction::ChangeRecovery:      return "ChangeRecovery";
        case GovernanceAction::CertificateRevocation: return "CertificateRevocation";
        case GovernanceAction::EpochIncrement:      return "EpochIncrement";
        case GovernanceAction::EmergencyLockdown:   return "EmergencyLockdown";
        default:                                    return "Unknown";
    }
}

const char* to_string(ProposalState s) noexcept {
    switch (s) {
        case ProposalState::Draft:      return "Draft";
        case ProposalState::Signing:    return "Signing";
        case ProposalState::Committed:  return "Committed";
        case ProposalState::Rejected:   return "Rejected";
        case ProposalState::Expired:    return "Expired";
        case ProposalState::Conflicted: return "Conflicted";
        default:                        return "Unknown";
    }
}

std::string MeshHealth::to_display() const {
    std::ostringstream os;
    const char* level_str = "Unknown";
    switch (level) {
        case HealthLevel::Healthy:  level_str = "Healthy";   break;
        case HealthLevel::Warning:  level_str = "Warning";   break;
        case HealthLevel::Critical: level_str = "Critical";  break;
        case HealthLevel::Recovery: level_str = "Recovery";  break;
    }

    os << "State:       " << (operational ? "Operational" : "Degraded") << "\n";
    os << "Health:      " << level_str << "\n";
    os << "Authorities: " << online_authorities << "/" << total_authorities
       << " online (min=" << min_required
       << ", preferred=" << preferred
       << ", max=" << maximum << ")\n";
    os << "Offline:     " << offline_authorities << "\n";
    os << "Quorum:      " << current_quorum << "/" << required_quorum
       << " (need " << required_quorum << " to operate)\n";
    os << "Operational: " << (operational ? "YES" : "NO") << "\n";
    os << "Fault tolerance: can tolerate "
       << (offline_authorities >= total_authorities ? 0 : total_authorities - offline_authorities - 1)
       << " more failure(s)\n";

    return os.str();
}

// ===========================================================================
// GovernanceSignature
// ===========================================================================
Bytes GovernanceSignature::serialize() const {
    Bytes out;
    out.insert(out.end(), authority_id.value.begin(), authority_id.value.end());
    write_u32(out, static_cast<uint32_t>(signature.size()));
    out.insert(out.end(), signature.begin(), signature.end());
    write_u64(out, static_cast<uint64_t>(signed_at));
    return out;
}

Result<GovernanceSignature> GovernanceSignature::deserialize(BytesView data) {
    GovernanceSignature gs;
    size_t off = 0;
    if (off + 32 > data.size()) {
        return SMO_ERR_GOVERNANCE(810, Error, NoRetry, None,
                                  "truncated authority_id in signature");
    }
    std::memcpy(gs.authority_id.value.data(), data.data() + off, 32);
    off += 32;
    uint32_t sig_len = read_u32(data, off);
    if (off + sig_len > data.size()) {
        return SMO_ERR_GOVERNANCE(810, Error, NoRetry, None,
                                  "truncated signature data");
    }
    gs.signature = Bytes(data.begin() + static_cast<ptrdiff_t>(off),
                          data.begin() + static_cast<ptrdiff_t>(off + sig_len));
    off += sig_len;
    gs.signed_at = static_cast<int64_t>(read_u64(data, off));
    return gs;
}

// ===========================================================================
// GovernanceProposal
// ===========================================================================
Bytes GovernanceProposal::serialize() const {
    Bytes out;
    write_u64(out, id.value);
    out.push_back(static_cast<uint8_t>(tier));
    out.push_back(static_cast<uint8_t>(level));
    out.push_back(static_cast<uint8_t>(action));
    out.push_back(static_cast<uint8_t>(state));

    write_u32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());

    write_u64(out, static_cast<uint64_t>(created_at));
    write_u64(out, static_cast<uint64_t>(expires_at));
    out.push_back(static_cast<uint8_t>(threshold));

    write_u32(out, static_cast<uint32_t>(signatures.size()));
    for (const auto& sig : signatures) {
        Bytes sig_ser = sig.serialize();
        write_u32(out, static_cast<uint32_t>(sig_ser.size()));
        out.insert(out.end(), sig_ser.begin(), sig_ser.end());
    }

    return out;
}

Result<GovernanceProposal> GovernanceProposal::deserialize(BytesView data) {
    GovernanceProposal prop;
    size_t off = 0;

    prop.id.value = read_u64(data, off);
    if (off + 4 > data.size()) {
        return SMO_ERR_GOVERNANCE(810, Error, NoRetry, None,
                                  "truncated proposal header");
    }
    prop.tier   = static_cast<GovernanceTier>(data[off++]);
    prop.level  = static_cast<GovernanceLevel>(data[off++]);
    prop.action = static_cast<GovernanceAction>(data[off++]);
    prop.state  = static_cast<ProposalState>(data[off++]);

    uint32_t payload_len = read_u32(data, off);
    if (off + payload_len > data.size()) {
        return SMO_ERR_GOVERNANCE(810, Error, NoRetry, None,
                                  "truncated proposal payload");
    }
    prop.payload = Bytes(data.begin() + static_cast<ptrdiff_t>(off),
                          data.begin() + static_cast<ptrdiff_t>(off + payload_len));
    off += payload_len;

    prop.created_at = static_cast<int64_t>(read_u64(data, off));
    prop.expires_at = static_cast<int64_t>(read_u64(data, off));
    prop.threshold  = (off < data.size()) ? static_cast<int>(data[off++]) : 1;

    uint32_t sig_count = read_u32(data, off);
    for (uint32_t i = 0; i < sig_count; ++i) {
        uint32_t sig_len = read_u32(data, off);
        if (off + sig_len > data.size()) break;
        auto sig = GovernanceSignature::deserialize(data.subspan(off, sig_len));
        if (!sig) break;
        prop.signatures.push_back(std::move(sig.value()));
        off += sig_len;
    }

    return prop;
}

// ===========================================================================
// GovernanceEngine
// ===========================================================================
Result<ProposalID> GovernanceEngine::submit(GovernanceProposal proposal) {
    if (proposal.payload.empty()) {
        return SMO_ERR_GOVERNANCE(810, Error, NoRetry, None,
                                  "proposal payload cannot be empty");
    }

    ProposalID pid{next_id_++};
    proposal.id = pid;
    proposal.tier = action_to_tier(proposal.action);
    proposal.state = ProposalState::Signing;
    if (proposal.created_at == 0) proposal.created_at = 0;
    if (proposal.expires_at == 0) {
        // 48h for Constitution, 24h for Membership
        int64_t ttl = (proposal.tier >= GovernanceTier::Constitution)
            ? 172800000000000LL : 86400000000000LL;
        proposal.expires_at = proposal.created_at + ttl;
    }

    proposals_[pid.value] = std::move(proposal);
    return pid;
}

Result<void> GovernanceEngine::sign(ProposalID id, NodeID authority,
                                     Bytes signature, int64_t now)
{
    auto it = proposals_.find(id.value);
    if (it == proposals_.end()) {
        return SMO_ERR_GOVERNANCE(800, Error, NoRetry, GovernanceVote,
                                  "proposal not found");
    }
    auto& prop = it->second;

    if (prop.state != ProposalState::Signing) {
        return SMO_ERR_GOVERNANCE(810, Error, NoRetry, None,
                                  "proposal is not in Signing state");
    }
    if (prop.is_expired(now)) {
        prop.state = ProposalState::Expired;
        return SMO_ERR_GOVERNANCE(810, Error, NoRetry, None,
                                  "proposal has expired");
    }

    for (const auto& s : prop.signatures) {
        if (s.authority_id == authority) {
            return SMO_ERR_GOVERNANCE(800, Error, NoRetry, GovernanceVote,
                                      "authority already signed");
        }
    }

    GovernanceSignature gs;
    gs.authority_id = authority;
    gs.signature = std::move(signature);
    gs.signed_at = now;
    prop.signatures.push_back(std::move(gs));
    return {};
}

Result<void> GovernanceEngine::commit(ProposalID id, int64_t now) {
    auto it = proposals_.find(id.value);
    if (it == proposals_.end()) {
        return SMO_ERR_GOVERNANCE(800, Error, NoRetry, GovernanceVote,
                                  "proposal not found");
    }
    auto& prop = it->second;

    if (prop.state != ProposalState::Signing) {
        return SMO_ERR_GOVERNANCE(810, Error, NoRetry, None,
                                  "proposal is not in Signing state");
    }
    if (prop.is_expired(now)) {
        prop.state = ProposalState::Expired;
        return SMO_ERR_GOVERNANCE(810, Error, NoRetry, None,
                                  "proposal has expired");
    }
    if (!prop.threshold_met()) {
        return SMO_ERR_GOVERNANCE(803, Warn, NoRetry, GovernanceVote,
                                  "signature threshold not met");
    }

    prop.state = ProposalState::Committed;
    return {};
}

Result<void> GovernanceEngine::reject(ProposalID id) {
    auto it = proposals_.find(id.value);
    if (it == proposals_.end()) {
        return SMO_ERR_GOVERNANCE(800, Error, NoRetry, GovernanceVote,
                                  "proposal not found");
    }
    it->second.state = ProposalState::Rejected;
    return {};
}

Result<GovernanceProposal> GovernanceEngine::get(ProposalID id) const {
    auto it = proposals_.find(id.value);
    if (it == proposals_.end()) {
        return SMO_ERR_GOVERNANCE(800, Error, NoRetry, GovernanceVote,
                                  "proposal not found");
    }
    return it->second;
}

std::vector<GovernanceProposal> GovernanceEngine::pending() const {
    std::vector<GovernanceProposal> result;
    for (const auto& [key, prop] : proposals_) {
        if (prop.state == ProposalState::Signing) {
            result.push_back(prop);
        }
    }
    return result;
}

void GovernanceEngine::tick(int64_t now) {
    for (auto& [key, prop] : proposals_) {
        if (prop.state == ProposalState::Signing && prop.is_expired(now)) {
            prop.state = ProposalState::Expired;
        }
    }
}

Bytes GovernanceEngine::serialize_all() const {
    Bytes out;
    write_u64(out, next_id_);
    write_u32(out, static_cast<uint32_t>(proposals_.size()));
    for (const auto& [key, prop] : proposals_) {
        Bytes ser = prop.serialize();
        write_u32(out, static_cast<uint32_t>(ser.size()));
        out.insert(out.end(), ser.begin(), ser.end());
    }
    return out;
}

Bytes GovernanceProposalMsg::serialize() const {
    return proposal.serialize();
}

Result<GovernanceProposalMsg> GovernanceProposalMsg::deserialize(BytesView data) {
    auto p = GovernanceProposal::deserialize(data);
    if (!p) return std::move(p.error());
    GovernanceProposalMsg msg;
    msg.proposal = std::move(p.value());
    return msg;
}

Bytes GovernanceSignatureMsg::serialize() const {
    Bytes out;
    write_u64(out, proposal_id.value);
    Bytes sig = signature.serialize();
    write_u32(out, static_cast<uint32_t>(sig.size()));
    out.insert(out.end(), sig.begin(), sig.end());
    return out;
}

Result<GovernanceSignatureMsg> GovernanceSignatureMsg::deserialize(BytesView data) {
    GovernanceSignatureMsg msg;
    size_t off = 0;
    msg.proposal_id.value = read_u64(data, off);
    uint32_t sig_len = read_u32(data, off);
    auto sig = GovernanceSignature::deserialize(data.subspan(off, sig_len));
    if (!sig) return std::move(sig.error());
    msg.signature = std::move(sig.value());
    return msg;
}

Bytes GovernanceCommitMsg::serialize() const {
    Bytes out;
    write_u64(out, proposal_id.value);
    out.push_back(accepted ? 1 : 0);
    return out;
}

Result<GovernanceCommitMsg> GovernanceCommitMsg::deserialize(BytesView data) {
    GovernanceCommitMsg msg;
    size_t off = 0;
    msg.proposal_id.value = read_u64(data, off);
    msg.accepted = (off < data.size()) ? (data[off] != 0) : false;
    return msg;
}

Bytes EpochIncrementMsg::serialize() const {
    Bytes out;
    write_u64(out, new_epoch);
    write_u32(out, static_cast<uint32_t>(signatures.size()));
    for (const auto& sig : signatures) {
        Bytes sig_ser = sig.serialize();
        write_u32(out, static_cast<uint32_t>(sig_ser.size()));
        out.insert(out.end(), sig_ser.begin(), sig_ser.end());
    }
    return out;
}

Result<EpochIncrementMsg> EpochIncrementMsg::deserialize(BytesView data) {
    EpochIncrementMsg msg;
    size_t off = 0;
    msg.new_epoch = read_u64(data, off);
    uint32_t sig_count = read_u32(data, off);
    for (uint32_t i = 0; i < sig_count; ++i) {
        uint32_t sig_len = read_u32(data, off);
        if (off + sig_len > data.size()) break;
        auto sig = GovernanceSignature::deserialize(data.subspan(off, sig_len));
        if (!sig) break;
        msg.signatures.push_back(std::move(sig.value()));
        off += sig_len;
    }
    return msg;
}

} // namespace smo
