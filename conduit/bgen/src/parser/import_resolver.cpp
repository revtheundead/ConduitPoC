// SPDX-License-Identifier: MIT
// Bgen - Import Resolver Implementation

#include "import_resolver.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace bgen::parser {

namespace fs = std::filesystem;

namespace {

class ImportResolverImpl {
public:
    void resolve(const std::string& root_path) {
        auto abs_path = normalize_path(root_path);
        resolve_file(abs_path, 0);
    }

    bool has_errors() const { return !errors_.empty(); }
    const std::vector<ResolveError>& errors() const { return errors_; }
    std::vector<model::BmdlFile>& files() { return files_; }

private:
    static constexpr int MAX_IMPORT_DEPTH = 64;

    void resolve_file(const std::string& abs_path, int depth) {
        if (depth > MAX_IMPORT_DEPTH) {
            errors_.push_back(ResolveError{
                "import depth exceeds " + std::to_string(MAX_IMPORT_DEPTH) +
                " — possible circular import chain",
                model::SourceLoc{abs_path, 0}
            });
            return;
        }

        // Dedup by absolute path (case-insensitive on Windows)
        if (seen_.count(dedup_key(abs_path))) return;
        seen_.insert(dedup_key(abs_path));

        // Parse the file
        auto result = parse_bmdl_file(abs_path);
        if (!result) {
            for (const auto& e : result.error()) {
                errors_.push_back(ResolveError{e.message, e.loc});
            }
            return;
        }

        auto& bmdl = *result;
        auto dir = fs::path(abs_path).parent_path();

        // Recursively resolve imports
        for (const auto& imp : bmdl.imports) {
            auto imp_path = normalize_path((dir / imp.href).string());
            try {
                if (!fs::exists(imp_path)) {
                    errors_.push_back(ResolveError{
                        "import file not found: " + imp.href,
                        imp.loc
                    });
                    continue;
                }
            } catch (const std::exception& e) {
                errors_.push_back(ResolveError{
                    "cannot access import '" + imp.href + "': " + e.what(),
                    imp.loc
                });
                continue;
            }
            resolve_file(imp_path, depth + 1);
        }

        files_.push_back(std::move(bmdl));
    }

    static std::string dedup_key(const std::string& path) {
#ifdef _WIN32
        // Case-insensitive filesystem: lowercase for dedup only
        std::string key = path;
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
#else
        return path;
#endif
    }

    std::string normalize_path(const std::string& path) {
        try {
            auto p = fs::absolute(fs::path(path)).lexically_normal();
            return p.string();
        } catch (const std::exception& e) {
            errors_.push_back(ResolveError{
                "path normalization failed for: " + path + " (" + e.what() + ")", {}});
            return path;
        } catch (...) {
            errors_.push_back(ResolveError{"path normalization failed for: " + path, {}});
            return path;
        }
    }

    std::set<std::string> seen_;
    std::vector<model::BmdlFile> files_;
    std::vector<ResolveError> errors_;
};

} // anonymous namespace

ResolveResult resolve_imports(const std::string& root_path) {
    ImportResolverImpl resolver;
    resolver.resolve(root_path);

    if (resolver.has_errors()) {
        return std::unexpected(resolver.errors());
    }

    return std::move(resolver.files());
}

} // namespace bgen::parser
