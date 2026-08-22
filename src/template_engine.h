#pragma once
#include "utils.h"
#include <functional>

struct Context {
    std::vector<std::map<std::string, std::string>*> scopes;
    fs::path template_dir;

    Context() = default;
    Context(const Context& other) : template_dir(other.template_dir) {
        for (auto* s : other.scopes) {
            scopes.push_back(new std::map<std::string, std::string>(*s));
        }
    }
    Context& operator=(const Context& other) {
        if (this != &other) {
            for (auto* s : scopes) delete s;
            scopes.clear();
            template_dir = other.template_dir;
            for (auto* s : other.scopes) {
                scopes.push_back(new std::map<std::string, std::string>(*s));
            }
        }
        return *this;
    }
    ~Context() {
        for (auto* s : scopes) delete s;
        scopes.clear();
    }

    void push_scope();
    void pop_scope();
    void set(const std::string& key, const std::string& val);
    std::string get(const std::string& key) const;
    void set_all(const std::map<std::string, std::string>& vars);
};

enum class NodeType { Text, Variable, If, For, Include, Assign, Toc };

struct TemplateNode {
    NodeType type;
    std::string raw;
    std::string var_name;
    std::string filter_name;
    std::vector<std::string> filter_args;
    std::vector<TemplateNode> children;
    std::vector<TemplateNode> else_children;
    std::string include_path;
    std::string assign_var;
    std::string assign_expr;
};

class TemplateEngine {
public:
    TemplateEngine(const fs::path& tpl_dir);

    std::string render_file(const std::string& filename, const Context& ctx);
    std::string render_file_with_layout(const std::string& filename,
                                         const std::string& parent_layout,
                                         const Context& ctx);
    std::string render_string(const std::string& tmpl, Context& ctx);
    std::string render_with_layout(const std::string& content,
                                    const std::string& layout_name,
                                    Context& ctx);

private:
    fs::path tpl_dir_;

    std::vector<TemplateNode> parse(const std::string& tmpl);
    std::string render_nodes(const std::vector<TemplateNode>& nodes, Context& ctx);
    std::string render_node(const TemplateNode& node, Context& ctx);
    std::string evaluate_variable(const std::string& expr, const Context& ctx);
    bool evaluate_condition(const std::string& expr, const Context& ctx);
    std::string apply_filter(const std::string& val, const std::string& filter,
                             const std::vector<std::string>& args, const Context& ctx);
    std::string resolve_dot_path(const std::string& path, const Context& ctx);
    std::string load_include(const std::string& name);
    std::string load_layout(const std::string& name);
};
