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

// ============================================================================
// Frame-based session discovery for migrated fixtures
// ============================================================================

TEST_CASE("Choice protocol is frame-based", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.is_frame_based);
    CHECK(si.frame != nullptr);
    CHECK(si.session_name == "Frame");
    CHECK(si.id_field_name == "message-type");
    CHECK(si.length_field_name == "length");
    CHECK_FALSE(si.payload_is_array);

    // Two leaves: AlphaBody and BetaBody
    REQUIRE(si.leaf_types.size() == 2);

    bool found_alpha = false, found_beta = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "AlphaBody") {
            found_alpha = true;
            CHECK_FALSE(lt.send_only);
            CHECK_FALSE(lt.receive_only);
            // v2 flat dispatch: constraint on id field
            REQUIRE(lt.constraints.size() == 1);
            CHECK(lt.constraints[0].first == "message-type");
            CHECK(lt.constraints[0].second == "1");
        }
        if (lt.name == "BetaBody") {
            found_beta = true;
            CHECK_FALSE(lt.send_only);
            CHECK(lt.receive_only);
            REQUIRE(lt.constraints.size() == 1);
            CHECK(lt.constraints[0].first == "message-type");
            CHECK(lt.constraints[0].second == "2");
        }
    }
    CHECK(found_alpha);
    CHECK(found_beta);
}

TEST_CASE("Session protocol is frame-based", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("session_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.is_frame_based);
    CHECK(si.frame != nullptr);
    CHECK(si.session_name == "Packet");
    CHECK(si.id_field_name == "msg-id");
    CHECK(si.length_field_name == "length");

    // Leaves: PingBody, DataBody, AckBody
    REQUIRE(si.leaf_types.size() == 3);

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

TEST_CASE("Auto-sequence is frame-based", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("auto_sequence.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.is_frame_based);
    CHECK(si.frame != nullptr);
    CHECK(si.session_name == "Frame");
    CHECK(si.id_field_name == "tag");

    // Single leaf: BodyA
    REQUIRE(si.leaf_types.size() == 1);
    CHECK(si.leaf_types[0].name == "BodyA");
    REQUIRE(si.leaf_types[0].constraints.size() == 1);
    CHECK(si.leaf_types[0].constraints[0].first == "tag");
    CHECK(si.leaf_types[0].constraints[0].second == "1");

    // auto="increment" fields detected via auto_expr (not legacy auto_attr)
    REQUIRE(si.leaf_types[0].auto_fields.size() == 1);
    CHECK(si.leaf_types[0].auto_fields[0] == "seq");
    REQUIRE(si.leaf_types[0].auto_field_bits.size() == 1);
    CHECK(si.leaf_types[0].auto_field_bits[0] == 16);
}

TEST_CASE("Inline struct is frame-based", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("inline_struct.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.is_frame_based);
    CHECK(si.frame != nullptr);
    CHECK(si.session_name == "Frame");
    CHECK(si.id_field_name == "msg-type");
    CHECK(si.length_field_name == "length");

    // Two leaves: BodyX and BodyY
    REQUIRE(si.leaf_types.size() == 2);
    bool found_x = false, found_y = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "BodyX") {
            found_x = true;
            REQUIRE(lt.constraints.size() == 1);
            CHECK(lt.constraints[0].second == "1");
        }
        if (lt.name == "BodyY") {
            found_y = true;
            REQUIRE(lt.constraints.size() == 1);
            CHECK(lt.constraints[0].second == "2");
        }
    }
    CHECK(found_x);
    CHECK(found_y);
}

TEST_CASE("Send-only leaf is frame-based", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("send_only_leaf.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.is_frame_based);
    CHECK(si.frame != nullptr);

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

TEST_CASE("Direction-qualified leaves have correct flags", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("direction_qualified.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.is_frame_based);
    CHECK(si.frame != nullptr);
    CHECK(si.session_name == "Frame");

    // UplinkPayload (send), DownlinkPayload (recv), CommonPayload (both)
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
// Sync pattern extraction
// ============================================================================

TEST_CASE("Sync pattern extraction from choice protocol frame", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // SYNC = 0xBEEF -> 2 bytes: 0xBE, 0xEF
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xBE);
    CHECK(si.sync_pattern[1] == 0xEF);
}

TEST_CASE("Sync pattern extraction from session protocol frame", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("session_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // SYNC = 0xDEAD -> [0xDE, 0xAD]
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xDE);
    CHECK(si.sync_pattern[1] == 0xAD);
}

TEST_CASE("Sync pattern extraction from auto-sequence frame", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("auto_sequence.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // SYNC = 0xBEEF -> [0xBE, 0xEF]
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xBE);
    CHECK(si.sync_pattern[1] == 0xEF);
}

TEST_CASE("Sync pattern extraction from inline struct frame", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("inline_struct.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // SYNC = 0xCAFE -> [0xCA, 0xFE]
    REQUIRE(si.sync_pattern.size() == 2);
    CHECK(si.sync_pattern[0] == 0xCA);
    CHECK(si.sync_pattern[1] == 0xFE);
}

// ============================================================================
// Frame length detection
// ============================================================================

TEST_CASE("Frame length detection for choice protocol", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.frame_length_expr == "length");
    CHECK(si.frame_length_bits == 16);
    // sync(16) + message-type(8) = 24 bits before length field
    CHECK(si.frame_length_bit_offset == 24);
}

TEST_CASE("Frame length detection for session protocol", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("session_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.frame_length_expr == "length");
    CHECK(si.frame_length_bits == 16);
    // sync(16) + seq(16) + msg-id(8) = 40 bits before length field
    CHECK(si.frame_length_bit_offset == 40);
}

TEST_CASE("Frame length detection for inline struct frame", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("inline_struct.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.frame_length_expr == "length");
    CHECK(si.frame_length_bits == 16);
    // sync(16) + seq(16) = 32 bits before length field
    CHECK(si.frame_length_bit_offset == 32);
}

TEST_CASE("Auto-sequence frame has no length field", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("auto_sequence.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.frame_length_expr.empty());
    CHECK(si.length_field_name.empty());
}

TEST_CASE("Frame length signed resolved through TypeDef", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("signed_length.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    CHECK(si.is_frame_based);
    CHECK(si.session_name == "SignedFrame");

    // The "length" field uses type="int16" (base=int) - should be detected as signed
    CHECK(si.frame_length_expr == "length");
    CHECK(si.frame_length_signed == true);
    CHECK(si.frame_length_bits == 16);
}

// ============================================================================
// Min frame header size computation
// ============================================================================

TEST_CASE("Min frame header size for choice protocol", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // Fields before payload: sync(uint16=2) + message-type(uint8=1) + length(uint16=2) = 5
    CHECK(si.min_frame_header_size == 5);
}

TEST_CASE("Min frame header size for session protocol", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("session_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // sync(uint16=2) + seq(uint16=2) + msg-id(uint8=1) + length(uint16=2) = 7
    CHECK(si.min_frame_header_size == 7);
}

TEST_CASE("Min frame header size for inline struct frame", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("inline_struct.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // sync(uint16=2) + seq(uint16=2) + length(uint16=2) + msg-type(uint8=1) = 7
    CHECK(si.min_frame_header_size == 7);
}

// ============================================================================
// Leaf annotations
// ============================================================================

TEST_CASE("Leaf annotations propagation", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("choice_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    for (const auto& lt : si.leaf_types) {
        if (lt.name == "AlphaBody") {
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

// ============================================================================
// Type ID generation
// ============================================================================

TEST_CASE("Type ID generation is deterministic", "[session_analyzer][frame]") {
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

TEST_CASE("Type IDs are unique across leaves", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("session_protocol.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    std::set<uint64_t> ids;
    for (const auto& lt : si.leaf_types) {
        CHECK(ids.insert(lt.type_id).second);  // insert returns false on duplicate
    }
}

// ============================================================================
// Sentry-link protocol (frame-based)
// ============================================================================

TEST_CASE("Sentry-link session analysis", "[session_analyzer][frame]") {
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
    CHECK(si.is_frame_based);
    CHECK(si.frame != nullptr);
    CHECK(si.session_name == "Frame");
    CHECK(si.id_field_name == "msg-type");
    CHECK(si.length_field_name == "length");

    // 4 leaf types: HeartbeatBody, SensorBody, ConfigBody, AlertBody
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

// ============================================================================
// Non-session fixtures return empty sessions
// ============================================================================

TEST_CASE("Non-session fixture arrays_choices returns no sessions", "[session_analyzer]") {
    auto [protocol, sessions] = analyze_fixture("arrays_choices.bmdl.xml");

    // arrays_choices has no <frame> element, so no sessions are produced
    CHECK(sessions.empty());
}

// ============================================================================
// Existing v2 frame-based tests (frame_basic, frame_config, etc.)
// ============================================================================

TEST_CASE("Frame-based session analysis basic", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("frame_basic.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    CHECK(si.session_name == "SimpleFrame");
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

TEST_CASE("Message-level config fields collected per leaf type", "[session_analyzer][frame][config]") {
    auto [protocol, sessions] = analyze_fixture("msg_config.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    // Frame-level config: system-id
    REQUIRE(si.config_fields.size() == 1);
    CHECK(si.config_fields[0].key == "system-id");

    // Three leaf types: Telemetry, Command, Heartbeat
    REQUIRE(si.leaf_types.size() == 3);

    for (const auto& lt : si.leaf_types) {
        if (lt.name == "Telemetry") {
            REQUIRE(lt.config_fields.size() == 1);
            CHECK(lt.config_fields[0].key == "station-id");
            CHECK(lt.config_fields[0].field_name == "station-id");
            CHECK(lt.config_fields[0].bits == 8);
        }
        if (lt.name == "Command") {
            REQUIRE(lt.config_fields.size() == 1);
            CHECK(lt.config_fields[0].key == "operator-id");
            CHECK(lt.config_fields[0].field_name == "operator-id");
            CHECK(lt.config_fields[0].bits == 16);
        }
        if (lt.name == "Heartbeat") {
            CHECK(lt.config_fields.empty());
        }
    }
}

TEST_CASE("Inline struct config fields collected per leaf type", "[session_analyzer][frame][config]") {
    auto [protocol, sessions] = analyze_fixture("msg_config_inline.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    // No frame-level config fields
    CHECK(si.config_fields.empty());

    REQUIRE(si.leaf_types.size() == 2);

    for (const auto& lt : si.leaf_types) {
        if (lt.name == "Report") {
            // Config fields from inlined SourceId struct: sac and sic
            REQUIRE(lt.config_fields.size() == 2);
            CHECK(lt.config_fields[0].key == "sac");
            CHECK(lt.config_fields[0].field_name == "sac");
            CHECK(lt.config_fields[0].bits == 8);
            CHECK(lt.config_fields[1].key == "sic");
            CHECK(lt.config_fields[1].field_name == "sic");
            CHECK(lt.config_fields[1].bits == 8);
        }
        if (lt.name == "Status") {
            CHECK(lt.config_fields.empty());
        }
    }
}

TEST_CASE("Frame-based session with footer", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("frame_footer.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    CHECK(si.session_name == "FooterFrame");
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
    CHECK(si.session_name == "DirFrame");

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
    CHECK(si.frame_length_modifier.op == bgen::model::ArithOp::Sub);
    CHECK(si.frame_length_modifier.literal == 3);
}

TEST_CASE("Frame-based session with array payload", "[session_analyzer][frame]") {
    auto [protocol, sessions] = analyze_fixture("frame_array.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    CHECK(si.payload_is_array);
    CHECK(si.session_name == "ArrayFrame");

    REQUIRE(si.leaf_types.size() == 1);
    CHECK(si.leaf_types[0].name == "Record");
}

// ============================================================================
// Payload length-from expression
// ============================================================================

TEST_CASE("Frame-based session captures payload length-from", "[session_analyzer][frame][payload_length_from]") {
    auto [protocol, sessions] = analyze_fixture("frame_payload_length_from.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    CHECK(si.is_frame_based);
    CHECK(si.session_name == "ExprFrame");
    CHECK_FALSE(si.payload_is_array);

    // payload_length_from should be set (from <payload length-from="body-size"/>)
    REQUIRE(si.payload_length_from != nullptr);
    CHECK(si.payload_length_from->op == bgen::model::ExprOp::FieldRef);
    CHECK(si.payload_length_from->name == "body-size");

    // No auto="length" field, so length_field_name should be empty
    CHECK(si.length_field_name.empty());

    // Two leaf types: Ping and Data
    REQUIRE(si.leaf_types.size() == 2);
    bool found_ping = false, found_data = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.name == "Ping") found_ping = true;
        if (lt.name == "Data") found_data = true;
    }
    CHECK(found_ping);
    CHECK(found_data);
}

TEST_CASE("Frame without payload length-from has null pointer", "[session_analyzer][frame][payload_length_from]") {
    auto [protocol, sessions] = analyze_fixture("frame_basic.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];

    // frame_basic has no <payload length-from="..."/>, so field should be null
    CHECK(si.payload_length_from == nullptr);
}

// ============================================================================
// Annotation scope: only message annotations propagate to leaf types
// ============================================================================

TEST_CASE("Only message annotations propagate to leaf types", "[session_analyzer][frame][annotations]") {
    auto [protocol, sessions] = analyze_fixture("annotation_scope.bmdl.xml");

    REQUIRE(sessions.size() == 1);
    const auto& si = sessions[0];
    REQUIRE(si.leaf_types.size() == 2);

    for (const auto& lt : si.leaf_types) {
        if (lt.name == "MsgWithAnnotations") {
            // Message-level annotation should propagate
            REQUIRE(lt.annotations.size() == 1);
            CHECK(lt.annotations[0].first == "msg-ann");
            CHECK(lt.annotations[0].second == "propagated");
            // Type-level annotation on annotated-type should NOT appear
            bool found_type_ann = false;
            for (const auto& ann : lt.annotations) {
                if (ann.first == "type-level") found_type_ann = true;
            }
            CHECK_FALSE(found_type_ann);
        }
        if (lt.name == "MsgNoAnnotations") {
            // No annotations on this message
            CHECK(lt.annotations.empty());
        }
    }
}
