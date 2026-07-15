#include <governance/governance.hpp>
#include <cstdio>
#include <cstring>

using namespace smo;

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
                   "      LHS=%lld  RHS=%lld\n",                        \
                   __FILE__, __LINE__, #a, #b,                          \
                   static_cast<long long>(a),                           \
                   static_cast<long long>(b));                          \
            return false;                                               \
        }                                                               \
    } while (false)

#define ASSERT_STREQ(a, b)                                              \
    do {                                                                \
        const auto& _a = (a);                                           \
        const auto& _b = (b);                                           \
        if (_a != _b) {                                                 \
            printf("\n    ASSERTION FAILED at %s:%d: %s == %s\n"        \
                   "      LHS=\"%s\"  RHS=\"%s\"\n",                     \
                   __FILE__, __LINE__, #a, #b,                          \
                   std::string(_a).c_str(),                             \
                   std::string(_b).c_str());                            \
            return false;                                               \
        }                                                               \
    } while (false)

// ==========================================================================
// Helper: make a NodeID with a given first byte
// ==========================================================================
static NodeID make_node_id(uint8_t first_byte) {
    NodeID id;
    id.value.fill(0);
    id.value[0] = first_byte;
    return id;
}

// ==========================================================================
// Tests — to_string helpers
// ==========================================================================
static bool test_governance_level_to_string() {
    ASSERT(std::strcmp(to_string(GovernanceLevel::Local), "Local") == 0);
    ASSERT(std::strcmp(to_string(GovernanceLevel::Authority), "Authority") == 0);
    ASSERT(std::strcmp(to_string(GovernanceLevel::Policy), "Policy") == 0);
    ASSERT(std::strcmp(to_string(GovernanceLevel::Critical), "Critical") == 0);
    ASSERT(std::strcmp(to_string(GovernanceLevel::Genesis), "Genesis") == 0);
    return true;
}

static bool test_proposal_state_to_string() {
    ASSERT(std::strcmp(to_string(ProposalState::Draft), "Draft") == 0);
    ASSERT(std::strcmp(to_string(ProposalState::Signing), "Signing") == 0);
    ASSERT(std::strcmp(to_string(ProposalState::Committed), "Committed") == 0);
    ASSERT(std::strcmp(to_string(ProposalState::Rejected), "Rejected") == 0);
    ASSERT(std::strcmp(to_string(ProposalState::Expired), "Expired") == 0);
    return true;
}

static bool test_governance_action_to_string() {
    ASSERT(std::strcmp(to_string(GovernanceAction::PolicyChange), "PolicyChange") == 0);
    ASSERT(std::strcmp(to_string(GovernanceAction::AuthorityCreate), "AuthorityCreate") == 0);
    ASSERT(std::strcmp(to_string(GovernanceAction::AuthorityRevoke), "AuthorityRevoke") == 0);
    ASSERT(std::strcmp(to_string(GovernanceAction::EpochIncrement), "EpochIncrement") == 0);
    ASSERT(std::strcmp(to_string(GovernanceAction::EmergencyLockdown), "EmergencyLockdown") == 0);
    return true;
}

// ==========================================================================
// Tests — GovernanceSignature serialization
// ==========================================================================
static bool test_governance_signature_roundtrip() {
    GovernanceSignature sig;
    sig.authority_id = make_node_id(0xAA);
    sig.signature = {0x01, 0x02, 0x03, 0x04};
    sig.signed_at = 1234567890;

    auto ser = sig.serialize();
    ASSERT(!ser.empty());

    auto deser = GovernanceSignature::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().authority_id.value[0], 0xAA);
    ASSERT_EQ(deser.value().signature.size(), 4U);
    ASSERT_EQ(deser.value().signature[0], 0x01);
    ASSERT_EQ(deser.value().signature[3], 0x04);
    ASSERT_EQ(deser.value().signed_at, 1234567890);

    return true;
}

// ==========================================================================
// Tests — GovernanceProposal serialization
// ==========================================================================
static bool test_governance_proposal_roundtrip() {
    GovernanceProposal prop;
    prop.id = {42};
    prop.level = GovernanceLevel::Policy;
    prop.action = GovernanceAction::AuthorityCreate;
    prop.payload = {'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    prop.created_at = 1000;
    prop.expires_at = 2000;
    prop.state = ProposalState::Signing;
    prop.threshold = 2;

    GovernanceSignature sig;
    sig.authority_id = make_node_id(0xBB);
    sig.signature = {0x10, 0x20};
    sig.signed_at = 1500;
    prop.signatures.push_back(sig);

    auto ser = prop.serialize();
    ASSERT(!ser.empty());

    auto deser = GovernanceProposal::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().id.value, 42U);
    ASSERT_EQ(deser.value().level, GovernanceLevel::Policy);
    ASSERT_EQ(deser.value().action, GovernanceAction::AuthorityCreate);
    ASSERT_EQ(deser.value().state, ProposalState::Signing);
    ASSERT_EQ(deser.value().threshold, 2);
    ASSERT_EQ(deser.value().created_at, 1000);
    ASSERT_EQ(deser.value().expires_at, 2000);
    ASSERT_EQ(deser.value().signatures.size(), 1U);
    ASSERT_EQ(deser.value().signatures[0].authority_id.value[0], 0xBB);
    ASSERT_EQ(deser.value().signatures[0].signed_at, 1500);

    return true;
}

// ==========================================================================
// Tests — Proposal helpers
// ==========================================================================
static bool test_threshold_met() {
    GovernanceProposal prop;
    prop.threshold = 2;
    ASSERT(!prop.threshold_met());  // 0 < 2

    GovernanceSignature sig;
    sig.authority_id = make_node_id(0x01);
    prop.signatures.push_back(sig);
    ASSERT(!prop.threshold_met());  // 1 < 2

    prop.signatures.push_back(sig);
    ASSERT(prop.threshold_met());   // 2 >= 2

    return true;
}

static bool test_is_expired() {
    GovernanceProposal prop;
    prop.expires_at = 1000;
    ASSERT(!prop.is_expired(500));
    ASSERT(prop.is_expired(1000));
    ASSERT(prop.is_expired(2000));

    // expires_at == 0 means no expiry
    prop.expires_at = 0;
    ASSERT(!prop.is_expired(1000000));

    return true;
}

// ==========================================================================
// Tests — GovernanceEngine
// ==========================================================================
static bool test_engine_submit() {
    GovernanceEngine engine;

    GovernanceProposal prop;
    prop.level = GovernanceLevel::Authority;
    prop.action = GovernanceAction::PolicyChange;
    prop.payload = {'x'};

    auto id = engine.submit(prop);
    ASSERT(id);  // Should succeed
    ASSERT_EQ(id.value().value, 1U);  // First ID should be 1
    ASSERT_EQ(engine.count(), 1U);

    return true;
}

static bool test_engine_submit_invalid_level() {
    GovernanceEngine engine;

    GovernanceProposal prop;
    prop.level = static_cast<GovernanceLevel>(99);  // invalid
    prop.payload = {'x'};

    auto id = engine.submit(prop);
    ASSERT(!id);  // Should fail

    return true;
}

static bool test_engine_submit_empty_payload() {
    GovernanceEngine engine;

    GovernanceProposal prop;
    prop.level = GovernanceLevel::Authority;

    auto id = engine.submit(prop);
    ASSERT(!id);  // Should fail — empty payload

    return true;
}

static bool test_engine_sign_and_commit() {
    GovernanceEngine engine;

    GovernanceProposal prop;
    prop.level = GovernanceLevel::Authority;
    prop.action = GovernanceAction::PolicyChange;
    prop.payload = {'x'};

    auto id_res = engine.submit(prop);
    ASSERT(id_res);
    auto id = id_res.value();

    // Sign
    Bytes sig_data = {0xde, 0xad};
    ASSERT(engine.sign(id, make_node_id(0x01), sig_data, 500));

    // Commit — should work since threshold=1 matches 1 signature
    ASSERT(engine.commit(id, 600));

    // Verify committed
    auto prop2 = engine.get(id);
    ASSERT(prop2);
    ASSERT_EQ(prop2.value().state, ProposalState::Committed);

    return true;
}

static bool test_engine_sign_threshold_not_met() {
    GovernanceEngine engine;

    GovernanceProposal prop;
    prop.level = GovernanceLevel::Policy;
    prop.action = GovernanceAction::PolicyChange;
    prop.payload = {'x'};
    prop.threshold = 3;

    auto id = engine.submit(prop);
    ASSERT(id);

    // Sign twice (still not enough)
    ASSERT(engine.sign(id.value(), make_node_id(0x01), {0x01}, 100));
    ASSERT(engine.sign(id.value(), make_node_id(0x02), {0x02}, 200));

    // Commit should fail — only 2/3
    auto res = engine.commit(id.value(), 300);
    ASSERT(!res);
    ASSERT_EQ(res.error().code.code, 803);

    return true;
}

static bool test_engine_duplicate_signer() {
    GovernanceEngine engine;

    GovernanceProposal prop;
    prop.level = GovernanceLevel::Authority;
    prop.payload = {'x'};

    auto id = engine.submit(prop);
    ASSERT(id);

    ASSERT(engine.sign(id.value(), make_node_id(0x01), {0x01}, 100));

    // Same authority signs again
    auto res = engine.sign(id.value(), make_node_id(0x01), {0x02}, 200);
    ASSERT(!res);

    return true;
}

static bool test_engine_reject() {
    GovernanceEngine engine;

    GovernanceProposal prop;
    prop.level = GovernanceLevel::Authority;
    prop.payload = {'x'};

    auto id = engine.submit(prop);
    ASSERT(id);

    ASSERT(engine.reject(id.value()));

    auto prop2 = engine.get(id.value());
    ASSERT(prop2);
    ASSERT_EQ(prop2.value().state, ProposalState::Rejected);

    // Commit rejected proposal should fail
    ASSERT(!engine.commit(id.value(), 100));

    return true;
}

static bool test_engine_get_not_found() {
    GovernanceEngine engine;
    auto res = engine.get({999});
    ASSERT(!res);
    ASSERT_EQ(res.error().code.code, 800);

    return true;
}

static bool test_engine_pending() {
    GovernanceEngine engine;

    GovernanceProposal p1; p1.level = GovernanceLevel::Authority; p1.payload = {'a'};
    GovernanceProposal p2; p2.level = GovernanceLevel::Policy;   p2.payload = {'b'};
    GovernanceProposal p3; p3.level = GovernanceLevel::Local;    p3.payload = {'c'};

    auto id1 = engine.submit(p1);
    auto id2 = engine.submit(p2);
    auto id3 = engine.submit(p3);
    ASSERT(id1); ASSERT(id2); ASSERT(id3);

    ASSERT_EQ(engine.pending().size(), 3U);

    // Reject one - should no longer be pending
    ASSERT(engine.reject(id1.value()));
    ASSERT_EQ(engine.pending().size(), 2U);

    return true;
}

static bool test_engine_tick_expiry() {
    GovernanceEngine engine;

    GovernanceProposal prop;
    prop.level = GovernanceLevel::Authority;
    prop.payload = {'x'};
    prop.created_at = 0;
    // Default expires_at will be set by submit(): created_at + 24h = 86400000000000
    // So we need a custom expires_at:
    prop.expires_at = 1000;
    auto id_res = engine.submit(prop);
    ASSERT(id_res);
    auto id = id_res.value();

    // Tick at time 2000 — should expire
    engine.tick(2000);

    auto prop2 = engine.get(id);
    ASSERT(prop2);
    ASSERT_EQ(prop2.value().state, ProposalState::Expired);

    return true;
}

static bool test_engine_serialize_all() {
    GovernanceEngine engine;

    GovernanceProposal p1; p1.level = GovernanceLevel::Authority; p1.payload = {'a'};
    GovernanceProposal p2; p2.level = GovernanceLevel::Policy;   p2.payload = {'b'};
    engine.submit(p1);
    engine.submit(p2);

    auto ser = engine.serialize_all();
    ASSERT(!ser.empty());
    ASSERT(ser.size() > 0);

    return true;
}

// ==========================================================================
// Tests — Wire messages
// ==========================================================================
static bool test_proposal_msg_roundtrip() {
    GovernanceProposalMsg msg;
    msg.proposal.level = GovernanceLevel::Authority;
    msg.proposal.action = GovernanceAction::EpochIncrement;
    msg.proposal.payload = {'e', 'p', 'o', 'c', 'h'};
    msg.proposal.id = {7};
    msg.proposal.created_at = 500;
    msg.proposal.expires_at = 2000;
    msg.proposal.state = ProposalState::Signing;
    msg.proposal.threshold = 1;

    auto ser = msg.serialize();
    ASSERT(!ser.empty());

    auto deser = GovernanceProposalMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().proposal.id.value, 7U);
    ASSERT_EQ(deser.value().proposal.action, GovernanceAction::EpochIncrement);
    ASSERT_EQ(deser.value().proposal.threshold, 1);

    return true;
}

static bool test_signature_msg_roundtrip() {
    GovernanceSignatureMsg msg;
    msg.proposal_id = {42};
    msg.signature.authority_id = make_node_id(0xCC);
    msg.signature.signature = {0x11, 0x22, 0x33};
    msg.signature.signed_at = 99;

    auto ser = msg.serialize();
    ASSERT(!ser.empty());

    auto deser = GovernanceSignatureMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().proposal_id.value, 42U);
    ASSERT_EQ(deser.value().signature.authority_id.value[0], 0xCC);
    ASSERT_EQ(deser.value().signature.signature.size(), 3U);
    ASSERT_EQ(deser.value().signature.signed_at, 99);

    return true;
}

static bool test_commit_msg_roundtrip() {
    GovernanceCommitMsg msg;
    msg.proposal_id = {17};
    msg.accepted = true;

    auto ser = msg.serialize();
    ASSERT(!ser.empty());

    auto deser = GovernanceCommitMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().proposal_id.value, 17U);
    ASSERT(deser.value().accepted);

    return true;
}

static bool test_epoch_increment_msg_roundtrip() {
    EpochIncrementMsg msg;
    msg.new_epoch = 5;

    GovernanceSignature sig;
    sig.authority_id = make_node_id(0xDD);
    sig.signature = {0xaa, 0xbb};
    sig.signed_at = 123;
    msg.signatures.push_back(sig);

    auto ser = msg.serialize();
    ASSERT(!ser.empty());

    auto deser = EpochIncrementMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().new_epoch, 5U);
    ASSERT_EQ(deser.value().signatures.size(), 1U);
    ASSERT_EQ(deser.value().signatures[0].authority_id.value[0], 0xDD);

    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO Governance — Unit Tests\n");
    printf("============================\n\n");

    TEST("GovernanceLevel to_string")                       END_TEST(test_governance_level_to_string());
    TEST("ProposalState to_string")                         END_TEST(test_proposal_state_to_string());
    TEST("GovernanceAction to_string")                      END_TEST(test_governance_action_to_string());
    TEST("GovernanceSignature roundtrip")                   END_TEST(test_governance_signature_roundtrip());
    TEST("GovernanceProposal roundtrip")                    END_TEST(test_governance_proposal_roundtrip());
    TEST("threshold_met")                                   END_TEST(test_threshold_met());
    TEST("is_expired")                                      END_TEST(test_is_expired());
    TEST("Engine submit")                                   END_TEST(test_engine_submit());
    TEST("Engine submit invalid level")                     END_TEST(test_engine_submit_invalid_level());
    TEST("Engine submit empty payload")                     END_TEST(test_engine_submit_empty_payload());
    TEST("Engine sign and commit")                          END_TEST(test_engine_sign_and_commit());
    TEST("Engine sign threshold not met")                   END_TEST(test_engine_sign_threshold_not_met());
    TEST("Engine duplicate signer")                         END_TEST(test_engine_duplicate_signer());
    TEST("Engine reject")                                   END_TEST(test_engine_reject());
    TEST("Engine get not found")                            END_TEST(test_engine_get_not_found());
    TEST("Engine pending")                                  END_TEST(test_engine_pending());
    TEST("Engine tick expiry")                              END_TEST(test_engine_tick_expiry());
    TEST("Engine serialize_all")                            END_TEST(test_engine_serialize_all());
    TEST("ProposalMsg roundtrip")                           END_TEST(test_proposal_msg_roundtrip());
    TEST("SignatureMsg roundtrip")                          END_TEST(test_signature_msg_roundtrip());
    TEST("CommitMsg roundtrip")                             END_TEST(test_commit_msg_roundtrip());
    TEST("EpochIncrementMsg roundtrip")                     END_TEST(test_epoch_increment_msg_roundtrip());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
