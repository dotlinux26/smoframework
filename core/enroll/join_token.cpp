#include "join_token.hpp"
#include "core/types.hpp"

#include <chrono>
#include <cstring>
#include <random>
#include <sstream>
#include <vector>

namespace smo::enroll {

// =========================================================================
// CBOR encoder/decoder (RFC 7049 — minimal subset for Join Token)
// =========================================================================
namespace cbor {

enum Major : uint8_t {
    Uint    = 0 << 5,
    Bstr    = 2 << 5,
    Text    = 3 << 5,
    Array   = 4 << 5,
    Map     = 5 << 5,
};

static void write_head(Bytes& out, Major mt, uint64_t val) {
    if (val <= 23) {
        out.push_back(static_cast<uint8_t>(mt) | static_cast<uint8_t>(val));
    } else if (val <= 0xFF) {
        out.push_back(static_cast<uint8_t>(mt) | 24);
        out.push_back(static_cast<uint8_t>(val));
    } else if (val <= 0xFFFF) {
        out.push_back(static_cast<uint8_t>(mt) | 25);
        uint16_t n = static_cast<uint16_t>(val);
        out.push_back(static_cast<uint8_t>(n >> 8));
        out.push_back(static_cast<uint8_t>(n));
    } else if (val <= 0xFFFFFFFFULL) {
        out.push_back(static_cast<uint8_t>(mt) | 26);
        uint32_t n = static_cast<uint32_t>(val);
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>((n >> (i * 8)) & 0xFF));
    } else {
        out.push_back(static_cast<uint8_t>(mt) | 27);
        for (int i = 7; i >= 0; --i) out.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

static uint64_t read_int(BytesView data, size_t& off, uint8_t ib) {
    uint8_t extra = ib & 0x1F;
    if (extra <= 23) return extra;
    if (off + (1ULL << (extra - 24)) > data.size()) return 0;
    uint64_t val = 0;
    size_t nbytes = 1ULL << (extra - 24);
    for (size_t i = 0; i < nbytes; ++i)
        val = (val << 8) | data[off++];
    return val;
}

void encode_uint(Bytes& out, uint64_t val) {
    write_head(out, Uint, val);
}

void encode_text(Bytes& out, const std::string& s) {
    write_head(out, Text, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

void encode_bytes(Bytes& out, BytesView data) {
    write_head(out, Bstr, data.size());
    out.insert(out.end(), data.begin(), data.end());
}

void encode_array(Bytes& out, size_t n) {
    write_head(out, Array, n);
}

void encode_map(Bytes& out, size_t n) {
    write_head(out, Map, n);
}

uint64_t decode_uint(BytesView data, size_t& off) {
    if (off >= data.size()) return 0;
    uint8_t ib = data[off++];
    if ((ib >> 5) != 0) return 0;
    return read_int(data, off, ib);
}

std::string decode_text(BytesView data, size_t& off) {
    if (off >= data.size()) return {};
    uint8_t ib = data[off++];
    if ((ib >> 5) != 3) return {};
    uint64_t len = read_int(data, off, ib);
    if (off + len > data.size()) return {};
    std::string s(reinterpret_cast<const char*>(data.data() + off), static_cast<size_t>(len));
    off += static_cast<size_t>(len);
    return s;
}

Bytes decode_bytes(BytesView data, size_t& off) {
    if (off >= data.size()) return {};
    uint8_t ib = data[off++];
    if ((ib >> 5) != 2) return {};
    uint64_t len = read_int(data, off, ib);
    if (off + len > data.size()) return {};
    Bytes b(data.begin() + static_cast<ptrdiff_t>(off),
            data.begin() + static_cast<ptrdiff_t>(off + static_cast<ptrdiff_t>(len)));
    off += static_cast<size_t>(len);
    return b;
}

size_t decode_array(BytesView data, size_t& off) {
    if (off >= data.size()) return 0;
    uint8_t ib = data[off++];
    if ((ib >> 5) != 4) return 0;
    return static_cast<size_t>(read_int(data, off, ib));
}

size_t decode_map(BytesView data, size_t& off) {
    if (off >= data.size()) return 0;
    uint8_t ib = data[off++];
    if ((ib >> 5) != 5) return 0;
    return static_cast<size_t>(read_int(data, off, ib));
}

} // namespace cbor

// =========================================================================
// Base64url
// =========================================================================
namespace {

std::string base64url_encode(BytesView data) {
    static const char kEnc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "abcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < data.size()) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < data.size()) v |= (uint32_t)data[i + 2];
        out += kEnc[(v >> 18) & 0x3f];
        out += kEnc[(v >> 12) & 0x3f];
        out += kEnc[(v >> 6) & 0x3f];
        out += kEnc[v & 0x3f];
    }
    return out;
}

static uint8_t kDec[256] = {};
static const bool kDecInit = []() {
    std::memset(kDec, 0xFF, 256);
    for (int i = 0; i < 26; ++i) {
        kDec[(uint8_t)('A' + i)] = i;
        kDec[(uint8_t)('a' + i)] = 26 + i;
    }
    for (int i = 0; i < 10; ++i) kDec[(uint8_t)('0' + i)] = 52 + i;
    kDec[(uint8_t)'-'] = 62;
    kDec[(uint8_t)'_'] = 63;
    return true;
}();

Bytes base64url_decode(const std::string& s) {
    Bytes out;
    out.reserve(s.size() * 3 / 4);
    uint32_t buf = 0;
    int bits = 0;
    for (uint8_t c : s) {
        if (c == '=') break;
        uint8_t val = kDec[c];
        if (val == 0xFF) continue;
        buf = (buf << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

std::string generate_nonce() {
    std::array<uint8_t, 16> nonce;
    std::random_device rd;
    for (auto& b : nonce) b = static_cast<uint8_t>(rd());
    std::ostringstream oss;
    for (uint8_t b : nonce) oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return oss.str();
}

} // anonymous namespace

// =========================================================================
// Admission serialization (nested CBOR map)
// =========================================================================
static Bytes serialize_admission(const Admission& a) {
    using namespace cbor;
    Bytes out;
    size_t count = 1; // role is always present
    if (!a.profile.empty()) ++count;
    if (a.slot >= 0) ++count;

    encode_map(out, count);

    // 1: role
    encode_uint(out, 1);
    encode_text(out, a.role);

    // 2: profile (optional)
    if (!a.profile.empty()) {
        encode_uint(out, 2);
        encode_text(out, a.profile);
    }

    // 3: slot (optional, -1 = not set)
    if (a.slot >= 0) {
        encode_uint(out, 3);
        encode_uint(out, static_cast<uint64_t>(a.slot));
    }

    return out;
}

static Admission deserialize_admission(BytesView data, size_t& off) {
    using namespace cbor;
    Admission a;
    size_t pairs = decode_map(data, off);
    for (size_t i = 0; i < pairs && off < data.size(); ++i) {
        uint64_t key = decode_uint(data, off);
        if (off > data.size()) break;
        switch (key) {
            case 1: a.role = decode_text(data, off); break;
            case 2: a.profile = decode_text(data, off); break;
            case 3: a.slot = static_cast<int>(decode_uint(data, off)); break;
            default: {
                uint8_t ib = (off < data.size()) ? data[off] : 0;
                uint8_t mt = ib >> 5;
                if (mt == 0 || mt == 1) decode_uint(data, off);
                else if (mt == 3) decode_text(data, off);
                else break;
            }
        }
    }
    return a;
}

// =========================================================================
// JoinToken serialization (CBOR map)
// =========================================================================
Bytes JoinToken::serialize_payload() const {
    using namespace cbor;

    Bytes out;
    size_t count = 7; // version, mesh_id, mesh_epoch, cipher_suite, endpoints, admission, nonce
    if (expiry_unix_sec > 0) ++count;
    if (!issuer.empty()) ++count;
    if (!signature.empty()) ++count;

    encode_map(out, count);

    encode_uint(out, 1); encode_uint(out, version);
    encode_uint(out, 2); encode_text(out, mesh_id);
    encode_uint(out, 3); encode_uint(out, static_cast<uint64_t>(mesh_epoch));
    encode_uint(out, 4); encode_uint(out, static_cast<uint64_t>(cipher_suite_id));

    // 5: endpoints
    encode_uint(out, 5);
    encode_array(out, bootstrap_endpoints.size());
    for (auto& ep : bootstrap_endpoints)
        encode_text(out, ep);

    // 6: admission
    encode_uint(out, 6);
    Bytes admission_bytes = serialize_admission(admission);
    out.insert(out.end(), admission_bytes.begin(), admission_bytes.end());

    // 7: expiry (optional)
    if (expiry_unix_sec > 0) {
        encode_uint(out, 7);
        encode_uint(out, static_cast<uint64_t>(expiry_unix_sec));
    }

    // 8: nonce
    encode_uint(out, 8);
    encode_text(out, nonce);

    // 9: issuer (optional)
    if (!issuer.empty()) {
        encode_uint(out, 9);
        encode_text(out, issuer);
    }

    // 10: signature (optional — omitted during generation, added before wire encoding)
    if (!signature.empty()) {
        encode_uint(out, 10);
        encode_bytes(out, signature);
    }

    return out;
}

Result<JoinToken> JoinToken::deserialize_payload(BytesView data) {
    using namespace cbor;

    JoinToken token;
    size_t off = 0;
    size_t pairs = decode_map(data, off);
    if (pairs == 0) {
        return SMO_ERR_CERT(212, Error, NoRetry, ManualIntervention,
                            "Join Token: expected CBOR map");
    }

    std::vector<std::string> endpoints;

    for (size_t i = 0; i < pairs; ++i) {
        uint64_t key = decode_uint(data, off);
        if (off > data.size()) break;

        switch (key) {
            case 1:
                token.version = static_cast<uint8_t>(decode_uint(data, off));
                break;
            case 2:
                token.mesh_id = decode_text(data, off);
                break;
            case 3:
                token.mesh_epoch = static_cast<int64_t>(decode_uint(data, off));
                break;
            case 4:
                token.cipher_suite_id = static_cast<int>(decode_uint(data, off));
                break;
            case 5: {
                size_t n = decode_array(data, off);
                endpoints.clear();
                for (size_t j = 0; j < n; ++j)
                    endpoints.push_back(decode_text(data, off));
                break;
            }
            case 6: {
                uint8_t ib = (off < data.size()) ? data[off] : 0;
                uint8_t mt = ib >> 5;
                if (mt == 3) {
                    // Old format: role as text (v1)
                    token.admission.role = decode_text(data, off);
                } else {
                    // New format: admission as map (v2+)
                    token.admission = deserialize_admission(data, off);
                }
                break;
            }
            case 7:
                token.expiry_unix_sec = static_cast<int64_t>(decode_uint(data, off));
                break;
            case 8:
                token.nonce = decode_text(data, off);
                break;
            case 9:
                token.issuer = decode_text(data, off);
                break;
            case 10:
                token.signature = decode_bytes(data, off);
                break;
            default: {
                if (off >= data.size()) break;
                uint8_t ib = data[off];
                uint8_t mt = ib >> 5;
                if (mt == 0 || mt == 1) decode_uint(data, off);
                else if (mt == 3 || mt == 2) decode_text(data, off);
                else if (mt == 4) {
                    size_t n = decode_array(data, off);
                    for (size_t j = 0; j < n; ++j) {
                        if (off >= data.size()) break;
                        uint8_t ib2 = data[off];
                        uint8_t mt2 = ib2 >> 5;
                        if (mt2 == 0 || mt2 == 1) decode_uint(data, off);
                        else if (mt2 == 3 || mt2 == 2) decode_text(data, off);
                        else break;
                    }
                }
                break;
            }
        }
    }

    token.bootstrap_endpoints = std::move(endpoints);
    return token;
}

// =========================================================================
// Token generation (v2 — signature-based)
// =========================================================================
Result<JoinToken> generate_token(
    const std::string& mesh_id,
    int64_t mesh_epoch,
    int cipher_suite_id,
    const std::vector<std::string>& bootstrap_endpoints,
    const Admission& admission,
    int64_t expiry_unix_sec,
    const std::string& issuer,
    const SignerImpl& signer,
    BytesView issuer_secret_key,
    RngRef& rng)
{
    JoinToken token;
    token.version = 1;
    token.mesh_id = mesh_id;
    token.mesh_epoch = mesh_epoch;
    token.cipher_suite_id = cipher_suite_id;
    token.bootstrap_endpoints = bootstrap_endpoints;
    token.admission = admission;
    token.expiry_unix_sec = expiry_unix_sec;
    token.nonce = generate_nonce();
    token.issuer = issuer;

    // Sign payload (without signature field)
    auto payload = token.serialize_payload();
    auto sig_result = signer.sign(payload, issuer_secret_key, rng);
    if (!sig_result) {
        return SMO_ERR_CERT(212, Error, NoRetry, RetryOperation,
                            "Join Token signing failed");
    }
    token.signature = std::move(sig_result.value());

    return token;
}

// =========================================================================
// Legacy token generation (v1 — HMAC)
// =========================================================================
Result<JoinToken> generate_token_hmac(
    const std::string& mesh_id,
    int64_t mesh_epoch,
    int cipher_suite_id,
    const std::vector<std::string>& bootstrap_endpoints,
    const std::string& role,
    int64_t expiry_unix_sec,
    const Bytes& hmac_secret,
    const HashImpl& hash)
{
    JoinToken token;
    token.version = 1;
    token.mesh_id = mesh_id;
    token.mesh_epoch = mesh_epoch;
    token.cipher_suite_id = cipher_suite_id;
    token.bootstrap_endpoints = bootstrap_endpoints;
    token.admission.role = role;
    token.expiry_unix_sec = expiry_unix_sec;
    token.nonce = generate_nonce();

    auto payload = token.serialize_payload();
    auto hmac_result = hash.hmac(BytesView(hmac_secret), BytesView(payload));
    if (!hmac_result) {
        return SMO_ERR_CERT(212, Error, NoRetry, RetryOperation,
                            "HMAC computation failed");
    }
    // Store HMAC in signature field for v1 compat
    token.signature = std::move(hmac_result.value());
    return token;
}

// =========================================================================
// Parse wire format
// =========================================================================
Result<JoinToken> parse_token(const std::string& token_str) {
    const char kPrefix[] = "SMO-JOIN-";
    if (token_str.size() < 9 || token_str.compare(0, 9, kPrefix) != 0) {
        return SMO_ERR_CERT(212, Error, NoRetry, ManualIntervention,
                            "Invalid Join Token: missing SMO-JOIN- prefix");
    }

    Bytes raw = base64url_decode(token_str.substr(9));

    // Need at least 1 byte of CBOR + min signature
    if (raw.size() < 33) {
        return SMO_ERR_CERT(212, Error, NoRetry, ManualIntervention,
                            "Join Token too short");
    }

    // Wire format: CBOR payload || sig_len(2 bytes BE) || signature
    // Backward compat (v1, HMAC): CBOR payload || 32 bytes signature (no length)
    //
    // To detect: if remaining bytes > 2 and the 2-byte prefix encodes a
    // plausible sig_len, use the new format. Otherwise assume 32-byte HMAC.
    size_t sig_len = 0;
    BytesView sig_raw;

    // Minimum plausible CBOR map is ~10 bytes (mesh_id + epoch + ...)
    // If remaining bytes > 2, check if 2-byte prefix encodes a valid sig_len
    constexpr size_t kMinPayload = 10;

    if (raw.size() > kMinPayload + 2) {
        // Read 2-byte big-endian sig length
        uint16_t candidate_len = (static_cast<uint16_t>(raw[raw.size() - 2]) << 8) |
                                  static_cast<uint16_t>(raw[raw.size() - 1]);
        size_t total_prefix = static_cast<size_t>(candidate_len) + 2; // len field + signature

        // Plausible: candidate_len in [8, 8192] and total_prefix fits
        if (candidate_len >= 8 && candidate_len <= 8192 &&
            raw.size() > kMinPayload + 2 && raw.size() >= kMinPayload + total_prefix) {
            // New format with length prefix
            sig_len = static_cast<size_t>(candidate_len);
            size_t payload_len = raw.size() - 2 - sig_len;
            BytesView payload(raw.data(), payload_len);
            sig_raw = BytesView(raw.data() + payload_len + 2, sig_len);

            auto token_result = JoinToken::deserialize_payload(payload);
            if (!token_result) return token_result.error();

            token_result.value().signature = Bytes(sig_raw.begin(), sig_raw.end());
            return token_result;
        }
    }

    // Fallback: v1/HMAC format — last 32 bytes are the signature
    size_t payload_len = raw.size() - 32;
    BytesView payload(raw.data(), payload_len);
    sig_raw = BytesView(raw.data() + payload_len, 32);

    auto token_result = JoinToken::deserialize_payload(payload);
    if (!token_result) return token_result.error();

    token_result.value().signature = Bytes(sig_raw.begin(), sig_raw.end());
    return token_result;
}

// =========================================================================
// Validate (v2 — signature-based)
// =========================================================================
Result<void> validate_token(const JoinToken& token,
                             const SignerImpl& signer,
                             BytesView issuer_public_key,
                             const HashImpl& hash)
{
    if (token.expiry_unix_sec > 0) {
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now_sec > token.expiry_unix_sec) {
            return SMO_ERR_CERT(213, Warn, NoRetry, ManualIntervention,
                                "Join Token has expired");
        }
    }

    if (token.issuer.empty()) {
        return SMO_ERR_CERT(212, Error, NoRetry, ManualIntervention,
                            "Join Token v2 requires issuer field");
    }
    if (token.signature.empty()) {
        return SMO_ERR_CERT(212, Error, NoRetry, ManualIntervention,
                            "Join Token has no signature");
    }

    // Verify signature: sign(message=payload, secret_key) → signature
    // Verify: signer.verify(message, signature, public_key) → bool
    auto payload = token.serialize_payload();
    auto verify_result = signer.verify(
        BytesView(payload),
        BytesView(token.signature),
        issuer_public_key
    );
    if (!verify_result) {
        return SMO_ERR_CERT(212, Error, NoRetry, ManualIntervention,
                            "Signature verification failed");
    }
    if (!verify_result.value()) {
        return SMO_ERR_CERT(212, Error, NoRetry, ManualIntervention,
                            "Join Token signature mismatch");
    }

    (void)hash;
    return {};
}

// =========================================================================
// Validate (v1 — HMAC-based, deprecated)
// =========================================================================
Result<void> validate_token_v1(const JoinToken& token, const Bytes& hmac_secret, const HashImpl& hash) {
    if (token.expiry_unix_sec > 0) {
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now_sec > token.expiry_unix_sec) {
            return SMO_ERR_CERT(213, Warn, NoRetry, ManualIntervention,
                                "Join Token has expired");
        }
    }

    auto payload = token.serialize_payload();
    auto hmac_result = hash.hmac(BytesView(hmac_secret), BytesView(payload));
    if (!hmac_result) {
        return SMO_ERR_CERT(212, Error, NoRetry, RetryOperation,
                            "HMAC computation failed");
    }

    auto expected = hmac_result.value();
    if (token.signature.size() != expected.size() ||
        std::memcmp(token.signature.data(), expected.data(), expected.size()) != 0) {
        return SMO_ERR_CERT(212, Error, NoRetry, ManualIntervention,
                            "Join Token HMAC mismatch");
    }

    return {};
}

// =========================================================================
// Detect v1 vs v2 format
// =========================================================================
bool token_is_v1(const JoinToken& token) {
    return token.issuer.empty() && !token.signature.empty();
}

// =========================================================================
// Encode wire format
// =========================================================================
std::string encode_token_wire(const JoinToken& token) {
    // Serialize payload WITHOUT signature first
    JoinToken tmp = token;
    tmp.signature.clear();
    auto payload = tmp.serialize_payload();

    // Wire format: CBOR payload || sig_len(2 bytes BE) || signature
    Bytes full;
    full.insert(full.end(), payload.begin(), payload.end());

    // Append 2-byte big-endian signature length
    uint16_t sig_len = static_cast<uint16_t>(token.signature.size());
    full.push_back(static_cast<uint8_t>((sig_len >> 8) & 0xFF));
    full.push_back(static_cast<uint8_t>(sig_len & 0xFF));

    // Append signature bytes
    full.insert(full.end(), token.signature.begin(), token.signature.end());

    return "SMO-JOIN-" + base64url_encode(full);
}

} // namespace smo::enroll
