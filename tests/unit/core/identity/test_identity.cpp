// SPDX-License-Identifier: Apache-2.0
//
// Identity — unit tests

#include <identity/identity.hpp>
#include <crypto/registry.hpp>
#include <crypto/impl.hpp>
#include <cstdio>
#include <cstring>

using namespace smo;

// ---------------------------------------------------------------------------
// Minimal test runner
// ---------------------------------------------------------------------------
static int failures = 0;

#define TEST(name)                                                      \
    do {                                                                \
        printf("  TEST %-50s ... ", name);                              \
        fflush(stdout);

#define END_TEST(result)                                                \
        if (result) {                                                   \
            printf("PASS\n");                                           \
        } else {                                                        \
            printf("FAIL\n");                                           \
            ++failures;                                                 \
        }                                                               \
    } while (false)

#define ASSERT(cond)                                                    \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("\n    ASSERTION FAILED at %s:%d: %s\n",             \
                   __FILE__, __LINE__, #cond);                          \
            return false;                                               \
        }                                                               \
    } while (false)

#define ASSERT_EQ(a, b)                                                 \
    do {                                                                \
        if ((a) != (b)) {                                               \
            printf("\n    ASSERTION FAILED at %s:%d: %s == %s\n"        \
                   "      LHS=%d  RHS=%d\n",                            \
                   __FILE__, __LINE__, #a, #b,                          \
                   static_cast<int>(a), static_cast<int>(b));           \
            return false;                                               \
        }                                                               \
    } while (false)

// ==========================================================================
// Mock crypto provider (self-contained, mirrors crypto tests)
// ==========================================================================

static uint8_t g_counter = 0;

static void mock_fill(void*, uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; ++i) buf[i] = g_counter++;
}

static Result<Bytes> mock_hash(BytesView data) {
    Bytes out(32, 0);
    // Simple hash: byte 0 = length, rest = XOR of input bytes
    out[0] = static_cast<uint8_t>(data.size());
    for (size_t i = 0; i < data.size(); ++i) out[(i % 31) + 1] ^= data[i];
    return out;
}

static Result<Bytes> mock_hmac(BytesView key, BytesView data) {
    (void)key;
    return mock_hash(data);
}

static Result<Bytes> mock_encrypt(BytesView pt, BytesView aad, BytesView key, BytesView nonce) {
    (void)aad; (void)key; (void)nonce;
    Bytes out(pt.size());
    for (size_t i = 0; i < pt.size(); ++i) out[i] = static_cast<uint8_t>(pt[i] ^ 0xAA);
    return out;
}

static Result<Bytes> mock_decrypt(BytesView ct, BytesView aad, BytesView key, BytesView nonce) {
    (void)aad; (void)key; (void)nonce;
    Bytes out(ct.size());
    for (size_t i = 0; i < ct.size(); ++i) out[i] = static_cast<uint8_t>(ct[i] ^ 0xAA);
    return out;
}

static Result<EncapsResult> mock_encaps(BytesView pubkey, RngRef& rng) {
    (void)pubkey;
    Bytes ct(32); rng.fill(ct);
    Bytes ss(32); rng.fill(ss);
    return EncapsResult{std::move(ct), std::move(ss)};
}

static Result<Bytes> mock_decaps(BytesView privkey, BytesView ciphertext) {
    (void)privkey; (void)ciphertext;
    return Bytes(32, 0x42);
}

static Result<KeypairResult> mock_keygen(RngRef& rng) {
    Bytes pk(32); rng.fill(pk);
    Bytes sk(32); rng.fill(sk);
    return KeypairResult{std::move(pk), std::move(sk)};
}

static Result<Bytes> mock_sign(BytesView msg, BytesView sk, RngRef& rng) {
    (void)sk;
    Bytes sig(64); rng.fill(sig);
    for (size_t i = 0; i < msg.size() && i < 32; ++i) sig[i] ^= msg[i];
    return sig;
}

static Result<bool> mock_verify(BytesView msg, BytesView sig, BytesView pk) {
    (void)msg; (void)sig; (void)pk;
    return true;
}

static const CryptoProvider kMockSuite1{
    1, "Classical",
    nullptr, mock_fill,
    { mock_hash, mock_hmac },
    { nullptr, nullptr },  // perf_hash
    { mock_encrypt, mock_decrypt },
    { mock_keygen, mock_encaps, mock_decaps },
    { mock_keygen, mock_sign, mock_verify }
};

// ==========================================================================
// Tests
// ==========================================================================

static bool test_node_id_from_public_key() {
    Bytes pk(32, 0xAB);
    auto nid = node_id_from_public_key(pk, kMockSuite1.hash);
    ASSERT(nid);
    // Mock hash sets byte 0 = length of input
    ASSERT_EQ(nid.value().value[0], 32);
    return true;
}

static bool test_node_id_empty_hash_fails() {
    HashImpl empty{};
    auto nid = node_id_from_public_key(Bytes{1, 2, 3}, empty);
    ASSERT(!nid);
    ASSERT_EQ(nid.error().code.category, ErrorCategory::Identity);
    return true;
}

static bool test_identity_create() {
    g_counter = 0;
    RngRef rng(nullptr, mock_fill);

    auto id = Identity::create(kMockSuite1, rng);
    ASSERT(id);
    ASSERT_EQ(id.value().suite_id(), 1);
    ASSERT_EQ(id.value().state(), IdentityState::KeypairReady);

    // NodeID should be 32 bytes
    const auto& nid = id.value().node_id();
    bool non_zero = false;
    for (auto b : nid.value) { if (b != 0) non_zero = true; }
    ASSERT(non_zero);

    // Public key should be non-empty
    ASSERT(!id.value().public_key().empty());
    ASSERT(!id.value().secret_key().empty());
    return true;
}

static bool test_identity_load() {
    Bytes pk(32, 0x01);
    Bytes sk(64, 0x02);

    auto id = Identity::load(pk, sk, 1);
    ASSERT(id);
    ASSERT_EQ(id.value().suite_id(), 1);
    ASSERT_EQ(id.value().state(), IdentityState::KeypairReady);
    ASSERT(id.value().public_key().size() == pk.size());
    ASSERT(id.value().secret_key().size() == sk.size());
    return true;
}

static bool test_identity_load_empty_fails() {
    auto id = Identity::load(Bytes{}, Bytes{}, 1);
    ASSERT(!id);
    ASSERT_EQ(id.error().code.category, ErrorCategory::Identity);
    ASSERT_EQ(id.error().code.code, 100);
    return true;
}

static bool test_identity_state_transitions_valid() {
    ASSERT(is_valid_transition(IdentityState::Uninitialized, IdentityState::KeypairReady));
    ASSERT(is_valid_transition(IdentityState::KeypairReady, IdentityState::CertificatePending));
    ASSERT(is_valid_transition(IdentityState::CertificatePending, IdentityState::Enrolled));
    ASSERT(is_valid_transition(IdentityState::Enrolled, IdentityState::Active));
    ASSERT(is_valid_transition(IdentityState::Enrolled, IdentityState::Suspended));
    ASSERT(is_valid_transition(IdentityState::Enrolled, IdentityState::Retired));
    ASSERT(is_valid_transition(IdentityState::Active, IdentityState::Suspended));
    ASSERT(is_valid_transition(IdentityState::Active, IdentityState::Retired));
    ASSERT(is_valid_transition(IdentityState::Active, IdentityState::KeypairReady));
    ASSERT(is_valid_transition(IdentityState::Suspended, IdentityState::Active));
    ASSERT(is_valid_transition(IdentityState::Suspended, IdentityState::Retired));
    return true;
}

static bool test_identity_state_transitions_invalid() {
    // Uninitialized → anything except KeypairReady
    ASSERT(!is_valid_transition(IdentityState::Uninitialized, IdentityState::Active));
    ASSERT(!is_valid_transition(IdentityState::Uninitialized, IdentityState::Retired));

    // Retired → anything (terminal)
    ASSERT(!is_valid_transition(IdentityState::Retired, IdentityState::Active));
    ASSERT(!is_valid_transition(IdentityState::Retired, IdentityState::Uninitialized));

    // Skip states
    ASSERT(!is_valid_transition(IdentityState::KeypairReady, IdentityState::Active));
    ASSERT(!is_valid_transition(IdentityState::CertificatePending, IdentityState::KeypairReady));
    return true;
}

static bool test_identity_transition_to() {
    g_counter = 0;
    RngRef rng(nullptr, mock_fill);

    auto id = Identity::create(kMockSuite1, rng);
    ASSERT(id);

    // Valid transition
    auto r = id.value().transition_to(IdentityState::CertificatePending);
    ASSERT(r);
    ASSERT_EQ(id.value().state(), IdentityState::CertificatePending);

    // Invalid transition should fail
    r = id.value().transition_to(IdentityState::Active);
    ASSERT(!r);
    ASSERT_EQ(r.error().code.category, ErrorCategory::Identity);
    ASSERT_EQ(r.error().code.code, 107);
    // State should remain unchanged
    ASSERT_EQ(id.value().state(), IdentityState::CertificatePending);
    return true;
}

static bool test_identity_to_string() {
    ASSERT(std::strcmp(to_string(IdentityState::Uninitialized), "Uninitialized") == 0);
    ASSERT(std::strcmp(to_string(IdentityState::Active), "Active") == 0);
    ASSERT(std::strcmp(to_string(IdentityState::Retired), "Retired") == 0);
    ASSERT(std::strcmp(to_string(static_cast<IdentityState>(99)), "Unknown") == 0);
    return true;
}

static bool test_identity_multiple_create_gives_different_keys() {
    g_counter = 0;
    RngRef rng(nullptr, mock_fill);

    auto id1 = Identity::create(kMockSuite1, rng);
    ASSERT(id1);

    auto id2 = Identity::create(kMockSuite1, rng);
    ASSERT(id2);

    // Different deterministic counter → different keys
    const auto& pk1 = id1.value().public_key();
    const auto& pk2 = id2.value().public_key();
    bool different = (pk1.size() != pk2.size());
    if (pk1.size() == pk2.size()) {
        different = std::memcmp(pk1.data(), pk2.data(), pk1.size()) != 0;
    }
    ASSERT(different);
    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO Identity — Unit Tests\n");
    printf("==========================\n\n");

    TEST("NodeID from public key")               END_TEST(test_node_id_from_public_key());
    TEST("NodeID with null hash fails")          END_TEST(test_node_id_empty_hash_fails());
    TEST("Identity create")                      END_TEST(test_identity_create());
    TEST("Identity load")                        END_TEST(test_identity_load());
    TEST("Identity load empty fails")            END_TEST(test_identity_load_empty_fails());
    TEST("Valid state transitions")              END_TEST(test_identity_state_transitions_valid());
    TEST("Invalid state transitions")            END_TEST(test_identity_state_transitions_invalid());
    TEST("Identity transition_to")               END_TEST(test_identity_transition_to());
    TEST("Identity to_string")                   END_TEST(test_identity_to_string());
    TEST("Multiple create → different keys")     END_TEST(test_identity_multiple_create_gives_different_keys());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
