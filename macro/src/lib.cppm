export module liorm.macro;

import std;
import pmacro;

namespace
{
using Output = std::expected<pmacro::TokenStream, pmacro::Error>;

struct Options {
    std::optional<std::string> name;
    std::optional<std::string> primary_key;
    std::optional<std::string> storage;
    bool                       nullable {};
    bool                       primary {};
};

auto fail(pmacro::Context&          context,
          const pmacro::SourceInfo& source,
          pmacro::Span              fallback,
          std::string_view          message) -> Output {
    context.diagnostic(
        pmacro::DiagnosticLevel::Error, source.diagnostic.value_or(fallback), message);
    return std::unexpected(pmacro::Error::Failure);
}

auto string_argument(pmacro::TokenTree group) -> std::expected<std::string, std::string> {
    if (group.kind() != pmacro::TokenKind::Group ||
        group.delimiter() != pmacro::Delimiter::Parenthesis)
        return std::unexpected("option requires parentheses with one string literal");
    auto args = group.children();
    if (args.size() != 1 || args[0].kind() != pmacro::TokenKind::Literal)
        return std::unexpected("option requires one string literal");
    auto text = std::string(args[0].spelling());
    if (text.size() < 3 || text.front() != '"' || text.back() != '"' ||
        text.find('\\') != std::string::npos)
        return std::unexpected("names must be nonempty unescaped string literals");
    for (std::size_t i = 1; i + 1 < text.size(); ++i)
        if (static_cast<unsigned char>(text[i]) < 0x20 || text[i] == '"')
            return std::unexpected("names must be nonempty unescaped string literals");
    return text.substr(1, text.size() - 2);
}

auto storage_argument(pmacro::TokenTree group) -> std::expected<std::string, std::string> {
    if (group.kind() != pmacro::TokenKind::Group ||
        group.delimiter() != pmacro::Delimiter::Parenthesis)
        return std::unexpected("storage requires parentheses with one value kind");
    auto args = group.children();
    if (args.size() != 1 || args[0].kind() != pmacro::TokenKind::Ident)
        return std::unexpected("storage requires one value kind");
    auto kind = args[0].spelling();
    if (kind == "integer") return "Integer";
    if (kind == "real") return "Real";
    if (kind == "boolean") return "Boolean";
    if (kind == "text") return "Text";
    if (kind == "blob") return "Blob";
    return std::unexpected("unknown storage value kind");
}

auto parse_options(pmacro::Context&                       context,
                   const std::vector<pmacro::HelperInfo>& helpers,
                   bool table) -> std::expected<Options, std::string> {
    Options result;
    for (const auto& helper : helpers) {
        if (helper.owner != "liorm::table") continue;
        if (! helper.arguments) return std::unexpected("liorm helper arguments are unavailable");
        auto tokens = context.tokens(*helper.arguments);
        if (! tokens) return std::unexpected("liorm helper tokens are unavailable");
        if (tokens->size() == 0) return std::unexpected("empty liorm helper");
        std::size_t i = 0;
        while (i < tokens->size()) {
            auto token = (*tokens)[i++];
            if (token.kind() != pmacro::TokenKind::Ident)
                return std::unexpected("expected liorm helper option");
            auto option = token.spelling();
            if (option == "name" || (table && option == "primary")) {
                if (i == tokens->size())
                    return std::unexpected("liorm option requires an argument");
                auto value = string_argument((*tokens)[i++]);
                if (! value) return std::unexpected(value.error());
                auto& target = option == "name" ? result.name : result.primary_key;
                if (target)
                    return std::unexpected("duplicate liorm option: " + std::string(option));
                target = std::move(*value);
            } else if (! table && option == "storage") {
                if (i == tokens->size()) return std::unexpected("storage requires a value kind");
                auto value = storage_argument((*tokens)[i++]);
                if (! value) return std::unexpected(value.error());
                if (result.storage) return std::unexpected("duplicate liorm option: storage");
                result.storage = std::move(*value);
            } else if (! table && option == "nullable") {
                if (result.nullable) return std::unexpected("duplicate liorm option: nullable");
                result.nullable = true;
            } else if (! table && option == "primary") {
                if (result.primary) return std::unexpected("duplicate liorm option: primary");
                result.primary = true;
            } else {
                return std::unexpected("unknown liorm option: " + std::string(option));
            }
            if (i < tokens->size()) {
                if ((*tokens)[i].kind() != pmacro::TokenKind::Punct ||
                    (*tokens)[i].spelling() != ",")
                    return std::unexpected("expected comma between liorm options");
                if (++i == tokens->size()) return std::unexpected("trailing comma in liorm helper");
            }
        }
    }
    return result;
}

auto quote(std::string_view value) -> std::string {
    return "\"" + std::string(value) + "\"";
}

auto fold(std::string_view value) -> std::string {
    std::string result(value);
    for (char& c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return result;
}

auto columns_type_name(std::string_view name) -> std::string {
    constexpr std::string_view digits = "0123456789abcdef";
    auto                       result = "liorm_columns_" + std::to_string(name.size()) + '_';
    for (unsigned char c : name) {
        result += digits[c >> 4];
        result += digits[c & 15];
    }
    return result;
}
} // namespace

[[pmacro::define("liorm")]]
auto table(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    auto snapshot = context.declaration();
    if (! snapshot)
        return fail(context, {}, input.item_span, "liorm table requires a complete declaration");
    const auto& declaration = **snapshot;
    if (! declaration.name)
        return fail(
            context, declaration.source, input.item_span, "liorm table requires a named type");
    if (declaration.scope != pmacro::DeclarationScope::Namespace)
        return fail(context,
                    declaration.source,
                    input.item_span,
                    "liorm table requires a namespace-scope declaration");
    const auto* record = std::get_if<pmacro::RecordInfo>(&declaration.data);
    if (! record || record->dependent || record->has_bases ||
        record->kind == pmacro::RecordKind::Union)
        return fail(context,
                    declaration.source,
                    input.item_span,
                    "liorm table requires a non-template record without bases");
    for (const auto& token : input.item) {
        if (token.kind() == pmacro::TokenKind::Group &&
            token.delimiter() == pmacro::Delimiter::Brace)
            break;
        if (token.kind() == pmacro::TokenKind::Punct &&
            (token.spelling() == ":" || token.spelling() == "::"))
            return fail(context,
                        declaration.source,
                        input.item_span,
                        "liorm table must be defined in its owning namespace");
    }
    auto config = parse_options(context, declaration.helpers, true);
    if (! config) return fail(context, declaration.source, input.item_span, config.error());
    if (! config->name || ! config->primary_key)
        return fail(context,
                    declaration.source,
                    input.item_span,
                    "liorm table requires name and primary options");
    if (record->fields.empty())
        return fail(context, declaration.source, input.item_span, "liorm table requires fields");

    const auto& qualified = *declaration.name;
    auto        source    = std::string();
    auto        columns   = std::string();
    auto definition = "::liorm::schema::TableDefinition<" + qualified + ',' + quote(*config->name) +
                      ',' + quote(*config->primary_key);
    std::set<std::string> names;
    std::set<std::string> members;
    std::size_t           primary_count = 0;
    for (const auto& field : record->fields) {
        if (! field.name || field.kind != pmacro::FieldKind::Named ||
            field.access != pmacro::Access::Public || field.bit_width || field.type.dependent ||
            ! field.type.type_id)
            return fail(context,
                        field.source,
                        input.item_span,
                        "liorm table requires public named non-bitfield members");
        if (*field.name == qualified)
            return fail(context,
                        field.source,
                        input.item_span,
                        "liorm column member name conflicts with model type name");
        members.insert(*field.name);
        auto options = parse_options(context, field.helpers, false);
        if (! options) return fail(context, field.source, input.item_span, options.error());
        if (! options->storage)
            return fail(
                context, field.source, input.item_span, "liorm field requires storage option");
        if (options->primary && options->nullable)
            return fail(
                context, field.source, input.item_span, "liorm primary field cannot be nullable");
        auto name = options->name.value_or(*field.name);
        if (! names.insert(fold(name)).second)
            return fail(context, field.source, input.item_span, "duplicate liorm column name");
        primary_count += options->primary;
        auto member_type = "decltype(" + qualified + "::" + *field.name + ")";
        source += "static_assert(!__is_reference(" + member_type + ")&&!__is_const(" + member_type +
                  "),\"liorm table fields cannot be const or references\");";
        definition += ",::liorm::schema::ColumnSpec<&" + qualified + "::" + *field.name + ',' +
                      quote(name) + ",::liorm::ValueKind::" + *options->storage + ',' +
                      (options->nullable ? "true" : "false") + ',' +
                      (options->primary ? "true" : "false") + '>';
        columns += "static constexpr ::liorm::Column<&" + qualified + "::" + *field.name + "> " +
                   *field.name + "{};";
    }
    if (primary_count == 0)
        return fail(
            context, declaration.source, input.item_span, "liorm table requires a primary field");
    auto       columns_name = columns_type_name(qualified);
    const auto columns_base = columns_name;
    for (std::size_t suffix = 0; members.contains(columns_name); ++suffix)
        columns_name = columns_base + '_' + std::to_string(suffix);
    source += "extern \"C++\" {";
    if (declaration.after_item_export == pmacro::AfterItemExport::Required) source += "export {";
    source += "struct " + columns_name + "{" + columns + "};";
    source += "constexpr auto liorm_table_definition(" + qualified + "*) { return " + definition +
              ">{ }; }";
    source += "constexpr auto liorm_columns_definition(" + qualified + "*) { return " +
              columns_name + "{}; }";
    if (declaration.after_item_export == pmacro::AfterItemExport::Required) source += '}';
    source += '}';
    return context.parse(source);
}

[[pmacro::define("liorm")]]
auto sqlite_enum(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    auto snapshot = context.declaration();
    if (! snapshot)
        return fail(
            context, {}, input.item_span, "liorm sqlite_enum requires a complete declaration");
    const auto& declaration = **snapshot;
    if (! declaration.name)
        return fail(context,
                    declaration.source,
                    input.item_span,
                    "liorm sqlite_enum requires a named type");
    if (declaration.scope != pmacro::DeclarationScope::Namespace)
        return fail(context,
                    declaration.source,
                    input.item_span,
                    "liorm sqlite_enum requires a namespace-scope declaration");
    const auto* info = std::get_if<pmacro::EnumInfo>(&declaration.data);
    if (! info)
        return fail(
            context, declaration.source, input.item_span, "liorm sqlite_enum requires an enum");
    if (info->enumerators.empty())
        return fail(context,
                    declaration.source,
                    input.item_span,
                    "liorm sqlite_enum requires at least one enumerator");
    const auto& qualified = declaration.qualified_name.value_or(*declaration.name);
    auto        count     = std::to_string(info->enumerators.size());
    auto        source    = std::string();
    if (declaration.after_item_export == pmacro::AfterItemExport::Required) source += "export ";
    source += "constexpr auto liorm_sqlite_enum_values(" + qualified + "*)->::rstd::array<" +
              qualified + "," + count + ">{return {";
    for (const auto& enumerator : info->enumerators)
        source += qualified + "::" + enumerator.name + ",";
    source += "};}";
    return context.parse(source);
}
