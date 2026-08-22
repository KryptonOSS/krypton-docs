#pragma once
#include "frontmatter.h"
#include <map>

struct ContentItem {
    std::string filename;
    std::string collection;
    std::string permalink;
    std::string layout;
    FrontMatter fm;
    std::string html_body;
    std::map<std::string, std::string> meta;
};

struct Collection {
    std::string name;
    std::vector<ContentItem> items;
};

class CollectionsManager {
public:
    void scan_directory(const fs::path& dir, const std::string& collection_name);
    void scan_item(const fs::path& file, const std::string& collection_name);
    Collection& get(const std::string& name);
    std::vector<ContentItem>& items(const std::string& name);
    std::map<std::string, Collection>& all();

    std::vector<ContentItem> sorted_by_date_desc(const std::string& name) const;
    std::vector<ContentItem> sorted_by_date_asc(const std::string& name) const;

private:
    std::map<std::string, Collection> collections_;
};
