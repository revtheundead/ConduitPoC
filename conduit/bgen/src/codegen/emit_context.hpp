// SPDX-License-Identifier: MIT
// Bgen - Emit Context (shared state during code emission)

#pragma once

#include <sstream>
#include <string>

namespace bgen::codegen {

class EmitContext {
public:
    void indent() { indent_level_++; }
    void dedent() { if (indent_level_ > 0) indent_level_--; }

    void line(const std::string& text = "") {
        if (text.empty()) {
            out_ << "\n";
        } else {
            out_ << std::string(static_cast<size_t>(indent_level_) * 4, ' ') << text << "\n";
        }
    }

    void raw(const std::string& text) {
        out_ << text;
    }

    // Emit a (possibly multi-line) C++ line comment.
    // Each line of `text` is prefixed with "// ".
    void comment(const std::string& text) {
        std::string::size_type start = 0;
        while (start < text.size()) {
            auto nl = text.find('\n', start);
            auto segment = (nl == std::string::npos)
                ? text.substr(start)
                : text.substr(start, nl - start);
            // Trim trailing whitespace from segment
            while (!segment.empty() && (segment.back() == ' ' || segment.back() == '\r'))
                segment.pop_back();
            if (segment.empty())
                line("//");
            else
                line("// " + segment);
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }

    std::string str() const { return out_.str(); }
    void clear() { out_.str(""); out_.clear(); indent_level_ = 0; }

    int indent_level() const { return indent_level_; }

private:
    std::ostringstream out_;
    int indent_level_ = 0;
};

} // namespace bgen::codegen
