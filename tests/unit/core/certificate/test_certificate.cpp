// SPDX-License-Identifier: Apache-2.0
//
// Certificate — unit tests

#include <certificate/certificate.hpp>
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
// Mock crypto provider
// ==========================================================================

static uint8_t g_counter = 0;

static void mock_fill(void*, uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; ++i) buf[i] = g_counter++;
}

static Result<Bytes> mock_hash(BytesView data) {
    Bytes out(32, 0);
    out[0] = static_cast<uint8_t>(data.size() & 0xFF);
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

static const uint8_t kMockKey = 0x42;

static Result<Bytes> mock_sign(BytesView msg, BytesView sk, RngRef& rng) {
    (void)sk; (void)rng;
    Bytes sig(msg.size());
    for (size_t i = 0; i < msg.size(); ++i) sig[i] = static_cast<uint8_t>(msg[i] ^ kMockKey);
    return sig;
}

static Result<bool> mock_verify(BytesView msg, BytesView sig, BytesView pk) {
    (void)pk;
    if (sig.size() != msg.size()) return false;
    for (size_t i = 0; i < msg.size(); ++i) {
        if (sig[i] != static_cast<uint8_t>(msg[i] ^ kMockKey)) return false;
    }
    return true;
}

static const CryptoProvider kMockSuite1{
    1, "Classical", nullptr, mock_fill,
    { mock_hash, mock_hmac },
    { nullptr, nullptr },  // perf_hash — not used
    { mock_encrypt, mock_decrypt },
    { mock_keygen, mock_encaps, mock_decaps },
    { mock_keygen, mock_sign, mock_verify }
};

// Generate a deterministic keypair from the mock
static bool make_keypair(Bytes& pk, Bytes& sk) {
    RngRef rng(nullptr, mock_fill);
    auto kp = mock_keygen(rng);
    if (!kp) return false;
    pk = std::move(kp.value().public_key);
    sk = std::move(kp.value().secret_key);
    return true;
}

// ==========================================================================
// Tests
// ==========================================================================

static bool test_role_to_string() {
    ASSERT(std::strcmp(to_string(Role::Root), "Root") == 0);
    ASSERT(std::strcmp(to_string(Role::Authority), "Authority") == 0);
    ASSERT(std::strcmp(to_string(Role::Contributor), "Contributor") == 0);
    ASSERT(std::strcmp(to_string(Role::Reader), "Reader") == 0);
    ASSERT(std::strcmp(to_string(Role::Observer), "Observer") == 0);
    ASSERT(std::strcmp(to_string(static_cast<Role>(99)), "Unknown") == 0);
    return true;
}

static bool test_certificate_serialize_roundtrip() {
    Certificate cert;
    cert.mesh_id = Bytes(32, 0xAA);
    cert.subject_pubkey = Bytes(32, 0xBB);
    cert.issuer_pubkey = Bytes(32, 0xCC);
    cert.role = Role::Authority;
    cert.capabilities = Bytes{1, 2, 3};
    cert.epoch = 1;
    cert.not_before = 1000;
    cert.not_after = 2000;

    auto body = cert.serialize();
    ASSERT(!body.empty());

    auto restored = Certificate::deserialize(body);
    ASSERT(restored);
    ASSERT_EQ(restored.value().mesh_id.size(), 32);
    ASSERT(restored.value().mesh_id[0] == 0xAA);
    ASSERT_EQ(restored.value().role, Role::Authority);
    ASSERT_EQ(restored.value().epoch, 1);
    ASSERT_EQ(restored.value().not_before, 1000);
    ASSERT_EQ(restored.value().not_after, 2000);
    return true;
}

static bool test_certificate_deserialize_truncated_fails() {
    Bytes bad = { 0, 0, 0, 1, 0xAA };  // truncated
    auto r = Certificate::deserialize(bad);
    ASSERT(!r);
    ASSERT_EQ(r.error().code.category, ErrorCategory::Certificate);
    return true;
}

static bool test_certificate_cert_hash() {
    Certificate cert;
    cert.mesh_id = Bytes(32, 0x01);
    cert.subject_pubkey = Bytes(32, 0x02);
    cert.role = Role::Contributor;
    cert.not_after = 9999;

    auto h = cert.cert_hash(kMockSuite1.hash);
    ASSERT(h);
    ASSERT_EQ(h.value().size(), 32);
    return true;
}

static bool test_certificate_sign_and_verify() {
    Bytes issuer_pk, issuer_sk;
    ASSERT(make_keypair(issuer_pk, issuer_sk));

    Bytes subject_pk, subject_sk;
    ASSERT(make_keypair(subject_pk, subject_sk));

    Certificate cert;
    cert.mesh_id = Bytes(32, 0xAA);
    cert.subject_pubkey = subject_pk;
    cert.issuer_pubkey = issuer_pk;
    cert.role = Role::Contributor;
    cert.epoch = 1;
    cert.not_before = 0;
    cert.not_after = 9999999999;

    // Sign with issuer's key
    RngRef rng(nullptr, mock_fill);
    auto body = cert.serialize();
    auto sig = mock_sign(body, issuer_sk, rng);
    ASSERT(sig);
    cert.signature = std::move(sig.value());

    auto ok = cert.verify(kMockSuite1.signer);
    ASSERT(ok);
    ASSERT(ok.value());
    return true;
}

static bool test_certificate_verify_bad_signature() {
    Certificate cert;
    cert.mesh_id = Bytes(32, 0xBB);
    cert.subject_pubkey = Bytes(32, 0x01);
    cert.issuer_pubkey = Bytes(32, 0x02);
    cert.role = Role::Reader;
    cert.signature = Bytes{1, 2, 3};  // garbage

    auto ok = cert.verify(kMockSuite1.signer);
    ASSERT(ok);
    ASSERT(!ok.value());  // should verify to false (not error)
    return true;
}

static bool test_certificate_verify_empty_sig_fails() {
    Certificate cert;
    cert.issuer_pubkey = Bytes(32, 0x01);
    // signature is empty by default
    auto ok = cert.verify(kMockSuite1.signer);
    ASSERT(!ok);
    ASSERT_EQ(ok.error().code.code, 204);
    return true;
}

static bool test_certificate_is_valid_at() {
    Certificate cert;
    cert.not_before = 100;
    cert.not_after = 200;

    ASSERT(cert.is_valid_at(100));
    ASSERT(cert.is_valid_at(150));
    ASSERT(cert.is_valid_at(200));
    ASSERT(!cert.is_valid_at(50));
    ASSERT(!cert.is_valid_at(201));
    return true;
}

static bool test_certificate_chain_verify() {
    Bytes root_pk, root_sk;
    ASSERT(make_keypair(root_pk, root_sk));
    Bytes auth_pk, auth_sk;
    ASSERT(make_keypair(auth_pk, auth_sk));
    Bytes node_pk, node_sk;
    ASSERT(make_keypair(node_pk, node_sk));

    RngRef rng(nullptr, mock_fill);

    // Root self-signed cert
    Certificate root_cert;
    root_cert.mesh_id = Bytes(32, 0xAA);
    root_cert.subject_pubkey = root_pk;
    root_cert.issuer_pubkey = root_pk;  // self-signed
    root_cert.role = Role::Root;
    root_cert.not_after = 9999999999;
    {
        auto body = root_cert.serialize();
        auto sig = mock_sign(body, root_sk, rng);
        ASSERT(sig);
        root_cert.signature = std::move(sig.value());
    }

    // Authority cert signed by Root
    Certificate auth_cert;
    auth_cert.mesh_id = Bytes(32, 0xAA);
    auth_cert.subject_pubkey = auth_pk;
    auth_cert.issuer_pubkey = root_pk;
    auth_cert.role = Role::Authority;
    auth_cert.not_after = 9999999999;
    {
        auto body = auth_cert.serialize();
        auto sig = mock_sign(body, root_sk, rng);
        ASSERT(sig);
        auth_cert.signature = std::move(sig.value());
    }

    // Node cert signed by Authority
    Certificate node_cert;
    node_cert.mesh_id = Bytes(32, 0xAA);
    node_cert.subject_pubkey = node_pk;
    node_cert.issuer_pubkey = auth_pk;
    node_cert.role = Role::Contributor;
    node_cert.not_after = 9999999999;
    {
        auto body = node_cert.serialize();
        auto sig = mock_sign(body, auth_sk, rng);
        ASSERT(sig);
        node_cert.signature = std::move(sig.value());
    }

    // Build chain: leaf → auth → root
    CertificateChain chain;
    chain.push_back(std::move(node_cert));
    chain.push_back(std::move(auth_cert));
    chain.push_back(std::move(root_cert));

    auto r = chain.verify(kMockSuite1, root_pk);
    ASSERT(r);
    return true;
}

static bool test_certificate_chain_bad_linkage() {
    Bytes pk_a, sk_a;
    ASSERT(make_keypair(pk_a, sk_a));
    Bytes pk_b, sk_b;
    ASSERT(make_keypair(pk_b, sk_b));

    RngRef rng(nullptr, mock_fill);

    // Root cert
    Certificate root;
    root.subject_pubkey = pk_a;
    root.issuer_pubkey = pk_a;
    root.role = Role::Root;
    root.not_after = 9999999999;
    {
        auto sig = mock_sign(root.serialize(), sk_a, rng);
        ASSERT(sig);
        root.signature = std::move(sig.value());
    }

    // Node cert — issuer_pubkey doesn't match root's subject_pubkey
    Certificate node;
    node.subject_pubkey = pk_b;
    node.issuer_pubkey = pk_b;  // wrong — should be pk_a
    node.role = Role::Reader;
    node.not_after = 9999999999;
    {
        auto sig = mock_sign(node.serialize(), sk_b, rng);
        ASSERT(sig);
        node.signature = std::move(sig.value());
    }

    CertificateChain chain;
    chain.push_back(std::move(node));
    chain.push_back(std::move(root));

    auto r = chain.verify(kMockSuite1, pk_a);
    ASSERT(!r);
    // Linkage check catches the mismatch (issuer != next subject) before signature check
    ASSERT_EQ(r.error().code.code, 217);
    return true;
}

static bool test_certificate_chain_empty_fails() {
    CertificateChain chain;
    auto r = chain.verify(kMockSuite1, Bytes{});
    ASSERT(!r);
    ASSERT_EQ(r.error().code.code, 200);
    return true;
}

static bool test_certificate_chain_valid_at() {
    CertificateChain chain;
    Certificate c1, c2;
    c1.not_before = 0; c1.not_after = 100;
    c2.not_before = 50; c2.not_after = 150;
    chain.push_back(std::move(c1));
    chain.push_back(std::move(c2));

    ASSERT(chain.is_valid_at(75));
    ASSERT(!chain.is_valid_at(25));
    ASSERT(!chain.is_valid_at(125));
    return true;
}

static bool test_csr_serialize_roundtrip() {
    CertificateSigningRequest csr;
    csr.new_public_key = Bytes(32, 0x01);
    csr.mesh_id = Bytes(32, 0x02);
    csr.old_cert_hash = Bytes(32, 0x03);
    csr.display_name = "soc-hn-01";
    csr.platform = "linux";
    csr.version = "3.2.1";
    csr.timestamp = 1234567890;

    auto body = csr.serialize();
    ASSERT(!body.empty());

    auto restored = CertificateSigningRequest::deserialize(body);
    ASSERT(restored);
    ASSERT_EQ(restored.value().timestamp, 1234567890);
    ASSERT(restored.value().new_public_key.size() == 32);
    ASSERT(restored.value().display_name == "soc-hn-01");
    ASSERT(restored.value().platform == "linux");
    ASSERT(restored.value().version == "3.2.1");
    return true;
}

static bool test_csr_sign_and_verify() {
    Bytes pk, sk;
    ASSERT(make_keypair(pk, sk));

    CertificateSigningRequest csr;
    csr.new_public_key = pk;
    csr.mesh_id = Bytes(32, 0xAA);
    csr.display_name = "test-node";
    csr.platform = "linux";
    csr.version = "3.2.1";
    csr.timestamp = 1000;

    RngRef rng(nullptr, mock_fill);
    auto r = csr.sign(kMockSuite1.signer, sk, rng);
    ASSERT(r);
    ASSERT(!csr.signature.empty());

    auto ok = csr.verify(kMockSuite1.signer, pk);
    ASSERT(ok);
    ASSERT(ok.value());
    return true;
}

static bool test_csr_verify_bad_signature() {
    Bytes pk, sk;
    ASSERT(make_keypair(pk, sk));

    CertificateSigningRequest csr;
    csr.new_public_key = pk;
    csr.mesh_id = Bytes(32, 0xBB);
    csr.display_name = "test-node";
    csr.platform = "linux";
    csr.version = "3.2.1";
    csr.timestamp = 1000;
    csr.signature = Bytes{0, 1, 2, 3};  // garbage

    auto ok = csr.verify(kMockSuite1.signer, pk);
    ASSERT(ok);
    ASSERT(!ok.value());  // should return false, not error
    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO Certificate — Unit Tests\n");
    printf("=============================\n\n");

    TEST("Role to_string")                          END_TEST(test_role_to_string());
    TEST("Certificate serialize roundtrip")         END_TEST(test_certificate_serialize_roundtrip());
    TEST("Certificate deserialize truncated fails") END_TEST(test_certificate_deserialize_truncated_fails());
    TEST("Certificate cert_hash")                   END_TEST(test_certificate_cert_hash());
    TEST("Certificate sign + verify")               END_TEST(test_certificate_sign_and_verify());
    TEST("Certificate verify bad signature")        END_TEST(test_certificate_verify_bad_signature());
    TEST("Certificate verify empty sig fails")      END_TEST(test_certificate_verify_empty_sig_fails());
    TEST("Certificate is_valid_at")                 END_TEST(test_certificate_is_valid_at());
    TEST("CertificateChain verify")                 END_TEST(test_certificate_chain_verify());
    TEST("CertificateChain bad linkage")            END_TEST(test_certificate_chain_bad_linkage());
    TEST("CertificateChain empty fails")            END_TEST(test_certificate_chain_empty_fails());
    TEST("CertificateChain is_valid_at")            END_TEST(test_certificate_chain_valid_at());
    TEST("CSR serialize roundtrip")                 END_TEST(test_csr_serialize_roundtrip());
    TEST("CSR sign + verify")                       END_TEST(test_csr_sign_and_verify());
    TEST("CSR verify bad signature")                END_TEST(test_csr_verify_bad_signature());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
