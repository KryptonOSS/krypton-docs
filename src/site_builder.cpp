#include "site_builder.h"
#include "markdown.h"
#include <algorithm>
#include <iostream>

static std::string flat_get(const Dict& flat, const std::string& key, const std::string& def = "") {
    auto it = flat.find(key);
    return it != flat.end() ? it->second : def;
}

SiteBuilder::SiteBuilder(const fs::path& content_dir, const fs::path& output_dir,
                          const fs::path& template_dir, const std::string* baseurl_override)
    : content_dir_(content_dir), output_dir_(output_dir), template_dir_(template_dir),
      baseurl_override_(baseurl_override), engine_(template_dir) {}

void SiteBuilder::build() {
    std::cout << "Loading configuration..." << std::endl;
    build_config();

    std::cout << "Discovering sections..." << std::endl;
    discover_sections();

    std::cout << "Building navigation..." << std::endl;
    build_navigation();

    std::cout << "Scanning content..." << std::endl;
    build_content();

    std::cout << "Building pages..." << std::endl;
    build_pages();

    std::cout << "Copying static files..." << std::endl;
    copy_static_files();

    std::cout << "Generating sitemap..." << std::endl;
    build_sitemap();

    std::cout << "Generating feed..." << std::endl;
    build_feed();

    std::cout << "Build complete!" << std::endl;
}

void SiteBuilder::build_config() {
    fs::path config_path = content_dir_ / "config.toml";
    config_ = load_config(config_path);
    if (baseurl_override_) {
        config_.baseurl = *baseurl_override_;
    }
}

void SiteBuilder::discover_sections() {
    if (!fs::exists(content_dir_)) return;

    for (auto& entry : fs::directory_iterator(content_dir_)) {
        if (!entry.is_directory()) continue;
        std::string dirname = entry.path().filename().string();
        if (dirname[0] == '.' || dirname == "files" || dirname == "images") continue;

        fs::path index_md = entry.path() / "index.md";
        if (!fs::exists(index_md)) continue;

        std::string raw = utils::read_file(index_md);
        FrontMatter fm = parse_frontmatter(raw);

        SectionInfo sec;
        sec.name = dirname;
        sec.permalink = fm.get_str("permalink", "/" + dirname + "/");
        sec.title = fm.get_str("title", dirname);
        sec.order = static_cast<int>(fm.flat.count("nav_order") ? std::stoi(fm.flat.at("nav_order")) : 999);

        sections_.push_back(sec);
    }

    std::sort(sections_.begin(), sections_.end(), [](const SectionInfo& a, const SectionInfo& b) {
        return a.order < b.order;
    });
}

void SiteBuilder::build_navigation() {
    for (auto& sec : sections_) {
        Dict nav_item;
        nav_item["title"] = sec.title;
        nav_item["url"] = sec.permalink;
        config_.navigation.push_back(nav_item);
    }
}

void SiteBuilder::build_content() {
    for (auto& sec : sections_) {
        fs::path sec_dir = content_dir_ / sec.name;
        for (auto& entry : fs::directory_iterator(sec_dir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().filename() == "index.md") continue;
            std::string ext = entry.path().extension().string();
            if (ext != ".md" && ext != ".html") continue;

            collections_.scan_item(entry.path(), sec.name);
        }
    }
}

void SiteBuilder::build_pages() {
    Context ctx = make_context();

    for (auto& sec : sections_) {
        build_section(sec);
    }

    if (fs::exists(content_dir_)) {
        for (auto& entry : fs::directory_iterator(content_dir_)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (ext != ".md") continue;
            std::string stem = entry.path().stem().string();
            if (stem == "config") continue;

            std::string raw = utils::read_file(entry.path());
            FrontMatter fm = parse_frontmatter(raw);

            std::string page_layout = fm.get_str("layout", "single");
            std::string page_title = fm.get_str("title", "");
            std::string page_permalink = fm.get_str("permalink", "");
            if (page_permalink.empty()) {
                page_permalink = "/" + stem + "/";
            }

            std::string html_body = markdown_to_html(fm.body);

            Context page_ctx = ctx;
            page_ctx.set("page.title", page_title);
            page_ctx.set("page.url", page_permalink);
            page_ctx.set("page.layout", page_layout);
            page_ctx.set("page.content", html_body);
            page_ctx.set("page.toc", fm.get_str("toc", ""));

            if (fm.has("redirect_from")) {
                auto redirs = fm.get_array("redirect_from");
                for (auto& redir_url : redirs) {
                    if (!redir_url.empty()) {
                        std::string redir_content = "<meta http-equiv=\"refresh\" content=\"0;url=" + page_permalink + "\">";
                        fs::path redir_path = output_dir_ / redir_url.substr(1);
                        if (redir_url.back() != '/') redir_path = redir_path / "index.html";
                        else redir_path = redir_path / "index.html";
                        utils::write_file(redir_path, redir_content);
                    }
                }
            }

            std::string page_html;
            if (page_layout == "splash") {
                page_html = engine_.render_file_with_layout("layouts/splash.html", "default", page_ctx);
            } else {
                page_ctx.set("content", html_body);
                page_html = engine_.render_file_with_layout("layouts/single.html", "default", page_ctx);
            }

            fs::path out_path;
            if (page_permalink.size() > 5 && page_permalink.substr(page_permalink.size() - 5) == ".html") {
                out_path = output_dir_ / page_permalink.substr(1);
            } else {
                out_path = output_dir_ / page_permalink.substr(1) / "index.html";
            }
            utils::write_file(out_path, page_html);
        }
    }
}

void SiteBuilder::build_section(const SectionInfo& sec) {
    Context ctx = make_context();
    fs::path sec_dir = content_dir_ / sec.name;
    fs::path index_md = sec_dir / "index.md";

    std::string raw = utils::read_file(index_md);
    FrontMatter fm = parse_frontmatter(raw);
    std::string page_layout = fm.get_str("layout", "archive");
    std::string page_title = fm.get_str("title", sec.title);
    std::string html_body = markdown_to_html(fm.body);

    Context page_ctx = ctx;
    page_ctx.set("page.title", page_title);
    page_ctx.set("page.url", sec.permalink);
    page_ctx.set("page.layout", page_layout);
    page_ctx.set("page.toc", fm.get_str("toc", ""));

    page_ctx.set("content", html_body);

    std::string layout_file = "layouts/" + page_layout + ".html";
    std::string page_html = engine_.render_file_with_layout(layout_file, "default", page_ctx);

    fs::path out_path = output_dir_ / sec.permalink.substr(1) / "index.html";
    utils::write_file(out_path, page_html);

    build_collection_pages(sec.name, "single");
}

void SiteBuilder::build_collection_pages(const std::string& name, const std::string& layout) {
    auto sorted = collections_.sorted_by_date_desc(name);
    for (auto& item : sorted) {
        Context ctx = make_page_context(item);
        ctx.set("content", item.html_body);

        std::string layout_name = item.layout.empty() ? layout : item.layout;
        std::string page_html = engine_.render_file_with_layout("layouts/" + layout_name + ".html",
                                                                  "default", ctx);

        fs::path out_path = output_dir_ / item.permalink.substr(1) / "index.html";
        utils::write_file(out_path, page_html);
    }
}

Context SiteBuilder::make_context() {
    Context ctx;
    ctx.template_dir = template_dir_;
    ctx.push_scope();

    ctx.set("site.title", config_.title);
    ctx.set("site.name", config_.name);
    ctx.set("site.description", config_.description);
    ctx.set("site.url", config_.url);
    ctx.set("site.baseurl", config_.baseurl);
    ctx.set("site.locale", config_.locale);
    ctx.set("site.time", utils::now_date("%Y-%m-%d %H:%M:%S %z"));

    for (auto& [k, v] : config_.social) {
        ctx.set("social." + k, v);
        ctx.set("site.social." + k, v);
    }

    for (size_t i = 0; i < config_.navigation.size(); i++) {
        for (auto& [k, v] : config_.navigation[i]) {
            ctx.set("site.data.navigation.main." + std::to_string(i) + "." + k, v);
        }
    }
    ctx.set("site.data.navigation.main.size", std::to_string(config_.navigation.size()));

    std::string nav_links;
    for (size_t i = 0; i < config_.navigation.size(); i++) {
        std::string title = config_.navigation[i]["title"];
        std::string url = config_.navigation[i]["url"];
        nav_links += "<li class=\"masthead__menu-item\"><a href=\"" +
                     config_.baseurl + url + "\">" + title + "</a></li>\n";
    }
    ctx.set("site.navigation_html", nav_links);

    for (auto& [name, col] : collections_.all()) {
        auto sorted = collections_.sorted_by_date_desc(name);
        std::string items_str;
        for (size_t i = 0; i < sorted.size(); i++) {
            if (i > 0) items_str += "|";
            items_str += sorted[i].filename;
        }
        ctx.set("site." + name + ".items", items_str);
        ctx.set("site." + name + ".size", std::to_string(sorted.size()));

        for (size_t i = 0; i < sorted.size(); i++) {
            auto& item = sorted[i];
            std::string prefix = name + "." + std::to_string(i);
            for (auto& [k, v] : item.meta) {
                ctx.set(prefix + "." + k, v);
            }
            ctx.set(prefix + ".content", item.html_body);
            ctx.set(prefix + ".excerpt", flat_get(item.fm.flat, "excerpt"));
        }
    }

    return ctx;
}

Context SiteBuilder::make_page_context(const ContentItem& item) {
    Context ctx = make_context();
    for (auto& [k, v] : item.meta) {
        ctx.set("page." + k, v);
    }
    ctx.set("page.content", item.html_body);
    ctx.set("page.collection", item.collection);
    if (item.fm.flat.count("toc"))
        ctx.set("page.toc", item.fm.flat.at("toc"));
    if (item.fm.flat.count("excerpt"))
        ctx.set("page.excerpt", item.fm.flat.at("excerpt"));
    if (item.fm.flat.count("date"))
        ctx.set("page.date", item.fm.flat.at("date"));
    if (item.fm.flat.count("venue"))
        ctx.set("page.venue", item.fm.flat.at("venue"));
    if (item.fm.flat.count("type"))
        ctx.set("page.type", item.fm.flat.at("type"));
    if (item.fm.flat.count("location"))
        ctx.set("page.location", item.fm.flat.at("location"));
    if (item.fm.flat.count("citation"))
        ctx.set("page.citation", item.fm.flat.at("citation"));
    if (item.fm.flat.count("paperurl"))
        ctx.set("page.paperurl", item.fm.flat.at("paperurl"));
    if (item.fm.flat.count("slidesurl"))
        ctx.set("page.slidesurl", item.fm.flat.at("slidesurl"));
    if (item.fm.flat.count("bibtexurl"))
        ctx.set("page.bibtexurl", item.fm.flat.at("bibtexurl"));
    if (item.fm.flat.count("category"))
        ctx.set("page.category", item.fm.flat.at("category"));
    if (item.fm.flat.count("read_time"))
        ctx.set("page.read_time", item.fm.flat.at("read_time"));
    if (item.fm.flat.count("share"))
        ctx.set("page.share", item.fm.flat.at("share"));
    if (item.fm.flat.count("tags"))
        ctx.set("page.tags", item.fm.flat.at("tags"));
    if (item.fm.flat.count("categories"))
        ctx.set("page.categories", item.fm.flat.at("categories"));
    return ctx;
}

std::string SiteBuilder::render_item_list(const std::string& collection_name,
                                           const std::string& include_template) {
    auto sorted = collections_.sorted_by_date_desc(collection_name);
    Context ctx = make_context();
    std::string result;
    for (auto& item : sorted) {
        for (auto& [k, v] : item.meta) {
            ctx.set("post." + k, v);
        }
        ctx.set("post.content", item.html_body);
        if (item.fm.flat.count("excerpt"))
            ctx.set("post.excerpt", item.fm.flat.at("excerpt"));
        if (item.fm.flat.count("date"))
            ctx.set("post.date", item.fm.flat.at("date"));
        if (item.fm.flat.count("venue"))
            ctx.set("post.venue", item.fm.flat.at("venue"));
        if (item.fm.flat.count("type"))
            ctx.set("post.type", item.fm.flat.at("type"));
        if (item.fm.flat.count("location"))
            ctx.set("post.location", item.fm.flat.at("location"));
        if (item.fm.flat.count("citation"))
            ctx.set("post.citation", item.fm.flat.at("citation"));
        if (item.fm.flat.count("paperurl"))
            ctx.set("post.paperurl", item.fm.flat.at("paperurl"));
        if (item.fm.flat.count("slidesurl"))
            ctx.set("post.slidesurl", item.fm.flat.at("slidesurl"));
        if (item.fm.flat.count("bibtexurl"))
            ctx.set("post.bibtexurl", item.fm.flat.at("bibtexurl"));
        if (item.fm.flat.count("category"))
            ctx.set("post.category", item.fm.flat.at("category"));
        ctx.set("post.collection", collection_name);

        result += engine_.render_file("includes/" + include_template, ctx);
    }
    return result;
}

void SiteBuilder::build_sitemap() {
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";

    auto add_url = [&](const std::string& loc) {
        xml += "  <url><loc>" + config_.url + config_.baseurl + loc + "</loc></url>\n";
    };

    for (auto& [name, col] : collections_.all()) {
        for (auto& item : col.items) {
            add_url(item.permalink);
        }
    }

    for (auto& sec : sections_) {
        add_url(sec.permalink);
    }

    if (fs::exists(content_dir_)) {
        for (auto& entry : fs::directory_iterator(content_dir_)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (ext != ".md") continue;
            std::string stem = entry.path().stem().string();
            if (stem == "config") continue;

            std::string raw = utils::read_file(entry.path());
            FrontMatter fm = parse_frontmatter(raw);
            std::string permalink = fm.get_str("permalink", "");
            if (!permalink.empty()) add_url(permalink);
        }
    }

    xml += "</urlset>\n";
    utils::write_file(output_dir_ / "sitemap.xml", xml);
}

void SiteBuilder::build_feed() {
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n";
    xml += "  <title>" + config_.title + "</title>\n";
    xml += "  <link href=\"" + config_.url + config_.baseurl + "/feed.xml\" rel=\"self\"/>\n";
    xml += "  <link href=\"" + config_.url + config_.baseurl + "\"/>\n";
    xml += "  <updated>" + utils::now_date("%Y-%m-%dT%H:%M:%SZ") + "</updated>\n";
    xml += "  <id>" + config_.url + config_.baseurl + "</id>\n";

    auto posts = collections_.sorted_by_date_desc("posts");
    auto pubs = collections_.sorted_by_date_desc("publications");
    auto talks = collections_.sorted_by_date_desc("talks");

    auto add_entry = [&](const ContentItem& item) {
        std::string title = flat_get(item.fm.flat, "title", item.filename);
        std::string date = flat_get(item.fm.flat, "date");
        xml += "  <entry>\n";
        xml += "    <title>" + utils::escape_once(title) + "</title>\n";
        xml += "    <link href=\"" + config_.url + config_.baseurl + item.permalink + "\"/>\n";
        xml += "    <id>" + config_.url + config_.baseurl + item.permalink + "</id>\n";
        if (!date.empty())
            xml += "    <updated>" + utils::date_to_xmlschema(date) + "</updated>\n";
        std::string excerpt = flat_get(item.fm.flat, "excerpt");
        if (!excerpt.empty())
            xml += "    <summary>" + utils::escape_once(excerpt) + "</summary>\n";
        xml += "  </entry>\n";
    };

    for (auto& p : posts) add_entry(p);
    for (auto& p : pubs) add_entry(p);
    for (auto& p : talks) add_entry(p);

    xml += "</feed>\n";
    utils::write_file(output_dir_ / "feed.xml", xml);
}

void SiteBuilder::copy_static_files() {
    if (fs::exists(content_dir_ / "files")) {
        fs::copy(content_dir_ / "files", output_dir_ / "files",
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
    if (fs::exists(content_dir_ / "images")) {
        fs::copy(content_dir_ / "images", output_dir_ / "images",
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }

    fs::path assets_src = content_dir_.parent_path() / "assets";
    if (fs::exists(assets_src)) {
        fs::copy(assets_src, output_dir_ / "assets",
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }

    fs::path css_src = content_dir_.parent_path() / "assets" / "css" / "main.css";
    if (!fs::exists(css_src)) {
        fs::path alt_css = content_dir_.parent_path() / "assets" / "css";
        if (fs::exists(alt_css)) {
            fs::copy(alt_css, output_dir_ / "assets/css",
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
    }
}
