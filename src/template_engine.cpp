#include "template_engine.h"
#include "markdown.h"

void Context::push_scope() {
    scopes.push_back(new std::map<std::string, std::string>());
}

void Context::pop_scope() {
    if (!scopes.empty()) {
        delete scopes.back();
        scopes.pop_back();
    }
}

void Context::set(const std::string& key, const std::string& val) {
    if (scopes.empty()) push_scope();
    (*scopes.back())[key] = val;
}

std::string Context::get(const std::string& key) const {
    for (int i = (int)scopes.size() - 1; i >= 0; --i) {
        auto it = scopes[i]->find(key);
        if (it != scopes[i]->end()) return it->second;
    }
    return "";
}

void Context::set_all(const std::map<std::string, std::string>& vars) {
    for (auto& [k, v] : vars) set(k, v);
}

TemplateEngine::TemplateEngine(const fs::path& tpl_dir) : tpl_dir_(tpl_dir) {}

std::string TemplateEngine::render_file(const std::string& filename, const Context& ctx) {
    fs::path path = tpl_dir_ / filename;
    std::string content = utils::read_file(path);
    if (content.empty()) return "[Template not found: " + filename + "]";
    Context c = ctx;
    return render_string(content, c);
}

std::string TemplateEngine::render_file_with_layout(const std::string& filename,
                                                     const std::string& parent_layout,
                                                     const Context& ctx) {
    fs::path path = tpl_dir_ / filename;
    std::string content = utils::read_file(path);
    if (content.empty()) return "[Template not found: " + filename + "]";

    Context c = ctx;
    std::string inner = render_string(content, c);

    if (!parent_layout.empty()) {
        c.set("content", inner);
        std::string layout_content = load_layout(parent_layout);
        if (!layout_content.empty()) {
            return render_string(layout_content, c);
        }
    }
    return inner;
}

std::string TemplateEngine::render_string(const std::string& tmpl, Context& ctx) {
    auto nodes = parse(tmpl);
    return render_nodes(nodes, ctx);
}

std::string TemplateEngine::render_with_layout(const std::string& content,
                                                const std::string& layout_name,
                                                Context& ctx) {
    std::string layout_content = load_layout(layout_name);
    if (layout_content.empty()) return content;

    Context c = ctx;
    c.set("content", content);
    return render_string(layout_content, c);
}

std::vector<TemplateNode> TemplateEngine::parse(const std::string& tmpl) {
    std::vector<TemplateNode> nodes;
    size_t pos = 0;
    while (pos < tmpl.size()) {
        auto tag_start = tmpl.find("{{", pos);
        auto block_start = tmpl.find("{%", pos);

        size_t next_pos = std::min(
            tag_start != std::string::npos ? tag_start : tmpl.size(),
            block_start != std::string::npos ? block_start : tmpl.size()
        );

        if (tag_start == std::string::npos && block_start == std::string::npos) {
            if (pos < tmpl.size()) {
                TemplateNode n;
                n.type = NodeType::Text;
                n.raw = tmpl.substr(pos);
                nodes.push_back(n);
            }
            break;
        }

        if (next_pos > pos) {
            TemplateNode n;
            n.type = NodeType::Text;
            n.raw = tmpl.substr(pos, next_pos - pos);
            nodes.push_back(n);
        }

        if (block_start != std::string::npos && block_start == next_pos) {
            auto tag_end = tmpl.find("%}", block_start + 2);
            if (tag_end == std::string::npos) {
                pos = block_start + 2;
                continue;
            }
            std::string tag = utils::trim(tmpl.substr(block_start + 2, tag_end - block_start - 2));
            pos = tag_end + 2;

            if (utils::starts_with(tag, "if ")) {
                std::string cond = tag.substr(3);
                TemplateNode n;
                n.type = NodeType::If;
                n.raw = cond;

                int depth = 1;
                size_t body_start = pos;
                size_t search_pos = body_start;
                std::vector<TemplateNode> if_children;

                while (depth > 0 && search_pos < tmpl.size()) {
                    auto next_if = tmpl.find("{%", search_pos);
                    if (next_if == std::string::npos) break;
                    auto next_tag_end = tmpl.find("%}", next_if + 2);
                    if (next_tag_end == std::string::npos) break;
                    std::string inner = utils::trim(tmpl.substr(next_if + 2, next_tag_end - next_if - 2));

                    if (utils::starts_with(inner, "if ")) {
                        depth++;
                        search_pos = next_tag_end + 2;
                    } else if (inner == "endif") {
                        depth--;
                        if (depth == 0) {
                            std::string body = tmpl.substr(body_start, next_if - body_start);
                            n.children = parse(body);
                            pos = next_tag_end + 2;
                        } else {
                            search_pos = next_tag_end + 2;
                        }
                    } else if (depth == 1 && utils::starts_with(inner, "elsif ")) {
                        std::string body = tmpl.substr(body_start, next_if - body_start);
                        n.children = parse(body);

                        std::string elsif_cond = inner.substr(6);
                        TemplateNode elsif_n;
                        elsif_n.type = NodeType::If;
                        elsif_n.raw = elsif_cond;

                        size_t elsif_body_start = next_tag_end + 2;
                        int d2 = 1;
                        size_t sp2 = elsif_body_start;
                        while (d2 > 0 && sp2 < tmpl.size()) {
                            auto ni2 = tmpl.find("{%", sp2);
                            if (ni2 == std::string::npos) break;
                            auto nte2 = tmpl.find("%}", ni2 + 2);
                            if (nte2 == std::string::npos) break;
                            std::string inner2 = utils::trim(tmpl.substr(ni2 + 2, nte2 - ni2 - 2));
                            if (utils::starts_with(inner2, "if ")) { d2++; sp2 = nte2 + 2; }
                            else if (inner2 == "endif") {
                                d2--;
                                if (d2 == 0) {
                                    std::string eb = tmpl.substr(elsif_body_start, ni2 - elsif_body_start);
                                    elsif_n.children = parse(eb);
                                    n.else_children.push_back(elsif_n);
                                    pos = nte2 + 2;
                                } else { sp2 = nte2 + 2; }
                            } else { sp2 = nte2 + 2; }
                        }
                        break;
                    } else if (depth == 1 && inner == "else") {
                        std::string body = tmpl.substr(body_start, next_if - body_start);
                        n.children = parse(body);
                        size_t else_body_start = next_tag_end + 2;
                        int d2 = 1;
                        size_t sp2 = else_body_start;
                        while (d2 > 0 && sp2 < tmpl.size()) {
                            auto ni2 = tmpl.find("{%", sp2);
                            if (ni2 == std::string::npos) break;
                            auto nte2 = tmpl.find("%}", ni2 + 2);
                            if (nte2 == std::string::npos) break;
                            std::string inner2 = utils::trim(tmpl.substr(ni2 + 2, nte2 - ni2 - 2));
                            if (utils::starts_with(inner2, "if ")) { d2++; sp2 = nte2 + 2; }
                            else if (inner2 == "endif") {
                                d2--;
                                if (d2 == 0) {
                                    std::string eb = tmpl.substr(else_body_start, ni2 - else_body_start);
                                    TemplateNode else_n;
                                    else_n.type = NodeType::Text;
                                    else_n.raw = "else_true";
                                    else_n.children = parse(eb);
                                    n.else_children.push_back(else_n);
                                    pos = nte2 + 2;
                                } else { sp2 = nte2 + 2; }
                            } else { sp2 = nte2 + 2; }
                        }
                        break;
                    } else {
                        search_pos = next_tag_end + 2;
                    }
                }
                nodes.push_back(n);
            } else if (utils::starts_with(tag, "for ")) {
                std::string rest = tag.substr(4);
                auto in_pos = rest.find(" in ");
                std::string var_name = utils::trim(rest.substr(0, in_pos));
                std::string collection = utils::trim(rest.substr(in_pos + 4));

                TemplateNode n;
                n.type = NodeType::For;
                n.var_name = var_name;
                n.raw = collection;

                int depth = 1;
                size_t search_pos = pos;
                while (depth > 0 && search_pos < tmpl.size()) {
                    auto ni = tmpl.find("{%", search_pos);
                    if (ni == std::string::npos) break;
                    auto nte = tmpl.find("%}", ni + 2);
                    if (nte == std::string::npos) break;
                    std::string inner = utils::trim(tmpl.substr(ni + 2, nte - ni - 2));
                    if (utils::starts_with(inner, "for ")) { depth++; search_pos = nte + 2; }
                    else if (inner == "endfor") {
                        depth--;
                        if (depth == 0) {
                            std::string body = tmpl.substr(pos, ni - pos);
                            n.children = parse(body);
                            pos = nte + 2;
                        } else { search_pos = nte + 2; }
                    } else { search_pos = nte + 2; }
                }
                nodes.push_back(n);
            } else if (tag == "toc") {
                TemplateNode n;
                n.type = NodeType::Toc;
                nodes.push_back(n);
            } else if (utils::starts_with(tag, "include ")) {
                TemplateNode n;
                n.type = NodeType::Include;
                std::string path = tag.substr(8);
                path = utils::trim(path);
                if (!path.empty() && path.front() == '"') path = path.substr(1);
                if (!path.empty() && path.back() == '"') path.pop_back();
                n.include_path = path;
                nodes.push_back(n);
            } else if (utils::starts_with(tag, "assign ")) {
                TemplateNode n;
                n.type = NodeType::Assign;
                auto eq = tag.find("=");
                n.assign_var = utils::trim(tag.substr(7, eq - 7));
                n.assign_expr = utils::trim(tag.substr(eq + 1));
                nodes.push_back(n);
            } else {
                TemplateNode n;
                n.type = NodeType::Variable;
                n.raw = tag;
                nodes.push_back(n);
            }
        } else {
            auto tag_end = tmpl.find("}}", tag_start + 2);
            if (tag_end == std::string::npos) {
                pos = tag_start + 2;
                continue;
            }
            std::string tag = utils::trim(tmpl.substr(tag_start + 2, tag_end - tag_start - 2));
            TemplateNode n;
            n.type = NodeType::Variable;
            n.raw = tag;
            nodes.push_back(n);
            pos = tag_end + 2;
        }
    }
    // Strip whitespace-only text nodes between block tags (like Jekyll)
    // This removes blank lines around {% if %}, {% endif %}, {% for %}, etc.
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].type == NodeType::Text) {
            bool is_whitespace = true;
            for (char c : nodes[i].raw) {
                if (c != '\n' && c != '\r' && c != ' ' && c != '\t') {
                    is_whitespace = false;
                    break;
                }
            }
            if (is_whitespace) {
                // Check if adjacent to a block tag
                bool prev_block = (i > 0 && nodes[i-1].type != NodeType::Text && nodes[i-1].type != NodeType::Variable);
                bool next_block = (i + 1 < nodes.size() && nodes[i+1].type != NodeType::Text && nodes[i+1].type != NodeType::Variable);
                if (prev_block || next_block) {
                    nodes.erase(nodes.begin() + i);
                    --i;
                }
            }
        }
    }
    return nodes;
}

std::string TemplateEngine::render_nodes(const std::vector<TemplateNode>& nodes, Context& ctx) {
    std::string result;
    for (auto& node : nodes) {
        result += render_node(node, ctx);
    }
    return result;
}

std::string TemplateEngine::render_node(const TemplateNode& node, Context& ctx) {
    switch (node.type) {
        case NodeType::Text:
            return node.raw;

        case NodeType::Variable: {
            std::string val = evaluate_variable(node.raw, ctx);
            auto pipe_pos = node.raw.find("|");
            if (pipe_pos != std::string::npos) {
                std::string expr = utils::trim(node.raw.substr(0, pipe_pos));
                val = evaluate_variable(expr, ctx);
                std::string filters_str = node.raw.substr(pipe_pos + 1);
                auto filter_parts = utils::split(filters_str, '|');
                for (auto& f : filter_parts) {
                    f = utils::trim(f);
                    std::string fname = f;
                    std::vector<std::string> fargs;
                    auto paren = f.find('(');
                    if (paren != std::string::npos) {
                        fname = f.substr(0, paren);
                        std::string args_str = f.substr(paren + 1);
                        if (!args_str.empty() && args_str.back() == ')')
                            args_str.pop_back();
                        fargs = utils::split(args_str, ',');
                        for (auto& a : fargs) {
                            a = utils::trim(a);
                            bool quoted = (!a.empty() && (a.front() == '"' || a.front() == '\''));
                            if (!a.empty() && a.front() == '"') a = a.substr(1);
                            if (!a.empty() && a.front() == '\'') a = a.substr(1);
                            if (!a.empty() && a.back() == '"') a.pop_back();
                            if (!a.empty() && a.back() == '\'') a.pop_back();
                            bool is_number = (!a.empty() && std::all_of(a.begin(), a.end(), [](char c){ return std::isdigit(c) || c == '-'; }));
                            if (!quoted && !is_number && !a.empty()) a = evaluate_variable(a, ctx);
                        }
                    }
                    val = apply_filter(val, fname, fargs, ctx);
                }
            }
            return val;
        }

        case NodeType::Assign: {
            std::string val = evaluate_variable(node.assign_expr, ctx);
            ctx.set(node.assign_var, val);
            return "";
        }

        case NodeType::If: {
            if (evaluate_condition(node.raw, ctx)) {
                return render_nodes(node.children, ctx);
            }
            for (auto& alt : node.else_children) {
                if (evaluate_condition(alt.raw, ctx)) {
                    return render_nodes(alt.children, ctx);
                }
            }
            if (!node.else_children.empty()) {
                auto& last = node.else_children.back();
                if (last.raw == "else_true") {
                    return render_nodes(last.children, ctx);
                }
            }
            return "";
        }

        case NodeType::For: {
            std::string collection_name = node.raw;
            std::string items_str = ctx.get(collection_name);

            std::vector<std::string> items;
            if (utils::contains(collection_name, ".")) {
                std::string resolved = resolve_dot_path(collection_name, ctx);
                if (!resolved.empty()) {
                    items = utils::split(resolved, '|');
                }
            }
            if (items.empty() && !items_str.empty()) {
                items = utils::split(items_str, '|');
            }
            if (items.empty()) return "";

            ctx.push_scope();
            std::string result;
            for (int i = 0; i < (int)items.size(); i++) {
                ctx.set(node.var_name, items[i]);
                ctx.set("forloop.index", std::to_string(i + 1));
                ctx.set("forloop.index0", std::to_string(i));
                ctx.set("forloop.first", i == 0 ? "true" : "false");
                ctx.set("forloop.last", i == (int)items.size() - 1 ? "true" : "false");
                ctx.set("forloop.length", std::to_string(items.size()));
                result += render_nodes(node.children, ctx);
            }
            ctx.pop_scope();
            return result;
        }

        case NodeType::Include: {
            std::string inc_content = load_include(node.include_path);
            if (inc_content.empty()) return "[Include not found: " + node.include_path + "]";
            Context c = ctx;
            return render_string(inc_content, c);
        }

        case NodeType::Toc: {
            std::string content = ctx.get("content");
            if (content.empty()) return "";

            struct Heading { int level; std::string id; std::string text; };
            std::vector<Heading> headings;

            size_t pos = 0;
            while (pos < content.size()) {
                auto tag_start = content.find("<h", pos);
                if (tag_start == std::string::npos) break;

                auto tag_close = content.find(">", tag_start);
                if (tag_close == std::string::npos) break;

                std::string tag = content.substr(tag_start, tag_close - tag_start + 1);

                int level = 0;
                if (tag.size() >= 3 && tag[2] >= '1' && tag[2] <= '6')
                    level = tag[2] - '0';

                if (level < 1 || level > 6) { pos = tag_close + 1; continue; }

                std::string id;
                auto id_pos = tag.find("id=\"");
                if (id_pos != std::string::npos) {
                    auto id_start = id_pos + 4;
                    auto id_end = tag.find('"', id_start);
                    if (id_end != std::string::npos)
                        id = tag.substr(id_start, id_end - id_start);
                }

                auto text_end = content.find("</h" + std::to_string(level), tag_close + 1);
                std::string text;
                if (text_end != std::string::npos) {
                    text = utils::strip_html(content.substr(tag_close + 1, text_end - tag_close - 1));
                    text = utils::trim(text);
                }

                if (!id.empty() && !text.empty())
                    headings.push_back({level, id, text});

                pos = (text_end != std::string::npos) ? text_end + 5 : tag_close + 1;
            }

            if (headings.empty()) return "";

            std::string result;
            result += "<nav class=\"toc\">\n<ul>\n";
            std::vector<int> open_levels;
            for (size_t i = 0; i < headings.size(); i++) {
                auto& h = headings[i];
                if (i > 0) {
                    if (h.level > open_levels.back()) {
                        // Going deeper: open one <ul> and track it
                        result += "<ul>\n";
                        open_levels.push_back(h.level);
                    } else if (h.level < open_levels.back()) {
                        // Going shallower: close </li> then close <ul></li> until we reach the right parent
                        result += "</li>\n";
                        while (!open_levels.empty() && open_levels.back() > h.level) {
                            result += "</ul>\n</li>\n";
                            open_levels.pop_back();
                        }
                    } else {
                        // Same level: close previous </li>
                        result += "</li>\n";
                    }
                } else {
                    open_levels.push_back(h.level);
                }
                result += "<li><a href=\"#" + h.id + "\">" + h.text + "</a>";
            }
            // Close all remaining open levels
            result += "</li>\n";
            while (open_levels.size() > 1) {
                result += "</ul>\n</li>\n";
                open_levels.pop_back();
            }
            result += "</ul>\n</nav>\n";
            return result;
        }
    }
    return "";
}

std::string TemplateEngine::evaluate_variable(const std::string& expr, const Context& ctx) {
    std::string e = utils::trim(expr);
    if (e.empty()) return "";
    if (e.front() == '"' && e.back() == '"') return e.substr(1, e.size() - 2);
    if (e.front() == '\'' && e.back() == '\'') return e.substr(1, e.size() - 2);
    if (e == "true") return "true";
    if (e == "false") return "false";
    if (e == "nil" || e == "null") return "";
    return resolve_dot_path(e, ctx);
}

std::string TemplateEngine::resolve_dot_path(const std::string& path, const Context& ctx) {
    if (path.empty()) return "";
    if (path == "content") return ctx.get("content");

    auto parts = utils::split(path, '.');
    if (parts.size() == 1) {
        return ctx.get(parts[0]);
    }

    std::string result;
    std::string current;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i == 0) {
            current = ctx.get(parts[i]);
        } else {
            std::string key = parts[i];
            if (current.empty() && i == 1) {
                current = ctx.get(parts[0] + "." + key);
            } else if (!current.empty()) {
                auto sub = utils::split(current, '\n');
                if (sub.size() > 1) {
                    current = sub[0];
                }
            }
        }
    }
    return current;
}

bool TemplateEngine::evaluate_condition(const std::string& expr, const Context& ctx) {
    std::string e = utils::trim(expr);

    if (utils::contains(e, " == ")) {
        auto pos = e.find(" == ");
        std::string left = utils::trim(e.substr(0, pos));
        std::string right = utils::trim(e.substr(pos + 4));
        std::string lv = resolve_dot_path(left, ctx);
        std::string rv = evaluate_variable(right, ctx);
        return lv == rv;
    }

    if (utils::contains(e, " != ")) {
        auto pos = e.find(" != ");
        std::string left = utils::trim(e.substr(0, pos));
        std::string right = utils::trim(e.substr(pos + 4));
        std::string lv = resolve_dot_path(left, ctx);
        std::string rv = evaluate_variable(right, ctx);
        return lv != rv;
    }

    if (utils::contains(e, " contains ")) {
        auto pos = e.find(" contains ");
        std::string left = utils::trim(e.substr(0, pos));
        std::string right = utils::trim(e.substr(pos + 10));
        std::string lv = resolve_dot_path(left, ctx);
        std::string rv = evaluate_variable(right, ctx);
        return utils::contains(lv, rv);
    }

    std::string val = resolve_dot_path(e, ctx);
    return !val.empty() && val != "false" && val != "nil" && val != "0";
}

std::string TemplateEngine::apply_filter(const std::string& val, const std::string& filter,
                                          const std::vector<std::string>& args, const Context& ctx) {
    if (filter == "markdownify") return markdown_to_html(val);
    if (filter == "strip_html") return utils::strip_html(val);
    if (filter == "strip_newlines") {
        std::string r = val;
        r.erase(std::remove(r.begin(), r.end(), '\n'), r.end());
        return r;
    }
    if (filter == "escape_once") return utils::escape_once(val);
    if (filter == "size") return std::to_string(val.size());
    if (filter == "upcase") { std::string r = val; std::transform(r.begin(), r.end(), r.begin(), ::toupper); return r; }
    if (filter == "downcase") return utils::to_lower(val);
    if (filter == "capitalize") {
        if (val.empty()) return val;
        std::string r = val;
        r[0] = std::toupper(r[0]);
        return r;
    }
    if (filter == "trim") return utils::trim(val);
    if (filter == "truncatewords") {
        int n = 10;
        if (!args.empty()) { try { n = std::stoi(args[0]); } catch (...) {} }
        return utils::truncatewords(val, n);
    }
    if (filter == "default") return val.empty() ? (args.empty() ? "" : args[0]) : val;
    if (filter == "prepend") return (args.empty() ? "" : args[0]) + val;
    if (filter == "append") return val + (args.empty() ? "" : args[0]);
    if (filter == "date_to_string") {
        return utils::date_format(val, "%B %d, %Y");
    }
    if (filter == "date_to_xmlschema") return utils::date_to_xmlschema(val);
    if (filter == "jsonify") return utils::jsonify(val);
    if (filter == "slugify") return utils::slugify(val);
    if (filter == "remove") {
        return utils::replace(val, args.empty() ? "" : args[0], "");
    }
    if (filter == "slice") {
        if (args.size() >= 2) {
            try {
                int start = std::stoi(args[0]);
                int len = std::stoi(args[1]);
                int sz = (int)val.size();
                if (start < 0) start = sz + start;
                if (start >= 0 && start + len <= sz)
                    return val.substr(start, len);
            } catch (...) {}
        }
        return val;
    }
    if (filter == "divided_by") {
        if (args.size() >= 1) {
            try {
                int divisor = std::stoi(args[0]);
                if (divisor != 0) {
                    return std::to_string(std::stoi(val) / divisor);
                }
            } catch (...) {}
        }
        return val;
    }
    if (filter == "abs") {
        try { return std::to_string(std::abs(std::stoi(val))); }
        catch (...) { return val; }
    }
    return val;
}

std::string TemplateEngine::load_include(const std::string& name) {
    fs::path p = tpl_dir_ / "includes" / name;
    if (!fs::exists(p)) {
        p = tpl_dir_ / name;
    }
    return utils::read_file(p);
}

std::string TemplateEngine::load_layout(const std::string& name) {
    fs::path p = tpl_dir_ / "layouts" / (name + ".html");
    if (!fs::exists(p)) {
        p = tpl_dir_ / name;
    }
    return utils::read_file(p);
}
