#include "collections.h"
#include "markdown.h"

static ContentItem make_item(const fs::path& file, const std::string& collection_name) {
    std::string raw = utils::read_file(file);
    ContentItem item;
    item.filename = file.stem().string();
    item.collection = collection_name;
    item.fm = parse_frontmatter(raw);
    item.html_body = markdown_to_html(item.fm.body);

    if (item.fm.has("layout"))
        item.layout = item.fm.get_str("layout", "");
    if (item.fm.has("permalink"))
        item.permalink = item.fm.get_str("permalink", "");

    if (collection_name == "posts") {
        if (item.permalink.empty()) {
            std::string date_str = item.fm.flat.count("date") ? item.fm.flat["date"] : "";
            if (date_str.size() >= 10) {
                item.permalink = "/posts/" + date_str.substr(0, 4) + "/" +
                                 date_str.substr(5, 2) + "/" + item.filename + "/";
            } else {
                item.permalink = "/posts/" + item.filename + "/";
            }
        }
    } else if (item.permalink.empty()) {
        item.permalink = "/" + collection_name + "/" + item.filename + "/";
    }

    for (auto& [k, v] : item.fm.flat) {
        item.meta[k] = v;
    }
    item.meta["url"] = item.permalink;
    item.meta["id"] = item.filename;

    return item;
}

void CollectionsManager::scan_item(const fs::path& file, const std::string& collection_name) {
    Collection& col = collections_[collection_name];
    col.name = collection_name;
    col.items.push_back(std::move(make_item(file, collection_name)));
}

void CollectionsManager::scan_directory(const fs::path& dir, const std::string& collection_name) {
    if (!fs::exists(dir)) return;
    Collection& col = collections_[collection_name];
    col.name = collection_name;

    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext != ".md" && ext != ".html") continue;

        col.items.push_back(std::move(make_item(entry.path(), collection_name)));
    }
}

Collection& CollectionsManager::get(const std::string& name) {
    return collections_[name];
}

std::vector<ContentItem>& CollectionsManager::items(const std::string& name) {
    return collections_[name].items;
}

std::map<std::string, Collection>& CollectionsManager::all() {
    return collections_;
}

static bool date_desc(const ContentItem& a, const ContentItem& b) {
    auto ita = a.fm.flat.find("date");
    auto itb = b.fm.flat.find("date");
    std::string da = ita != a.fm.flat.end() ? ita->second : "";
    std::string db = itb != b.fm.flat.end() ? itb->second : "";
    return da > db;
}

static bool date_asc(const ContentItem& a, const ContentItem& b) {
    auto ita = a.fm.flat.find("date");
    auto itb = b.fm.flat.find("date");
    std::string da = ita != a.fm.flat.end() ? ita->second : "";
    std::string db = itb != b.fm.flat.end() ? itb->second : "";
    return da < db;
}

std::vector<ContentItem> CollectionsManager::sorted_by_date_desc(const std::string& name) const {
    auto it = collections_.find(name);
    if (it == collections_.end()) return {};
    auto items = it->second.items;
    std::sort(items.begin(), items.end(), date_desc);
    return items;
}

std::vector<ContentItem> CollectionsManager::sorted_by_date_asc(const std::string& name) const {
    auto it = collections_.find(name);
    if (it == collections_.end()) return {};
    auto items = it->second.items;
    std::sort(items.begin(), items.end(), date_asc);
    return items;
}
