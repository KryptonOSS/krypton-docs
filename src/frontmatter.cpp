#include "frontmatter.h"

FrontMatter parse_frontmatter(const std::string& raw) {
    FrontMatter fm;
    std::string content = raw;

    std::string trimmed = utils::trim(content);
    if (trimmed.size() >= 3 && trimmed.substr(0, 3) == "+++") {
        auto first = content.find('\n');
        if (first == std::string::npos) {
            fm.body = content;
            return fm;
        }
        auto second = content.find("+++", first + 1);
        if (second == std::string::npos) {
            fm.body = content;
            return fm;
        }
        std::string toml_str = content.substr(first + 1, second - first - 1);
        fm.meta = toml::parse(toml_str);
        content = content.substr(second + 3);
        if (!content.empty() && content[0] == '\n')
            content = content.substr(1);
    }

    fm.body = content;

    fm.meta.for_each([&](const toml::key& key, const toml::node& val) {
        std::string k(key.str());
        if (auto s = val.as_string()) {
            fm.flat[k] = std::string(s->get());
        } else if (auto b = val.as_boolean()) {
            fm.flat[k] = b->get() ? "true" : "false";
        } else if (auto i = val.as_integer()) {
            fm.flat[k] = std::to_string(i->get());
        } else if (auto f = val.as_floating_point()) {
            fm.flat[k] = std::to_string(f->get());
        } else if (auto arr = val.as_array()) {
            std::string val_str;
            for (auto& item : *arr) {
                if (!val_str.empty()) val_str += ", ";
                if (auto s = item.as_string()) val_str += std::string(s->get());
            }
            fm.flat[k] = val_str;
        }
    });

    return fm;
}
