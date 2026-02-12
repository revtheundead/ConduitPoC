// SPDX-License-Identifier: MIT
// Bgen - XML Parser for BMDL files

#pragma once

#include "../model/ast.hpp"
#include "expression_parser.hpp"
#include <expected>
#include <string>
#include <vector>

namespace bgen::parser {

struct XmlParseError {
    std::string message;
    model::SourceLoc loc;

    std::string format() const {
        return loc.to_string() + ": error: " + message;
    }
};

struct XmlParseWarning {
    std::string message;
    model::SourceLoc loc;

    std::string format() const {
        return loc.to_string() + ": warning: " + message;
    }
};

using XmlParseResult = std::expected<model::BmdlFile, std::vector<XmlParseError>>;

// Parse a single BMDL XML file into a BmdlFile AST.
// Does NOT resolve imports or type references.
XmlParseResult parse_bmdl_file(const std::string& file_path);

} // namespace bgen::parser
