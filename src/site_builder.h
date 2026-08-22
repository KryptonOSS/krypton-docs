#pragma once
#include "config.h"
#include "collections.h"
#include "template_engine.h"
#include "markdown.h"

struct SectionInfo {
    std::string name;
    std::string permalink;
    std::string title;
    int order = 999;
};

class SiteBuilder {
public:
    SiteBuilder(const fs::path& content_dir, const fs::path& output_dir,
                const fs::path& template_dir, const std::string* baseurl_override = nullptr);

    void build();

private:
    fs::path content_dir_;
    fs::path output_dir_;
    fs::path template_dir_;
    const std::string* baseurl_override_;
    SiteConfig config_;
    CollectionsManager collections_;
    TemplateEngine engine_;
    std::vector<SectionInfo> sections_;

    void build_config();
    void build_content();
    void build_pages();
    void build_section(const SectionInfo& sec);
    void build_collection_pages(const std::string& name, const std::string& layout);
    void build_sitemap();
    void build_feed();
    void copy_static_files();

    void discover_sections();
    void build_navigation();

    Context make_context();
    Context make_page_context(const ContentItem& item);
    std::string render_item_list(const std::string& collection_name,
                                  const std::string& include_template);
};
