#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static std::string sanitize(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    return out;
}

static int embedAsset(const std::string& input, const std::string& outDir,
                      const std::string& assetName, const std::string& category) {
    const std::string safe = sanitize(assetName);
    const std::string prefix = "embedded_" + category + "_" + safe;

    std::string ext;
    const auto slash = input.find_last_of("/\\");
    const auto dot = input.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || slash < dot))
        ext = input.substr(dot);

    std::ifstream in(input, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open input '%s'\n", input.c_str());
        return 1;
    }
    const std::vector<unsigned char> data(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const std::string base = outDir + "/" + category + "_" + safe;

    std::ofstream h(base + ".h");
    if (!h) {
        std::fprintf(stderr, "cannot write '%s.h'\n", base.c_str());
        return 1;
    }
    h << "#pragma once\n"
      << "#include <cstddef>\n"
      << "extern const unsigned char " << prefix << "_data[];\n"
      << "extern const size_t " << prefix << "_size;\n"
      << "extern const char " << prefix << "_ext[];\n";

    std::ofstream cpp(base + ".cpp");
    if (!cpp) {
        std::fprintf(stderr, "cannot write '%s.cpp'\n", base.c_str());
        return 1;
    }
    cpp << "#include \"" << category << "_" << safe << ".h\"\n"
        << "alignas(16) const unsigned char " << prefix << "_data[] = {\n";
    char buf[8];
    for (size_t i = 0; i < data.size(); ++i) {
        if (i % 32 == 0) cpp << "     ";
        std::snprintf(buf, sizeof buf, "0x%02x", data[i]);
        cpp << buf;
        if (i + 1 < data.size()) cpp << ',';
        if (i % 32 == 31) cpp << '\n';
    }
    cpp << "\n};\n"
        << "const size_t " << prefix << "_size = " << data.size() << ";\n"
        << "const char " << prefix << "_ext[] = \"" << ext << "\";\n";
    return 0;
}

static int writeRegistry(const std::string& category, const std::string& outDir,
                         const std::vector<std::string>& names) {
    std::string includes;
    std::string entries;
    for (const auto& name : names) {
        const std::string safe = sanitize(name);
        const std::string prefix = "embedded_" + category + "_" + safe;
        if (!includes.empty()) {
            includes += "\n";
            entries += "\n";
        }
        includes += "#include \"" + category + "_" + safe + ".h\"";
        entries += "       {\"" + name + "\", {" + prefix + "_data, " + prefix +
                   "_size, " + prefix + "_ext}},";
    }

    std::ofstream out(outDir + "/" + category + "_registry.h");
    if (!out) {
        std::fprintf(stderr, "cannot write '%s/%s_registry.h'\n",
                     outDir.c_str(), category.c_str());
        return 1;
    }
    out << "#pragma once\n"
        << "#include <cstddef>\n"
        << "#include <string>\n"
        << "#include <unordered_map>\n"
        << "#include \"engine/EmbeddedAssets.h\"\n"
        << includes << "\n\n"
        << "inline const std::unordered_map<std::string, engine::EmbeddedAsset>& "
           "getEmbedded_"
        << category << "() {\n"
        << "    static const std::unordered_map<std::string, engine::EmbeddedAsset> "
           "assets = {\n"
        << entries << "\n"
        << "    };\n"
        << "    return assets;\n"
        << "}\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--registry") {
        // --registry CATEGORY OUTPUT_DIR [NAMES...]
        if (argc < 4) {
            std::fprintf(stderr,
                         "Usage: %s --registry CATEGORY OUTPUT_DIR [NAMES...]\n",
                         argv[0]);
            return 1;
        }
        const std::vector<std::string> names(argv + 4, argv + argc);
        return writeRegistry(argv[2], argv[3], names);
    }

    if (argc != 5) {
        std::fprintf(stderr, "Usage: %s INPUT_FILE OUTPUT_DIR ASSET_NAME CATEGORY\n",
                     argv[0]);
        return 1;
    }
    return embedAsset(argv[1], argv[2], argv[3], argv[4]);
}