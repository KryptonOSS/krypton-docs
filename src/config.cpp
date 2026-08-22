#include "config.h"

static Dict toml_table_to_dict(const toml::table& tbl) {
    Dict d;
    tbl.for_each([&](const toml::key& key, const toml::node& val) {
        std::string k(key.str());
        if (auto s = val.as_string()) {
            d[k] = std::string(s->get());
        } else if (auto b = val.as_boolean()) {
            d[k] = b->get() ? "true" : "false";
        } else if (auto i = val.as_integer()) {
            d[k] = std::to_string(i->get());
        } else if (auto f = val.as_floating_point()) {
            d[k] = std::to_string(f->get());
        }
    });
    return d;
}

SiteConfig load_config(const fs::path& config_path) {
    SiteConfig cfg;
    toml::table tbl = toml::parse_file(config_path.string());

    cfg.title = tbl["title"].value_or(std::string("Your Name / Site Title"));
    cfg.name = tbl["name"].value_or(std::string("Your Name"));
    cfg.description = tbl["description"].value_or(std::string(""));
    cfg.url = tbl["url"].value_or(std::string(""));
    cfg.baseurl = tbl["baseurl"].value_or(std::string(""));
    cfg.locale = tbl["locale"].value_or(std::string("en-US"));

    if (auto social = tbl["social"].as_table()) {
        cfg.social = toml_table_to_dict(*social);
    }

    return cfg;
}
