// SPDX-License-Identifier: MIT
// Bgen tests - Session Analyzer

#include <catch2/catch_test_macros.hpp>
#include "../src/model/ast_builder.hpp"
#include "../src/analyzer/type_resolver.hpp"
#include "../src/analyzer/session_analyzer.hpp"
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

static std::string fixture_path(const std::string& name) {
    return (fs::path(BGEN_TEST_FIXTURES_DIR) / name).string();
}

// Helper: build protocol from fixture file and analyze sessions
static std::pair<bgen::model::Protocol, std::vector<bgen::analyzer::SessionInfo>>
analyze_fixture(const std::string& fixture_name) {
    auto build_result = bgen::model::build_protocol(fixture_path(fixture_name));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto sessions = bgen::analyzer::analyze_sessions(protocol, index);
    return {std::move(protocol), std::move(sessions)};
}

TEST_CASE("Choice protocol leaf discovery", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "Frame");

    // Should find AlphaBody and BetaBody as leaves
    REQUIRE(si.leaf_types.size() == 2);

    bool found_alpha = false, found_beta = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "AlphaBody") {
            found_alpha = true;
            CHECK_FALSE(lt.send_only);
            CHECK_FALSE(lt.receive_only);
            // Should have access_path through "body" choice with discriminator
            REQUIRE(lt.access_path.size() == 1);
            CHECK(lt.access_path[0].choice_field == "body");
            CHECK(lt.access_path[0].variant_type == "AlphaBody");
            CHECK(lt.access_path[0].disc_field == "message-type");
            CHECK(lt.access_path[0].disc_value == "MSG_ALPHA");
        }
        if (lt.name == "BetaBody") {
            found_beta = true;
            CHECK_FALSE(lt.send_only);
            CHECK(lt.receive_only);
            // Should have access_path with discriminator
            REQUIRE(lt.access_path.size() == 1);
            CHECK(lt.access_path[0].disc_field == "message-type");
            CHECK(lt.access_path[0].disc_value == "MSG_BETA");
        }
    }
    CHECK(found_alpha);
    CHECK(found_beta);
}

TEST_CASE("Sync pattern extraction", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // SYNC = 0xBEEF -> 2 bytes: 0xBE, 0xEF
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xBE);
    CHECK(si.sync_pattern[1] == 0xEF);
}

TEST_CASE("Frame length expression detection", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // "length" field should be detected as frame length expr
    CHECK(si.frame_length_expr == "length");
}

TEST_CASE("Nested choice leaf discovery", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("nested_choice.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "Envelope");

    // Nested choices: CategoryA has sub-choice -> LeafX, LeafY
    // CategoryB is a direct leaf
    // So total leaves: LeafX, LeafY, CategoryB = 3
    REQUIRE(si.leaf_types.size() == 3);

    bool found_leafx = false, found_leafy = false, found_catb = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "LeafX") {
            found_leafx = true;
            // Access path: payload -> CategoryA (disc: category=CAT_A),
            //              then sub-body -> LeafX (disc: sub-type=SUB_X)
            REQUIRE(lt.access_path.size() == 2);
            CHECK(lt.access_path[0].choice_field == "payload");
            CHECK(lt.access_path[0].variant_type == "CategoryA");
            CHECK(lt.access_path[0].disc_field == "category");
            CHECK(lt.access_path[0].disc_value == "CAT_A");
            CHECK(lt.access_path[1].choice_field == "sub-body");
            CHECK(lt.access_path[1].variant_type == "LeafX");
            CHECK(lt.access_path[1].disc_field == "sub-type");
            CHECK(lt.access_path[1].disc_value == "SUB_X");
        }
        if (lt.name == "LeafY") {
            found_leafy = true;
            REQUIRE(lt.access_path.size() == 2);
            CHECK(lt.access_path[1].disc_field == "sub-type");
            CHECK(lt.access_path[1].disc_value == "SUB_Y");
        }
        if (lt.name == "CategoryB") {
            found_catb = true;
            REQUIRE(lt.access_path.size() == 1);
            CHECK(lt.access_path[0].choice_field == "payload");
            CHECK(lt.access_path[0].disc_field == "category");
            CHECK(lt.access_path[0].disc_value == "CAT_B");
        }
    }
    CHECK(found_leafx);
    CHECK(found_leafy);
    CHECK(found_catb);
}

TEST_CASE("Leaf annotations propagation", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    for (const auto& lt : si.leaf_types) {
        if (lt.name == "AlphaBody") {
            // AlphaBody struct has annotation group=control
            REQUIRE(lt.annotations.size() == 1);
            CHECK(lt.annotations[0].first == "group");
            CHECK(lt.annotations[0].second == "control");
        }
        if (lt.name == "BetaBody") {
            REQUIRE(lt.annotations.size() == 1);
            CHECK(lt.annotations[0].first == "group");
            CHECK(lt.annotations[0].second == "data");
        }
    }
}

TEST_CASE("Type ID generation is deterministic", "[session_analyzer]") {
    auto [protocol1, sessions1] = analyze_fixture("choice_protocol.bmdl.xml");
    auto [protocol2, sessions2] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions1.size() == sessions2.size());
    for (size_t i = 0; i < sessions1.size(); i++) {
        REQUIRE(sessions1[i].leaf_types.size() == sessions2[i].leaf_types.size());
        for (size_t j = 0; j < sessions1[i].leaf_types.size(); j++) {
            CHECK(sessions1[i].leaf_types[j].type_id == sessions2[i].leaf_types[j].type_id);
        }
    }
}

TEST_CASE("Otherwise case creates leaf", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("otherwise_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "Frame");

    // Should find BodyA and FallbackBody as leaves
    bool found_body_a = false, found_fallback = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "BodyA") found_body_a = true;
        if (lt.name == "FallbackBody") found_fallback = true;
    }
    CHECK(found_body_a);
    CHECK(found_fallback);
}

TEST_CASE("Send-only leaf excluded from decode", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("send_only_leaf.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    bool found_send = false, found_recv = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "SendBody") {
            found_send = true;
            CHECK(lt.send_only);
            CHECK_FALSE(lt.receive_only);
        }
        if (lt.name == "RecvBody") {
            found_recv = true;
            CHECK_FALSE(lt.send_only);
            CHECK(lt.receive_only);
        }
    }
    CHECK(found_send);
    CHECK(found_recv);
}

TEST_CASE("Auto-sequence fixture session structure", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("auto_sequence.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "Frame");

    // Should discover BodyA as a leaf
    REQUIRE(si.leaf_types.size() == 1);
    CHECK(si.leaf_types[0].name == "BodyA");
    REQUIRE(si.leaf_types[0].access_path.size() == 1);
    CHECK(si.leaf_types[0].access_path[0].choice_field == "body");
    CHECK(si.leaf_types[0].access_path[0].disc_value == "MSG_A");
}

TEST_CASE("Sync pattern from auto-sequence fixture", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("auto_sequence.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // sync_pattern should be 0xBEEF -> [0xBE, 0xEF]
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xBE);
    CHECK(si.sync_pattern[1] == 0xEF);

    // frame_length_expr should be empty (no length field in this fixture)
    CHECK(si.frame_length_expr.empty());
}

TEST_CASE("Session protocol session analysis", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("session_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "Packet");

    // SYNC = 0xDEAD -> [0xDE, 0xAD]
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xDE);
    CHECK(si.sync_pattern[1] == 0xAD);

    // Frame length expr should be "length"
    CHECK(si.frame_length_expr == "length");

    // Leaves: PingBody, DataBody (send/both), AckBody (receive)
    // DataBody should not be send_only, AckBody should be receive_only
    bool found_ping = false, found_data = false, found_ack = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "PingBody") {
            found_ping = true;
            CHECK_FALSE(lt.send_only);
            CHECK_FALSE(lt.receive_only);
        }
        if (lt.name == "DataBody") {
            found_data = true;
            CHECK_FALSE(lt.send_only);
            CHECK_FALSE(lt.receive_only);
        }
        if (lt.name == "AckBody") {
            found_ack = true;
            CHECK_FALSE(lt.send_only);
            CHECK(lt.receive_only);
        }
    }
    CHECK(found_ping);
    CHECK(found_data);
    CHECK(found_ack);
}

TEST_CASE("Auto fields found for increment", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("auto_sequence.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // Each leaf should have auto_fields containing "seq"
    for (const auto& lt : si.leaf_types) {
        bool has_seq_auto = false;
        for (const auto& af : lt.auto_fields) {
            if (af == "seq") has_seq_auto = true;
        }
        CHECK(has_seq_auto);
    }
}

TEST_CASE("Constraint fields found on entry-point", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("auto_sequence.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // sync_pattern should be 0xBEEF -> [0xBE, 0xEF]
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xBE);
    CHECK(si.sync_pattern[1] == 0xEF);

    // Each leaf should have constraint for "sync" field
    for (const auto& lt : si.leaf_types) {
        bool has_sync_constraint = false;
        for (const auto& [field, val] : lt.constraints) {
            if (field == "sync") has_sync_constraint = true;
        }
        CHECK(has_sync_constraint);
    }
}

// ============================================================================
// Inline struct entry-point field propagation
// ============================================================================

TEST_CASE("Inline struct auto fields propagated to leaves", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("inline_struct.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "Frame");

    // Should find BodyX and BodyY as leaves
    REQUIRE(si.leaf_types.size() == 2);

    // Each leaf should have "seq" in auto_fields (from inlined Header)
    for (const auto& lt : si.leaf_types) {
        bool has_seq = false;
        for (const auto& af : lt.auto_fields) {
            if (af == "seq") has_seq = true;
        }
        CHECK(has_seq);
    }
}

TEST_CASE("Inline struct constraint fields propagated to leaves", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("inline_struct.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // Each leaf should have constraint for "sync" field (from inlined Header)
    for (const auto& lt : si.leaf_types) {
        bool has_sync = false;
        for (const auto& [field, val] : lt.constraints) {
            if (field == "sync") has_sync = true;
        }
        CHECK(has_sync);
    }
}

TEST_CASE("Inline struct sync pattern extraction", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("inline_struct.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // sync_pattern should be 0xCAFE -> [0xCA, 0xFE]
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xCA);
    CHECK(si.sync_pattern[1] == 0xFE);
}

TEST_CASE("Inline struct frame length expression detection", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("inline_struct.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // "length" field inside inlined Header should be detected
    CHECK(si.frame_length_expr == "length");
}

// ============================================================================
// Multiple entry-point messages
// ============================================================================

TEST_CASE("Multiple entry-point sessions discovered", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("multi_entry.bmdl.xml");

    REQUIRE(sessions.size() == 2);

    bool found_cmd = false, found_evt = false;
    for (const auto& si : sessions) {
        if (si.entry_point_name == "CommandFrame") {
            found_cmd = true;
            // Should have 2 leaves: Cmd1Body, Cmd2Body
            CHECK(si.leaf_types.size() == 2);
            // Sync should be 0xAA55
            REQUIRE(si.sync_pattern.size() == 2);
            CHECK(si.sync_pattern[0] == 0xAA);
            CHECK(si.sync_pattern[1] == 0x55);
        }
        if (si.entry_point_name == "EventFrame") {
            found_evt = true;
            // Should have 1 leaf: Evt1Body
            CHECK(si.leaf_types.size() == 1);
            CHECK(si.leaf_types[0].name == "Evt1Body");
            // Sync should be 0x55AA
            REQUIRE(si.sync_pattern.size() == 2);
            CHECK(si.sync_pattern[0] == 0x55);
            CHECK(si.sync_pattern[1] == 0xAA);
        }
    }
    CHECK(found_cmd);
    CHECK(found_evt);
}

TEST_CASE("Multiple entry-point leaf access paths correct", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("multi_entry.bmdl.xml");

    REQUIRE(sessions.size() == 2);

    for (const auto& si : sessions) {
        if (si.entry_point_name == "CommandFrame") {
            for (const auto& lt : si.leaf_types) {
                REQUIRE(lt.access_path.size() == 1);
                CHECK(lt.access_path[0].choice_field == "cmd");
                CHECK(lt.access_path[0].disc_field == "cmd-id");
                if (lt.name == "Cmd1Body") {
                    CHECK(lt.access_path[0].disc_value == "CMD_1");
                }
                if (lt.name == "Cmd2Body") {
                    CHECK(lt.access_path[0].disc_value == "CMD_2");
                }
            }
        }
    }
}

// ============================================================================
// Min frame header size computation
// ============================================================================

TEST_CASE("Min frame header size for choice protocol", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // Fields before choice: sync(uint16=2) + message-type(uint8=1) + length(uint16=2) = 5
    CHECK(si.min_frame_header_size == 5);
}

TEST_CASE("Min frame header size for session protocol", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("session_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // Fields before choice: sync(uint16=2) + seq(uint16=2) + msg-id(uint8=1) + length(uint16=2) = 7
    CHECK(si.min_frame_header_size == 7);
}

TEST_CASE("Min frame header size with inline struct", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("inline_struct.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // Inline Header: sync(uint16=2) + seq(uint16=2) + length(uint16=2) = 6
    // Plus: msg-type(uint8=1) = 1
    // Total before choice: 7
    CHECK(si.min_frame_header_size == 7);
}

// ============================================================================
// Entry-point field propagation to multiple leaves
// ============================================================================

TEST_CASE("Session protocol auto and constraint fields on all leaves", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("session_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // All leaves should get the entry-point "seq" auto field and "sync" constraint
    for (const auto& lt : si.leaf_types) {
        bool has_seq_auto = false;
        for (const auto& af : lt.auto_fields) {
            if (af == "seq") has_seq_auto = true;
        }
        CHECK(has_seq_auto);

        bool has_sync_constraint = false;
        for (const auto& [field, val] : lt.constraints) {
            if (field == "sync") has_sync_constraint = true;
        }
        CHECK(has_sync_constraint);
    }
}

// ============================================================================
// Arrays/choices session analysis
// ============================================================================

TEST_CASE("Arrays choices session with otherwise", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("arrays_choices.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "ChoiceMsg");

    // Nested leaves: TypeABody has sub-choice -> SubX, SubY
    // TypeBBody is a direct leaf
    // FallbackBody from otherwise
    // Total: SubX, SubY, TypeBBody, FallbackBody = 4
    CHECK(si.leaf_types.size() == 4);

    bool found_subx = false, found_suby = false, found_typeb = false, found_fallback = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "SubX") {
            found_subx = true;
            // Access path: body -> TypeABody, then sub-body -> SubX
            REQUIRE(lt.access_path.size() == 2);
            CHECK(lt.access_path[0].choice_field == "body");
            CHECK(lt.access_path[0].disc_value == "TYPE_A");
            CHECK(lt.access_path[1].choice_field == "sub-body");
            CHECK(lt.access_path[1].disc_value == "SUB_X");
        }
        if (lt.name == "SubY") found_suby = true;
        if (lt.name == "TypeBBody") {
            found_typeb = true;
            CHECK(lt.receive_only);
        }
        if (lt.name == "FallbackBody") {
            found_fallback = true;
            REQUIRE(lt.access_path.size() == 1);
            CHECK(lt.access_path[0].choice_field == "body");
            CHECK(lt.access_path[0].variant_type == "FallbackBody");
        }
    }
    CHECK(found_subx);
    CHECK(found_suby);
    CHECK(found_typeb);
    CHECK(found_fallback);
}

// ============================================================================
// Type ID uniqueness
// ============================================================================

TEST_CASE("Type IDs are unique across leaves", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("session_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    std::set<uint64_t> ids;
    for (const auto& lt : si.leaf_types) {
        CHECK(ids.insert(lt.type_id).second);  // insert returns false on duplicate
    }
}

// ============================================================================
// Otherwise with no discriminator
// ============================================================================

TEST_CASE("Otherwise leaf has no discriminator", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("otherwise_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    for (const auto& lt : si.leaf_types) {
        if (lt.name == "FallbackBody") {
            REQUIRE(lt.access_path.size() == 1);
            // Otherwise case should have empty disc_field and disc_value
            CHECK(lt.access_path[0].disc_field.empty());
            CHECK(lt.access_path[0].disc_value.empty());
        }
    }
}

// ============================================================================
// Sentry-link protocol
// ============================================================================

TEST_CASE("Sentry-link session analysis", "[session_analyzer]") {
    // Use the real sentry-link protocol
    std::string sentry_path = (fs::path(BGEN_TEST_FIXTURES_DIR) / "sentry_link.bmdl.xml").string();
    if (!fs::exists(sentry_path)) {
        SKIP("sentry-link protocol not found at " + sentry_path);
    }

    auto build_result = bgen::model::build_protocol(sentry_path);
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto sessions = bgen::analyzer::analyze_sessions(protocol, index);
    REQUIRE(sessions.size() == 1);

    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "Frame");

    // Should find 4 leaf types: HeartbeatBody, SensorBody, ConfigBody, AlertBody
    CHECK(si.leaf_types.size() == 4);

    // Sync pattern: SYNC = 0xAA55
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xAA);
    CHECK(si.sync_pattern[1] == 0x55);

    // Frame length field
    CHECK(si.frame_length_expr == "length");

    // Check direction filtering
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "ConfigBody") {
            CHECK(lt.send_only);
            CHECK_FALSE(lt.receive_only);
        } else {
            CHECK_FALSE(lt.send_only);
            CHECK(lt.receive_only);
        }
    }
}

TEST_CASE("Frame length signed resolved through TypeDef", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("signed_length.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "SignedFrame");

    // The "length" field uses type="int16" (base=int) — should be detected as signed
    CHECK(si.frame_length_expr == "length");
    CHECK(si.frame_length_signed == true);
    CHECK(si.frame_length_bits == 16);
}

TEST_CASE("Direction-qualified leaves have correct flags", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("direction_qualified.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.entry_point_name == "Frame");

    // Should discover UplinkPayload, DownlinkPayload, CommonPayload as leaves
    REQUIRE(si.leaf_types.size() == 3);

    bool found_uplink = false, found_downlink = false, found_common = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "UplinkPayload") {
            found_uplink = true;
            CHECK(lt.send_only);
            CHECK_FALSE(lt.receive_only);
        }
        if (lt.name == "DownlinkPayload") {
            found_downlink = true;
            CHECK_FALSE(lt.send_only);
            CHECK(lt.receive_only);
        }
        if (lt.name == "CommonPayload") {
            found_common = true;
            CHECK_FALSE(lt.send_only);
            CHECK_FALSE(lt.receive_only);
        }
    }
    CHECK(found_uplink);
    CHECK(found_downlink);
    CHECK(found_common);
}

// ============================================================================
// Frame-based session analysis (v2)
// ============================================================================

TEST_CASE("Frame-based session analysis basic", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("frame_basic.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    CHECK(si.entry_point_name == "SimpleFrame");
    CHECK(si.frame != nullptr);
    CHECK(si.id_field_name == "msg-type");
    CHECK(si.length_field_name == "length");
    CHECK_FALSE(si.payload_is_array);

    // Two leaves: Heartbeat and Status
    REQUIRE(si.leaf_types.size() == 2);

    bool found_hb = false, found_st = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "Heartbeat") {
            found_hb = true;
            CHECK(lt.type_id == bgen::analyzer::fnv1a_hash("Heartbeat"));
            CHECK(lt.access_path.empty()); // flat dispatch
            REQUIRE(lt.constraints.size() == 1);
            CHECK(lt.constraints[0].first == "msg-type");
            CHECK(lt.constraints[0].second == "1");
        }
        if (lt.name == "Status") {
            found_st = true;
            REQUIRE(lt.constraints.size() == 1);
            CHECK(lt.constraints[0].first == "msg-type");
            CHECK(lt.constraints[0].second == "2");
        }
    }
    CHECK(found_hb);
    CHECK(found_st);

    // Min header size: uint8 (msg-type) + uint16 (length) = 3 bytes
    CHECK(si.min_frame_header_size == 3);

    // Frame length field info
    CHECK(si.frame_length_bits == 16);
    CHECK(si.frame_length_bit_offset == 8);
}

TEST_CASE("Frame-based session with config fields", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("frame_config.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    REQUIRE(si.config_fields.size() == 1);
    CHECK(si.config_fields[0].key == "system-id");
    CHECK(si.config_fields[0].field_name == "system-id");
    CHECK(si.config_fields[0].type_ref == "uint8");
    CHECK(si.config_fields[0].bits == 8);
}

TEST_CASE("Frame-based session with footer", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("frame_footer.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    CHECK(si.entry_point_name == "FooterFrame");
    CHECK(si.frame != nullptr);
    CHECK(si.id_field_name == "msg-type");
    CHECK(si.length_field_name == "length");

    // Two leaf types: Data and Ack
    REQUIRE(si.leaf_types.size() == 2);
}

TEST_CASE("Frame-based session with direction-qualified messages", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("frame_direction.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    CHECK(si.entry_point_name == "DirFrame");

    // Three leaf types: CommandRequest (send), CommandResponse (recv), Heartbeat (both)
    REQUIRE(si.leaf_types.size() == 3);

    bool has_send = false, has_recv = false, has_both = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "CommandRequest") {
            CHECK(lt.send_only);
            CHECK_FALSE(lt.receive_only);
            has_send = true;
        } else if (lt.name == "CommandResponse") {
            CHECK_FALSE(lt.send_only);
            CHECK(lt.receive_only);
            has_recv = true;
        } else if (lt.name == "Heartbeat") {
            CHECK_FALSE(lt.send_only);
            CHECK_FALSE(lt.receive_only);
            has_both = true;
        }
    }
    CHECK(has_send);
    CHECK(has_recv);
    CHECK(has_both);
}

TEST_CASE("Frame-based session captures length offset", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("frame_length_offset.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    CHECK(si.length_field_name == "length");
    CHECK(si.frame_length_bits == 16);
    CHECK(si.frame_length_offset == -3);
}

TEST_CASE("Frame-based session with array payload", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("frame_array.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    CHECK(si.payload_is_array);
    CHECK(si.entry_point_name == "ArrayFrame");

    REQUIRE(si.leaf_types.size() == 1);
    CHECK(si.leaf_types[0].name == "Record");
}
