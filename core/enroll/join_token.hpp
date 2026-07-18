#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "core/types.hpp"
#include "core/errors/error.hpp"
#include "core/crypto/impl.hpp"

namespace smo::enroll {

// ---------------------------------------------------------------------------
// Admission — role + profile + slot for join authorization
// ---------------------------------------------------------------------------
struct Admission {
    std::string role;      // Identity Role (Authority, Member, Contributor, Observer)
    std::string profile;   // Setup profile (server, desktop, embedded, gateway)
    int         slot = -1; // Bootstrap slot index (only for bootstrap authority)
};

// ---------------------------------------------------------------------------
// Join Token v2 — CBOR-encoded bootstrap credential
//
// Wire format:
//   SMO-JOIN-<base64url(CBOR(payload) || signature)>
//
// CBOR payload (map, keyed):
//   1: version         (uint)
//   2: mesh_id         (tstr)
//   3: mesh_epoch      (uint)
//   4: cipher_suite_id (uint)
//   5: endpoints       (array of tstr)
//   6: admission       (map: { role, profile?, slot? })
//   7: expiry          (uint — unix seconds, 0 = no expiry)
//   8: nonce           (tstr — 32 hex chars)
//   9: issuer          (tstr — "root:<fingerprint>" or "authority:<fingerprint>")
//  10: signature       (bstr — issuer's signature over payload)
//
// Signature covers ALL preceding CBOR payload bytes.
// ---------------------------------------------------------------------------

struct JoinToken {
    uint8_t     version = 1;
    std::string mesh_id;
    int64_t     mesh_epoch = 0;
    int         cipher_suite_id = 0;
    std::vector<std::string> bootstrap_endpoints;
    Admission   admission;
    int64_t     expiry_unix_sec = 0;  // 0 = no expiry
    std::string nonce;                // 32 hex chars (16 random bytes)
    std::string issuer;               // "root:<fingerprint>"
    Bytes       signature;            // Raw signature bytes

    Bytes serialize_payload() const;
    static Result<JoinToken> deserialize_payload(BytesView data);
};

// ---- CBOR helpers (internal) -------------------------------------------
namespace cbor {

void encode_uint(Bytes& out, uint64_t val);
void encode_text(Bytes& out, const std::string& s);
void encode_bytes(Bytes& out, BytesView data);
void encode_array(Bytes& out, size_t n);
void encode_map(Bytes& out, size_t n);

uint64_t     decode_uint(BytesView data, size_t& off);
std::string  decode_text(BytesView data, size_t& off);
Bytes        decode_bytes(BytesView data, size_t& off);
size_t       decode_array(BytesView data, size_t& off);
size_t       decode_map(BytesView data, size_t& off);

} // namespace cbor

// ---- Token API -----------------------------------------------------------

// Generate token (v2 with signature). Requires a SignerImpl for signing.
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
    RngRef& rng
);

// Legacy: generate token with HMAC (v1 compat, deprecated)
[[deprecated("Use generate_token with admission+signature instead")]]
Result<JoinToken> generate_token_hmac(
    const std::string& mesh_id,
    int64_t mesh_epoch,
    int cipher_suite_id,
    const std::vector<std::string>& bootstrap_endpoints,
    const std::string& role,
    int64_t expiry_unix_sec,
    const Bytes& hmac_secret,
    const HashImpl& hash
);

Result<JoinToken> parse_token(const std::string& token_str);

// Validate v2 token: verify signature using issuer's public key
// signer.verify(payload, token.signature, issuer_public_key) must return true
Result<void> validate_token(const JoinToken& token,
                             const SignerImpl& signer,
                             BytesView issuer_public_key,
                             const HashImpl& hash);

// Validate v1 token: verify HMAC (deprecated)
Result<void> validate_token_v1(const JoinToken& token,
                                const Bytes& hmac_secret,
                                const HashImpl& hash);

std::string encode_token_wire(const JoinToken& token);

// Detect token format: v1 (HMAC) or v2 (signature)
bool token_is_v1(const JoinToken& token);

} // namespace smo::enroll
