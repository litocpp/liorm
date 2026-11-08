module;
#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>
export module liorm:sqlite;
export import :query;

export namespace liorm::query {
struct Parameter {
  unsigned slot{};
  ParameterType type{};
  unsigned name_offset{}, name_length{};
};
class CompiledQuery {
  friend class Compiler;
  friend class ExpandedQuery;
  std::array<rstd::byte, 16384> sql_{};
  std::array<char, 8192> names_{};
  std::array<Parameter, 64> parameters_{};
  std::array<const void *, 64> member_identities_{};
  unsigned sql_size_{}, name_size_{}, parameter_count_{};
  constexpr CompiledQuery() = default;

public:
  constexpr auto sql() const -> rstd::ref<rstd::str> {
    return rstd::ref<rstd::str>::from_raw_parts_unchecked(
        sql_.data(), rstd::usize(sql_size_));
  }
  constexpr auto parameter_count() const -> unsigned {
    return parameter_count_;
  }
  constexpr auto parameter(unsigned index) const -> rstd::Option<Parameter> {
    if (index >= parameter_count_)
      return rstd::None();
    return rstd::Some(Parameter(parameters_[index]));
  }
  constexpr auto parameter_name(unsigned index) const -> std::string_view {
    if (index >= parameter_count_)
      return {};
    auto p = parameters_[index];
    return {names_.data() + p.name_offset, p.name_length};
  }
  constexpr auto parameter_has_member(unsigned index) const -> bool {
    return index < parameter_count_ && member_identities_[index] != nullptr;
  }
  template <auto Member>
  constexpr auto parameter_is(unsigned index) const -> bool {
    return index < parameter_count_ &&
           member_identities_[index] == &member_parameter_identity<Member>;
  }
  constexpr auto parameter_is_group(unsigned) const -> bool { return false; }
  constexpr auto parameter_slot(std::string_view name) const
      -> rstd::Option<rstd::usize> {
    for (unsigned i = 0; i < parameter_count_; ++i)
      if (parameter_name(i) == name)
        return rstd::Some(rstd::usize(parameters_[i].slot));
    return rstd::None();
  }
};
struct ParameterGroupSlots {
  // The group occupies count contiguous one-based slots; empty is {0, 0}.
  rstd::usize first{};
  rstd::usize count{};
};
struct ExpansionLimits {
  unsigned parameters = 1048576;
  unsigned sql = 1048576;
};
struct ExpansionPlan {
  // Physical slots after logical-name deduplication, and SQL bytes excluding
  // NUL.
  unsigned parameters{};
  unsigned sql{};
};
// The planned path opts into a wide policy, not a database or memory guarantee.
inline constexpr ExpansionLimits planned_expansion_ceiling{
    2147483647U, std::numeric_limits<unsigned>::max()};
// SQL and parameter metadata are owned; views returned by sql() remain valid
// only while this object is alive and has not been moved or assigned.
class ExpandedQuery {
  friend class Compiler;
  std::vector<rstd::byte> sql_;
  CompiledQuery metadata_;
  std::array<bool, 64> groups_{};
  std::array<unsigned, 64> counts_{};

public:
  auto sql() const -> rstd::ref<rstd::str> {
    return rstd::ref<rstd::str>::from_raw_parts_unchecked(
        sql_.data(), rstd::usize(sql_.size()));
  }
  auto parameter_count() const -> unsigned {
    return metadata_.parameter_count();
  }
  auto parameter(unsigned index) const -> rstd::Option<Parameter> {
    return metadata_.parameter(index);
  }
  auto parameter_name(unsigned index) const -> std::string_view {
    return metadata_.parameter_name(index);
  }
  auto parameter_has_member(unsigned index) const -> bool {
    return metadata_.parameter_has_member(index);
  }
  template <auto Member> auto parameter_is(unsigned index) const -> bool {
    return metadata_.template parameter_is<Member>(index);
  }
  auto parameter_is_group(unsigned index) const -> bool {
    return index < metadata_.parameter_count_ && groups_[index];
  }
  auto parameter_slot(std::string_view name) const
      -> rstd::Option<rstd::usize> {
    for (unsigned i = 0; i < metadata_.parameter_count_; ++i)
      if (metadata_.parameter_name(i) == name && !groups_[i])
        return rstd::Some(rstd::usize(metadata_.parameters_[i].slot));
    return rstd::None();
  }
  auto parameter_group(std::string_view name) const
      -> rstd::Option<ParameterGroupSlots> {
    for (unsigned i = 0; i < metadata_.parameter_count_; ++i)
      if (metadata_.parameter_name(i) == name && groups_[i])
        return rstd::Some(
            ParameterGroupSlots{rstd::usize(metadata_.parameters_[i].slot),
                                rstd::usize(counts_[i])});
    return rstd::None();
  }
};
} // namespace liorm::query

namespace liorm::query {
struct Source {
  Text name{};
  Id instance{};
  unsigned table{};
  bool known{}, nullable{};
};
enum class FactKind { Unknown, Known, NullLiteral };
struct Fact {
  FactKind kind{};
  ValueType type{};
};
struct ProjectionFacts {
  Id list{};
  std::array<Fact, 512> values{};
};
struct Scope {
  const Scope *parent{};
  std::array<Source, 128> sources{};
  Id write_target{};
  const ProjectionFacts *projection{};
  bool ordering{}, conflict_update{}, window_projection{};
  unsigned count{};
};
struct CteScope {
  const CteScope *parent{};
  std::array<Text, 128> names{};
  std::array<Text, 128> reserved{};
  unsigned reserved_count{};
  unsigned count{};
};
struct PreparedQuery {
  std::array<unsigned, 512> parameter_slots{};
  std::array<Fact, 4096> facts{};
  unsigned count{};
};
class Compiler {
  const QueryBuilder &builder_;
  PreparedQuery prepared_{};
  CompiledQuery result_{};
  Error error_{};
  bool preparing_{true};
  bool expanded_mode_{}, measuring_{};
  unsigned fact_cursor_{}, measured_sql_{};
  ExpandedQuery *expanded_{};
  ExpansionLimits expansion_limits_{};
  std::array<bool, 64> groups_{};
  std::array<unsigned, 64> group_counts_{};
  unsigned next_slot_{1};

  constexpr auto text(Text t) const -> std::string_view {
    return builder_.text(t);
  }
  constexpr auto node(Id id) const -> const Node & {
    return builder_.nodes_[id];
  }
  constexpr void fail(ErrorKind kind, Id id = 0, std::string_view name = {}) {
    if (error_.kind != ErrorKind::None)
      return;
    error_.kind = kind;
    error_.node = id;
    error_.length = name.size() < error_.context.size()
                        ? unsigned(name.size())
                        : unsigned(error_.context.size());
    error_.truncated = name.size() > error_.length;
    for (unsigned i = 0; i < error_.length; ++i)
      error_.context[i] = name[i];
  }
  constexpr auto enter(unsigned depth, Id id) -> bool {
    if (error_.kind != ErrorKind::None)
      return false;
    if (depth >= builder_.limits_.depth) {
      fail(ErrorKind::Depth, id);
      return false;
    }
    return true;
  }
  constexpr void out(char c) {
    if (preparing_ || error_.kind != ErrorKind::None)
      return;
    if (measuring_) {
      if (measured_sql_ >= expansion_limits_.sql) {
        fail(ErrorKind::Capacity, 0, "sql");
        return;
      }
      ++measured_sql_;
      return;
    }
    if (expanded_) {
      if (expanded_->sql_.size() >= expansion_limits_.sql) {
        fail(ErrorKind::Capacity, 0, "sql");
        return;
      }
      expanded_->sql_.push_back(rstd::byte{static_cast<unsigned char>(c)});
      return;
    }
    if (result_.sql_size_ >= builder_.limits_.sql) {
      fail(ErrorKind::Capacity, 0, "sql");
      return;
    }
    result_.sql_[result_.sql_size_++] =
        rstd::byte{static_cast<unsigned char>(c)};
  }
  constexpr void out(std::string_view value) {
    if (measuring_ && !preparing_ && error_.kind == ErrorKind::None) {
      if (value.size() > expansion_limits_.sql - measured_sql_) {
        fail(ErrorKind::Capacity, 0, "sql");
        return;
      }
      measured_sql_ += unsigned(value.size());
      return;
    }
    for (char c : value)
      out(c);
  }
  constexpr void number(long long value) {
    unsigned long long magnitude = static_cast<unsigned long long>(value);
    if (value < 0) {
      out('-');
      magnitude = 0 - magnitude;
    }
    char digits[20]{};
    unsigned count = 0;
    do {
      digits[count++] = char('0' + magnitude % 10);
      magnitude /= 10;
    } while (magnitude);
    while (count)
      out(digits[--count]);
  }
  constexpr void quote(Text value, char delimiter = '"') {
    if (measuring_ && error_.kind == ErrorKind::None) {
      // Every non-ASCII SQL byte comes from quoted builder text. Validate it
      // before allocating the planned output, using the same UTF-8 checker.
      auto raw = text(value);
      std::array<rstd::byte, 8192> bytes{};
      for (unsigned i = 0; i < raw.size(); ++i)
        bytes[i] = rstd::byte{static_cast<unsigned char>(raw[i])};
      if (rstd::str_::from_utf8(rstd::slice<rstd::u8>::from_raw_parts(
                                    bytes.data(), rstd::usize(raw.size())))
              .is_err()) {
        fail(ErrorKind::InvalidName);
        return;
      }
    }
    out(delimiter);
    for (char c : text(value)) {
      out(c);
      if (c == delimiter)
        out(c);
    }
    out(delimiter);
  }
  constexpr auto same(Text a, Text b) const -> bool {
    return same_name(text(a), text(b));
  }
  constexpr void qualifier(Text value, const Scope *scope, Id id) {
    if (!preparing_ || !value.length)
      return;
    for (auto s = scope; s; s = s->parent)
      for (unsigned i = 0; i < s->count; ++i)
        if (same(value, s->sources[i].name))
          return;
    fail(ErrorKind::UnknownQualifier, id, text(value));
  }
  constexpr auto cte_known(Text value, const CteScope *scope) const -> bool {
    for (auto s = scope; s; s = s->parent)
      for (unsigned i = 0; i < s->count; ++i)
        if (same(value, s->names[i]))
          return true;
    return false;
  }
  constexpr void register_source(Id id, Scope &scope, bool nullable = false) {
    if (!preparing_)
      return;
    if (!id) {
      fail(ErrorKind::InvalidShape, id, "source");
      return;
    }
    auto n = node(id);
    Text name =
        n.kind == Kind::Table && n.qualifier.length ? n.qualifier : n.name;
    if (n.kind == Kind::Alias) {
      auto k = node(n.a).kind;
      if ((k != Kind::Table && k != Kind::Cte) || node(n.a).qualifier.length) {
        fail(ErrorKind::InvalidShape, id, "source alias");
        return;
      }
    } else if (n.kind != Kind::Table && n.kind != Kind::Cte &&
               n.kind != Kind::Derived) {
      fail(ErrorKind::InvalidShape, id, "source");
      return;
    }
    for (unsigned i = 0; i < scope.count; ++i)
      if (same(name, scope.sources[i].name)) {
        fail(ErrorKind::DuplicateName, id, text(name));
        return;
      }
    if (scope.count == scope.sources.size()) {
      fail(ErrorKind::Capacity, id, "sources");
      return;
    }
    const auto &base = n.kind == Kind::Alias ? node(n.a) : n;
    scope.sources[scope.count++] = {name, id, base.table,
                                    bool(base.registration), nullable};
  }
  constexpr auto column(Id id, const Scope *scope, bool target,
                        bool alias_first = false) -> Fact {
    const auto &n = node(id);
    auto from = [&](const Source &source) -> Fact {
      if (n.source && n.source != source.instance) {
        fail(ErrorKind::InvalidSource, id, text(n.name));
        return {};
      }
      if (!source.known)
        return {};
      unsigned column_id = n.column;
      if (n.registration) {
        if (source.table != n.table) {
          fail(ErrorKind::InvalidSource, id, text(n.name));
          return {};
        }
      } else {
        auto resolved =
            builder_.schemas_.find_column(source.table, text(n.name));
        if (!resolved) {
          fail(ErrorKind::UnknownColumn, id, text(n.name));
          return {};
        }
        column_id = *resolved;
      }
      auto type = builder_.schemas_.column(column_id).type;
      type.nullable = type.nullable || source.nullable;
      return {FactKind::Known, type};
    };
    if (target) {
      if (!scope || !scope->write_target) {
        fail(ErrorKind::InvalidShape, id, "write target");
        return {};
      }
      if (n.qualifier.length && !n.source) {
        fail(ErrorKind::InvalidShape, id, "unqualified column");
        return {};
      }
      return from(scope->sources[0]);
    }
    if (n.qualifier.length) {
      for (auto s = scope; s; s = s->parent)
        for (unsigned i = 0; i < s->count; ++i)
          if (same(n.qualifier, s->sources[i].name))
            return from(s->sources[i]);
      fail(n.source ? ErrorKind::InvalidSource : ErrorKind::UnknownQualifier,
           id, text(n.qualifier));
      return {};
    }
    auto alias = [&](const Scope *s) -> std::optional<Fact> {
      if (!s || !s->ordering || !s->projection)
        return {};
      unsigned index = 0;
      for (Id l = s->projection->list; l; l = node(l).b, ++index) {
        const auto &projected = node(node(l).a);
        if (projected.kind == Kind::Alias && same(n.name, projected.name))
          return s->projection->values[index];
      }
      return {};
    };
    if (alias_first)
      if (auto found = alias(scope))
        return *found;
    bool has_sources = false;
    for (auto s = scope; s; s = s->parent) {
      has_sources = has_sources || s->count;
      unsigned matches = 0;
      bool unknown = false;
      Source found;
      for (unsigned i = 0; i < s->count; ++i) {
        auto source = s->sources[i];
        if (!source.known) {
          unknown = true;
          continue;
        }
        if (builder_.schemas_.find_column(source.table, text(n.name))) {
          ++matches;
          found = source;
        }
      }
      if (matches > 1) {
        fail(ErrorKind::AmbiguousColumn, id, text(n.name));
        return {};
      }
      if (unknown)
        return {};
      if (matches)
        return from(found);
      if (auto projected = alias(s))
        return *projected;
    }
    if (has_sources)
      fail(ErrorKind::UnknownColumn, id, text(n.name));
    return {};
  }
  constexpr void compatible(Fact a, Fact b, Id id) {
    if (a.kind == FactKind::Known && b.kind == FactKind::Known &&
        !comparable(a.type.kind, b.type.kind))
      fail(ErrorKind::TypeMismatch, id, "comparison");
  }
  constexpr auto nullable(Fact f) const -> bool {
    return f.kind == FactKind::NullLiteral ||
           (f.kind == FactKind::Known && f.type.nullable);
  }
  constexpr auto boolean(Fact a, Fact b = {FactKind::Known,
                                           {ValueKind::Boolean, false}}) const
      -> Fact {
    if (a.kind == FactKind::Unknown || b.kind == FactKind::Unknown)
      return {};
    return {FactKind::Known, {ValueKind::Boolean, nullable(a) || nullable(b)}};
  }
  constexpr void assignment(Fact target, Fact value, Id id) {
    if (target.kind != FactKind::Known || value.kind == FactKind::Unknown)
      return;
    if (value.kind == FactKind::Known &&
        !assignable(target.type.kind, value.type.kind))
      fail(ErrorKind::TypeMismatch, id, "assignment");
    if (!target.type.nullable && nullable(value))
      fail(ErrorKind::NullableAssignment, id, "assignment");
  }
  constexpr auto count(Id list) const -> unsigned {
    unsigned n = 0;
    for (; list; list = node(list).b)
      ++n;
    return n;
  }
  constexpr void parameter(Id id, bool group = false) {
    auto n = node(id);
    if (preparing_) {
      if (group && !expanded_mode_) {
        fail(ErrorKind::InvalidShape, id,
             "parameter group requires runtime expansion");
        return;
      }
      if (unsigned(n.type.kind) > unsigned(ParameterKind::Blob)) {
        fail(ErrorKind::UnsupportedType, id, text(n.name));
        return;
      }
      auto name = text(n.name);
      unsigned index = 0;
      for (; index < result_.parameter_count_; ++index)
        if (result_.parameter_name(index) == name)
          break;
      unsigned amount = group ? unsigned(n.integer) : 1;
      if (index < result_.parameter_count_) {
        if (result_.parameters_[index].type != n.type ||
            result_.member_identities_[index] != n.member_identity ||
            groups_[index] != group ||
            (group && group_counts_[index] != amount))
          fail(ErrorKind::ParameterConflict, id, name);
      } else {
        if (index >= builder_.limits_.parameters ||
            name.size() > result_.names_.size() - result_.name_size_) {
          fail(ErrorKind::Capacity, id, "parameters");
          return;
        }
        if (expanded_mode_ &&
            (next_slot_ > expansion_limits_.parameters + 1ULL ||
             amount > expansion_limits_.parameters + 1ULL - next_slot_ ||
             amount > 2147483648ULL - next_slot_)) {
          fail(ErrorKind::Capacity, id, "parameters");
          return;
        }
        unsigned slot = amount ? next_slot_ : 0;
        result_.parameters_[index] = {slot, n.type, result_.name_size_,
                                      unsigned(name.size())};
        result_.member_identities_[index] = n.member_identity;
        groups_[index] = group;
        group_counts_[index] = amount;
        next_slot_ += amount;
        for (char c : name)
          result_.names_[result_.name_size_++] = c;
        ++result_.parameter_count_;
      }
      prepared_.parameter_slots[id] = result_.parameters_[index].slot;
    } else if (group) {
      unsigned first = prepared_.parameter_slots[id];
      for (unsigned i = 0;
           i < unsigned(n.integer) && error_.kind == ErrorKind::None; ++i) {
        if (i)
          out(", ");
        out('?');
        number(static_cast<long long>(first) + i);
      }
    } else {
      out('?');
      number(prepared_.parameter_slots[id]);
    }
  }
  enum class Use { Value, Projection, Ordering, Assignment, Column };
  constexpr void expressions(Id list, const Scope *scope, const CteScope *ctes,
                             unsigned depth, Use use = Use::Value,
                             ProjectionFacts *projection = nullptr) {
    bool first = true;
    unsigned index = 0;
    for (; list && error_.kind == ErrorKind::None; list = node(list).b) {
      if (!first)
        out(", ");
      first = false;
      auto fact = expression(node(list).a, scope, ctes, depth + 1, use);
      if (projection)
        projection->values[index++] = fact;
    }
  }
  constexpr void scalar_query(Id id, const Scope *scope, const CteScope *ctes,
                              unsigned depth, bool one) {
    const auto &q = builder_.queries_[id];
    if (preparing_ &&
        (q.kind != StatementKind::Select ||
         (one && (count(q.get(Slot::Projection)) != 1 ||
                  node(node(q.get(Slot::Projection)).a).kind == Kind::Star)))) {
      fail(ErrorKind::InvalidShape, id, "subquery projection");
      return;
    }
    out('(');
    query(id, scope, ctes, depth + 1);
    out(')');
  }
  constexpr auto expression(Id id, const Scope *scope, const CteScope *ctes,
                            unsigned depth, Use use = Use::Value) -> Fact {
    if (!enter(depth, id))
      return {};
    if (!id) {
      fail(ErrorKind::InvalidHandle);
      return {};
    }
    const auto &n = node(id);
    unsigned occurrence = 0;
    if (preparing_) {
      if (prepared_.count == prepared_.facts.size()) {
        fail(ErrorKind::Capacity, id, "expression uses");
        return {};
      }
      occurrence = prepared_.count++;
    } else
      occurrence = fact_cursor_++;
    Fact fact;
    if (preparing_ && use == Use::Column &&
        (n.kind != Kind::Column || (n.qualifier.length && !n.source))) {
      fail(ErrorKind::InvalidShape, id, "unqualified column");
      return {};
    }
    if (preparing_ && use == Use::Assignment && n.kind != Kind::Assignment) {
      fail(ErrorKind::InvalidShape, id, "assignment");
      return {};
    }
    switch (n.kind) {
    case Kind::Column:
      if (preparing_)
        fact = column(id, scope, use == Use::Column, use == Use::Ordering);
      if (n.qualifier.length && use != Use::Column) {
        quote(n.qualifier);
        out('.');
      }
      quote(n.name);
      break;
    case Kind::Excluded:
      if (preparing_) {
        if (!scope || !scope->conflict_update || !scope->write_target || !n.a ||
            node(n.a).kind != Kind::Column) {
          fail(ErrorKind::InvalidShape, id, "excluded scope");
          return {};
        }
        // SQLite resolves a real target named excluded before its proposed-row
        // pseudo-table. Write aliases are not supported by this API yet.
        if (same_name(text(node(scope->write_target).name), "excluded")) {
          fail(ErrorKind::InvalidShape, id, "excluded target name");
          return {};
        }
        fact = column(n.a, scope, true);
      }
      out("\"excluded\".");
      quote(node(n.a).name);
      break;
    case Kind::Star:
      if (preparing_ && use != Use::Projection) {
        fail(ErrorKind::InvalidShape, id, "star");
        return {};
      }
      qualifier(n.qualifier, scope, id);
      if (n.qualifier.length) {
        quote(n.qualifier);
        out('.');
      }
      out('*');
      break;
    case Kind::Integer:
      fact = {FactKind::Known, {ValueKind::Integer, false}};
      number(n.integer);
      break;
    case Kind::Text:
      fact = {FactKind::Known, {ValueKind::Text, false}};
      quote(n.name, '\'');
      break;
    case Kind::Null:
      fact = {FactKind::NullLiteral, {}};
      out("NULL");
      break;
    case Kind::Parameter:
      fact = {FactKind::Known, n.type};
      parameter(id);
      break;
    case Kind::Binary: {
      constexpr std::string_view ops[] = {
          " = ", " <> ", " < ", " <= ",  " > ",  " >= ",  " + ",
          " - ", " * ",  " / ", " AND ", " OR ", " LIKE "};
      if (n.operation >= std::size(ops)) {
        fail(ErrorKind::InvalidShape, id, "operator");
        return {};
      }
      out('(');
      auto a = expression(n.a, scope, ctes, depth + 1);
      out(ops[n.operation]);
      auto b = expression(n.b, scope, ctes, depth + 1);
      if (preparing_) {
        auto op = Binary(n.operation);
        if (op <= Binary::GreaterEqual) {
          compatible(a, b, id);
          fact = boolean(a, b);
        } else if (op == Binary::Like) {
          if ((a.kind == FactKind::Known && a.type.kind != ValueKind::Text) ||
              (b.kind == FactKind::Known && b.type.kind != ValueKind::Text))
            fail(ErrorKind::TypeMismatch, id, "LIKE");
          fact = boolean(a, b);
        } else if (op >= Binary::And)
          fact = boolean(a, b);
        else {
          if ((a.kind == FactKind::Known && !numeric(a.type.kind)) ||
              (b.kind == FactKind::Known && !numeric(b.type.kind)))
            fail(ErrorKind::TypeMismatch, id, "arithmetic");
          if (a.kind == FactKind::NullLiteral ||
              b.kind == FactKind::NullLiteral)
            fact = {FactKind::NullLiteral, {}};
          else if (a.kind == FactKind::Known && b.kind == FactKind::Known)
            fact = {FactKind::Known,
                    {a.type.kind == ValueKind::Real ||
                             b.type.kind == ValueKind::Real
                         ? ValueKind::Real
                         : ValueKind::Integer,
                     nullable(a) || nullable(b) || op == Binary::Divide}};
        }
      }
      out(')');
      break;
    }
    case Kind::Not:
    case Kind::IsNull:
    case Kind::IsNotNull: {
      out('(');
      if (n.kind == Kind::Not)
        out("NOT ");
      auto operand = expression(n.a, scope, ctes, depth + 1);
      fact = n.kind == Kind::Not
                 ? boolean(operand)
                 : Fact{FactKind::Known, {ValueKind::Boolean, false}};
      if (n.kind == Kind::IsNull)
        out(" IS NULL");
      if (n.kind == Kind::IsNotNull)
        out(" IS NOT NULL");
      out(')');
      break;
    }
    case Kind::Alias:
      if (preparing_ && use != Use::Projection) {
        fail(ErrorKind::InvalidShape, id, "projection alias");
        return {};
      }
      fact = expression(n.a, scope, ctes, depth + 1);
      out(" AS ");
      quote(n.name);
      break;
    case Kind::Order:
      if (preparing_ && use != Use::Ordering) {
        fail(ErrorKind::InvalidShape, id, "ordering");
        return {};
      }
      fact = expression(n.a, scope, ctes, depth + 1, Use::Ordering);
      out(n.flag ? " DESC" : " ASC");
      break;
    case Kind::Assignment: {
      if (preparing_ && use != Use::Assignment) {
        fail(ErrorKind::InvalidShape, id, "assignment");
        return {};
      }
      auto target = expression(n.a, scope, ctes, depth + 1, Use::Column);
      out(" = ");
      auto value = expression(n.b, scope, ctes, depth + 1);
      if (preparing_)
        assignment(target, value, id);
      break;
    }
    case Kind::In: {
      if (preparing_ && !n.b) {
        fail(ErrorKind::InvalidShape, id, "empty IN");
        return {};
      }
      out('(');
      auto a = expression(n.a, scope, ctes, depth + 1);
      out(" IN (");
      fact = boolean(a);
      for (Id l = n.b; l; l = node(l).b) {
        if (l != n.b)
          out(", ");
        auto b = expression(node(l).a, scope, ctes, depth + 2);
        if (preparing_) {
          compatible(a, b, id);
          if (b.kind == FactKind::Unknown)
            fact = {};
          else if (nullable(b))
            fact.type.nullable = true;
        }
      }
      out("))");
      break;
    }
    case Kind::InGroup: {
      const auto &group = node(n.b);
      if (!n.b || group.kind != Kind::ParameterGroup) {
        fail(ErrorKind::InvalidShape, id, "parameter group");
        return {};
      }
      out('(');
      auto a = expression(n.a, scope, ctes, depth + 1);
      out(" IN (");
      parameter(n.b, true);
      if (preparing_) {
        Fact b{FactKind::Known, group.type};
        compatible(a, b, id);
        fact = group.integer == 0
                   ? Fact{FactKind::Known, {ValueKind::Boolean, false}}
                   : boolean(a, b);
      }
      out("))");
      break;
    }
    case Kind::InQuery:
      out('(');
      expression(n.a, scope, ctes, depth + 1);
      out(" IN ");
      scalar_query(n.b, scope, ctes, depth + 1, true);
      out(')');
      break;
    case Kind::Scalar:
      scalar_query(n.a, scope, ctes, depth + 1, true);
      break;
    case Kind::Exists:
      fact = {FactKind::Known, {ValueKind::Boolean, false}};
      out("EXISTS ");
      scalar_query(n.a, scope, ctes, depth + 1, false);
      break;
    case Kind::RowNumber: {
      if (preparing_ && (!scope || !scope->window_projection || !n.a)) {
        fail(ErrorKind::InvalidShape, id, "window projection");
        return {};
      }
      fact = {FactKind::Known, {ValueKind::Integer, false}};
      out("ROW_NUMBER() OVER (ORDER BY ");
      Scope ordering_scope = scope ? *scope : Scope{};
      ordering_scope.window_projection = false;
      ordering_scope.ordering = false;
      ordering_scope.projection = nullptr;
      expressions(n.a, &ordering_scope, ctes, depth + 1, Use::Ordering);
      out(')');
      break;
    }
    case Kind::Function: {
      constexpr std::string_view names[] = {
          "COUNT", "SUM",   "AVG",    "MIN",      "MAX",
          "LOWER", "UPPER", "LENGTH", "COALESCE", "ABS"};
      unsigned arity = count(n.a);
      if (n.operation >= std::size(names) ||
          (n.operation == unsigned(Function::Coalesce) ? arity < 2
                                                       : arity != 1)) {
        fail(ErrorKind::InvalidShape, id, "function arity");
        return {};
      }
      out(names[n.operation]);
      out('(');
      if (n.operation == unsigned(Function::Count) &&
          node(node(n.a).a).kind == Kind::Star) {
        if (preparing_ && node(node(n.a).a).qualifier.length) {
          fail(ErrorKind::InvalidShape, id, "COUNT star");
          return {};
        }
        out('*');
        fact = {FactKind::Known, {ValueKind::Integer, false}};
      } else {
        auto f = Function(n.operation);
        bool aggregate =
            f == Function::Count || f == Function::Sum || f == Function::Avg ||
            ((f == Function::Min || f == Function::Max) && arity == 1);
        std::optional<Scope> aggregate_scope;
        const Scope *argument_scope = scope;
        if (aggregate && scope) {
          aggregate_scope.emplace(*scope);
          aggregate_scope->window_projection = false;
          argument_scope = &*aggregate_scope;
        }
        bool first = true;
        Fact argument;
        bool coalesce_known = true, coalesce_nonnull = false;
        bool only_null = true;
        for (Id l = n.a; l; l = node(l).b) {
          if (!first)
            out(", ");
          auto v = expression(node(l).a, argument_scope, ctes, depth + 2);
          if (first)
            argument = v;
          if (Function(n.operation) == Function::Coalesce) {
            if (v.kind == FactKind::Unknown)
              coalesce_known = false;
            if (v.kind == FactKind::Known) {
              if (!v.type.nullable)
                coalesce_nonnull = true;
              if (only_null) {
                argument = v;
                only_null = false;
              } else if (argument.kind == FactKind::Known &&
                         !comparable(argument.type.kind, v.type.kind))
                coalesce_known = false;
              else if (argument.type.kind == ValueKind::Integer &&
                       v.type.kind == ValueKind::Real)
                argument.type.kind = ValueKind::Real;
            }
          }
          first = false;
        }
        if (f == Function::Count)
          fact = {FactKind::Known, {ValueKind::Integer, false}};
        else if (f == Function::Coalesce) {
          if (coalesce_known) {
            fact = only_null ? Fact{FactKind::NullLiteral, {}} : argument;
            fact.type.nullable = !coalesce_nonnull;
          }
        } else if (f == Function::Min || f == Function::Max) {
          fact = argument;
          fact.type.nullable = true;
        } else if (f == Function::Lower || f == Function::Upper) {
          if (argument.kind == FactKind::Known &&
              argument.type.kind == ValueKind::Text)
            fact = argument;
        } else if (f == Function::Length) {
          if (argument.kind == FactKind::Known &&
              (argument.type.kind == ValueKind::Text ||
               argument.type.kind == ValueKind::Blob))
            fact = {FactKind::Known, {ValueKind::Integer, nullable(argument)}};
        } else if (argument.kind == FactKind::Known &&
                   numeric(argument.type.kind)) {
          fact = argument;
          if (f == Function::Avg) {
            fact.type.kind = ValueKind::Real;
            fact.type.nullable = true;
          }
          if (f == Function::Sum) {
            fact = {};
          }
        }
      }
      out(')');
      break;
    }
    default:
      fail(ErrorKind::InvalidShape, id, "expression");
      break;
    }
    if (preparing_)
      prepared_.facts[occurrence] = fact;
    return preparing_ ? fact : prepared_.facts[occurrence];
  }
  constexpr void source(Id id, const CteScope *ctes, unsigned depth) {
    if (!enter(depth, id))
      return;
    auto n = node(id);
    switch (n.kind) {
    case Kind::Table:
      if (preparing_) {
        for (auto s = ctes; s; s = s->parent)
          for (unsigned i = 0; i < s->reserved_count; ++i)
            if (same(n.name, s->reserved[i])) {
              fail(ErrorKind::UnknownCte, id, "CTE reference requires cte()");
              return;
            }
      }
      quote(n.name);
      if (n.qualifier.length) {
        out(" AS ");
        quote(n.qualifier);
      }
      break;
    case Kind::Cte:
      if (preparing_ && !cte_known(n.name, ctes)) {
        fail(ErrorKind::UnknownCte, id, text(n.name));
        return;
      }
      quote(n.name);
      break;
    case Kind::Derived:
      scalar_query(n.a, nullptr, ctes, depth + 1, false);
      out(" AS ");
      quote(n.name);
      break;
    case Kind::Alias:
      source(n.a, ctes, depth + 1);
      out(" AS ");
      quote(n.name);
      break;
    default:
      fail(ErrorKind::InvalidShape, id, "source");
      break;
    }
  }
  constexpr void unique_columns(Id list, bool assignments) {
    if (!preparing_)
      return;
    for (Id a = list; a; a = node(a).b) {
      Id ai = node(a).a;
      if (assignments)
        ai = node(ai).a;
      if (node(ai).kind != Kind::Column ||
          (node(ai).qualifier.length && !node(ai).source)) {
        fail(ErrorKind::InvalidShape, ai, "column");
        return;
      }
      for (Id b = node(a).b; b; b = node(b).b) {
        Id bi = node(b).a;
        if (assignments)
          bi = node(bi).a;
        const auto &x = node(ai);
        const auto &y = node(bi);
        bool duplicate = x.registration && y.registration
                             ? x.source == y.source && x.column == y.column
                             : same(x.name, y.name);
        if (duplicate) {
          fail(ErrorKind::DuplicateName, bi, text(node(ai).name));
          return;
        }
      }
    }
  }
  constexpr void group_values(Id id, Id columns, const Scope *scope,
                              unsigned depth) {
    if (!enter(depth, id))
      return;
    auto groups = node(id).a;
    if (preparing_) {
      // One validation per column, not per expanded cell: the AST and facts
      // remain bounded independently of the number of VALUES rows.
      for (Id l = groups; l && error_.kind == ErrorKind::None;
           l = node(l).b, columns = node(columns).b) {
        auto group_id = node(l).a;
        const auto &group = node(group_id);
        parameter(group_id, true);
        assignment(column(node(columns).a, scope, true),
                   Fact{FactKind::Known, group.type}, group_id);
      }
      return;
    }
    auto rows = unsigned(node(node(groups).a).integer);
    for (unsigned row = 0; row < rows && error_.kind == ErrorKind::None;
         ++row) {
      if (row)
        out(", ");
      out('(');
      for (Id l = groups; l && error_.kind == ErrorKind::None; l = node(l).b) {
        if (l != groups)
          out(", ");
        out('?');
        number(static_cast<long long>(prepared_.parameter_slots[node(l).a]) +
               row);
      }
      out(')');
    }
  }
  constexpr void shape(const QueryData &q, Id id) {
    if (!preparing_)
      return;
    auto get = [&](Slot s) { return q.get(s); };
    if (q.kind == StatementKind::Select) {
      if (!get(Slot::Projection) || get(Slot::Values) || get(Slot::Sets) ||
          get(Slot::Conflict) || get(Slot::ConflictSets) ||
          get(Slot::Returning) || q.allow_all || q.default_values ||
          (get(Slot::Joins) && !get(Slot::Source)) ||
          (get(Slot::Having) && !get(Slot::Group)) ||
          (get(Slot::Offset) && !get(Slot::Limit)))
        fail(ErrorKind::InvalidShape, id, "SELECT");
    } else {
      if (!get(Slot::Source) || node(get(Slot::Source)).kind != Kind::Table ||
          node(get(Slot::Source)).qualifier.length || q.distinct ||
          get(Slot::Group) || get(Slot::Having) || get(Slot::Order) ||
          get(Slot::Limit) || get(Slot::Offset) || get(Slot::Joins))
        fail(ErrorKind::InvalidShape, id, "write clauses");
      if (q.kind == StatementKind::Insert) {
        auto values = get(Slot::Values);
        bool grouped = values && node(values).kind == Kind::ValuesGroups;
        auto inputs = grouped ? node(values).a : values;
        if ((q.default_values &&
             (get(Slot::Projection) || values || get(Slot::Conflict) ||
              get(Slot::ConflictSets))) ||
            (!q.default_values &&
             (!get(Slot::Projection) || !inputs ||
              count(get(Slot::Projection)) != count(inputs))) ||
            (get(Slot::ConflictSets) && !get(Slot::Conflict)) ||
            get(Slot::Where) || get(Slot::Sets) || q.allow_all)
          fail(ErrorKind::InvalidShape, id, "INSERT");
        if (grouped) {
          long long rows = 0;
          for (Id l = inputs; l && error_.kind == ErrorKind::None;
               l = node(l).b) {
            const auto &group = node(node(l).a);
            if (group.kind != Kind::ParameterGroup || group.integer <= 0 ||
                (rows && rows != group.integer)) {
              fail(ErrorKind::InvalidShape, id, "VALUES groups");
              break;
            }
            rows = group.integer;
          }
        }
        unique_columns(get(Slot::Projection), false);
        if (get(Slot::Conflict))
          unique_columns(get(Slot::Conflict), false);
        if (get(Slot::ConflictSets))
          unique_columns(get(Slot::ConflictSets), true);
      } else {
        if (q.default_values || get(Slot::Projection) || get(Slot::Values) ||
            get(Slot::Conflict) || get(Slot::ConflictSets) ||
            (q.kind == StatementKind::Delete && get(Slot::Returning)) ||
            (q.kind == StatementKind::Update ? !get(Slot::Sets)
                                             : bool(get(Slot::Sets))))
          fail(ErrorKind::InvalidShape, id, "UPDATE/DELETE");
        if (!get(Slot::Where) && !q.allow_all)
          fail(ErrorKind::UnsafeWrite, id);
        if (get(Slot::Sets))
          unique_columns(get(Slot::Sets), true);
      }
    }
  }
  constexpr void query(Id id, const Scope *parent, const CteScope *outer_ctes,
                       unsigned depth) {
    if (!enter(depth, id))
      return;
    const auto &q = builder_.queries_[id];
    shape(q, id);
    if (error_.kind != ErrorKind::None)
      return;
    CteScope ctes{.parent = outer_ctes};
    for (Id l = q.get(Slot::Ctes); l; l = node(l).b) {
      if (ctes.reserved_count == ctes.reserved.size()) {
        fail(ErrorKind::Capacity, id, "CTEs");
        return;
      }
      ctes.reserved[ctes.reserved_count++] = node(node(l).a).name;
    }
    if (auto list = q.get(Slot::Ctes)) {
      out("WITH ");
      bool first = true;
      for (; list; list = node(list).b) {
        auto n = node(node(list).a);
        for (unsigned i = 0; i < ctes.count; ++i)
          if (same(n.name, ctes.names[i]))
            fail(ErrorKind::DuplicateName, id, text(n.name));
        if (!first)
          out(", ");
        first = false;
        quote(n.name);
        out(" AS ");
        scalar_query(n.a, nullptr, &ctes, depth + 1, false);
        if (ctes.count >= ctes.names.size()) {
          fail(ErrorKind::Capacity, id, "CTEs");
          return;
        }
        ctes.names[ctes.count++] = n.name;
      }
      out(' ');
    }
    std::optional<ProjectionFacts> projection;
    if (preparing_ && q.kind == StatementKind::Select)
      for (Id l = q.get(Slot::Projection); l; l = node(l).b)
        if (node(node(l).a).kind == Kind::Alias) {
          projection.emplace();
          projection->list = q.get(Slot::Projection);
          break;
        }
    Scope scope{.parent = parent};
    if (projection)
      scope.projection = &*projection;
    if (q.get(Slot::Source))
      register_source(q.get(Slot::Source), scope);
    for (Id l = q.get(Slot::Joins); l; l = node(l).b)
      register_source(node(node(l).a).a, scope, node(node(l).a).flag);
    if (q.kind != StatementKind::Select)
      scope.write_target = q.get(Slot::Source);
    if (error_.kind != ErrorKind::None)
      return;
    if (q.kind == StatementKind::Select) {
      out(q.distinct ? "SELECT DISTINCT " : "SELECT ");
      Scope projection_scope = scope;
      projection_scope.window_projection = true;
      expressions(q.get(Slot::Projection), &projection_scope, &ctes, depth + 1,
                  Use::Projection, projection ? &*projection : nullptr);
      if (q.get(Slot::Source)) {
        out(" FROM ");
        source(q.get(Slot::Source), &ctes, depth + 1);
      }
      Scope join_scope{.parent = parent};
      if (q.get(Slot::Source))
        register_source(q.get(Slot::Source), join_scope);
      for (Id l = q.get(Slot::Joins); l; l = node(l).b) {
        const auto &n = node(node(l).a);
        register_source(n.a, join_scope);
        out(n.flag ? " LEFT JOIN " : " INNER JOIN ");
        source(n.a, &ctes, depth + 1);
        out(" ON ");
        expression(n.b, &join_scope, &ctes, depth + 1);
        if (n.flag && join_scope.count)
          join_scope.sources[join_scope.count - 1].nullable = true;
      }
    } else if (q.kind == StatementKind::Insert) {
      out("INSERT INTO ");
      source(q.get(Slot::Source), &ctes, depth + 1);
      if (q.default_values) {
        out(" DEFAULT VALUES");
      } else {
        out(" (");
        expressions(q.get(Slot::Projection), &scope, &ctes, depth + 1,
                    Use::Column);
        out(") VALUES ");
        if (node(q.get(Slot::Values)).kind == Kind::ValuesGroups) {
          group_values(q.get(Slot::Values), q.get(Slot::Projection), &scope,
                       depth + 1);
        } else {
          out('(');
          Id column_list = q.get(Slot::Projection);
          for (Id l = q.get(Slot::Values); l; l = node(l).b) {
            if (l != q.get(Slot::Values))
              out(", ");
            auto value = expression(node(l).a, nullptr, &ctes, depth + 2);
            if (preparing_)
              assignment(column(node(column_list).a, &scope, true), value,
                         node(l).a);
            column_list = node(column_list).b;
          }
          out(')');
        }
      }
      if (q.get(Slot::Conflict)) {
        out(" ON CONFLICT (");
        expressions(q.get(Slot::Conflict), &scope, &ctes, depth + 1,
                    Use::Column);
        if (q.get(Slot::ConflictSets)) {
          out(") DO UPDATE SET ");
          Scope conflict_scope = scope;
          conflict_scope.conflict_update = true;
          expressions(q.get(Slot::ConflictSets), &conflict_scope, &ctes,
                      depth + 1, Use::Assignment);
        } else {
          out(") DO NOTHING");
        }
      }
    } else if (q.kind == StatementKind::Update) {
      out("UPDATE ");
      source(q.get(Slot::Source), &ctes, depth + 1);
      out(" SET ");
      expressions(q.get(Slot::Sets), &scope, &ctes, depth + 1, Use::Assignment);
    } else {
      out("DELETE FROM ");
      source(q.get(Slot::Source), &ctes, depth + 1);
    }
    if (q.get(Slot::Where)) {
      out(" WHERE ");
      expression(q.get(Slot::Where), &scope, &ctes, depth + 1);
    }
    if (q.get(Slot::Group)) {
      out(" GROUP BY ");
      expressions(q.get(Slot::Group), &scope, &ctes, depth + 1);
    }
    if (q.get(Slot::Having)) {
      out(" HAVING ");
      expression(q.get(Slot::Having), &scope, &ctes, depth + 1);
    }
    if (q.get(Slot::Order)) {
      out(" ORDER BY ");
      scope.ordering = true;
      expressions(q.get(Slot::Order), &scope, &ctes, depth + 1, Use::Ordering);
    }
    if (q.get(Slot::Returning)) {
      out(" RETURNING ");
      expressions(q.get(Slot::Returning), &scope, &ctes, depth + 1,
                  Use::Column);
    }
    if (q.get(Slot::Limit)) {
      out(" LIMIT ");
      expression(q.get(Slot::Limit), nullptr, &ctes, depth + 1);
    }
    if (q.get(Slot::Offset)) {
      out(" OFFSET ");
      expression(q.get(Slot::Offset), nullptr, &ctes, depth + 1);
    }
  }

  void prepare_and_measure(Id id) {
    query(id, nullptr, nullptr, 0);
    if (error_.kind != ErrorKind::None)
      return;
    preparing_ = false;
    measuring_ = true;
    fact_cursor_ = 0;
    measured_sql_ = 0;
    query(id, nullptr, nullptr, 0);
    measuring_ = false;
    if (error_.kind == ErrorKind::None && fact_cursor_ != prepared_.count)
      fail(ErrorKind::InvalidShape, id, "expression uses");
  }

public:
  constexpr explicit Compiler(const QueryBuilder &b) : builder_(b) {}
  explicit Compiler(const QueryBuilder &b, ExpansionLimits limits)
      : builder_(b), expanded_mode_(true), expansion_limits_(limits) {}
  explicit Compiler(const QueryBuilder &b, ExpandedQuery &out,
                    ExpansionLimits limits)
      : builder_(b), expanded_mode_(true), expanded_(&out),
        expansion_limits_(limits) {}
  static auto run_expanded(Query value, ExpansionLimits limits)
      -> rstd::Result<ExpandedQuery, Error> {
    if (!value.owner_ || !value.id_)
      return rstd::Err(Error{.kind = ErrorKind::InvalidHandle});
    ExpandedQuery result;
    Compiler c(*value.owner_, result, limits);
    if (value.owner_->error_.kind != ErrorKind::None)
      return rstd::Err(Error(value.owner_->error_));
    c.query(value.id_, nullptr, nullptr, 0);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    c.preparing_ = false;
    c.query(value.id_, nullptr, nullptr, 0);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    auto valid = rstd::str_::from_utf8(result.sql().as_bytes());
    if (valid.is_err())
      return rstd::Err(Error{.kind = ErrorKind::InvalidName});
    result.metadata_ = c.result_;
    result.groups_ = c.groups_;
    result.counts_ = c.group_counts_;
    return rstd::Ok(rstd::move(result));
  }

  static auto plan_expansion(Query value, ExpansionLimits ceiling)
      -> rstd::Result<ExpansionPlan, Error> {
    if (!value.owner_ || !value.id_)
      return rstd::Err(Error{.kind = ErrorKind::InvalidHandle});
    Compiler c(*value.owner_, ceiling);
    if (value.owner_->error_.kind != ErrorKind::None)
      return rstd::Err(Error(value.owner_->error_));
    c.prepare_and_measure(value.id_);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    return rstd::Ok(ExpansionPlan{c.next_slot_ - 1, c.measured_sql_});
  }

  static auto run_expanded_planned(Query value, ExpansionLimits ceiling)
      -> rstd::Result<ExpandedQuery, Error> {
    if (!value.owner_ || !value.id_)
      return rstd::Err(Error{.kind = ErrorKind::InvalidHandle});
    Compiler c(*value.owner_, ceiling);
    if (value.owner_->error_.kind != ErrorKind::None)
      return rstd::Err(Error(value.owner_->error_));
    c.prepare_and_measure(value.id_);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    ExpansionPlan plan{c.next_slot_ - 1, c.measured_sql_};
    ExpandedQuery result;
    result.sql_.reserve(plan.sql);
    c.expanded_ = &result;
    c.expansion_limits_ = {plan.parameters, plan.sql};
    c.fact_cursor_ = 0;
    c.query(value.id_, nullptr, nullptr, 0);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    if (result.sql_.size() != plan.sql || c.next_slot_ - 1 != plan.parameters ||
        c.fact_cursor_ != c.prepared_.count) {
      c.fail(ErrorKind::InvalidShape, value.id_, "expansion plan");
      return rstd::Err(Error(c.error_));
    }
    auto valid = rstd::str_::from_utf8(result.sql().as_bytes());
    if (valid.is_err())
      return rstd::Err(Error{.kind = ErrorKind::InvalidName});
    result.metadata_ = c.result_;
    result.groups_ = c.groups_;
    result.counts_ = c.group_counts_;
    return rstd::Ok(rstd::move(result));
  }

  static constexpr auto run(Query value) -> rstd::Result<CompiledQuery, Error> {
    if (!value.owner_ || !value.id_)
      return rstd::Err(Error{.kind = ErrorKind::InvalidHandle});
    Compiler c(*value.owner_);
    if (value.owner_->error_.kind != ErrorKind::None)
      return rstd::Err(Error(value.owner_->error_));
    c.query(value.id_, nullptr, nullptr, 0);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    c.preparing_ = false;
    c.query(value.id_, nullptr, nullptr, 0);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    auto valid = rstd::str_::from_utf8(c.result_.sql().as_bytes());
    if (valid.is_err())
      return rstd::Err(Error{.kind = ErrorKind::InvalidName});
    return rstd::Ok(CompiledQuery(c.result_));
  }
};
// Immediate calls deliberately stop here with a domain-specific diagnostic.
inline void invalid_query_shape(Error) { __builtin_trap(); }
inline void invalid_query_owner(Error) { __builtin_trap(); }
inline void query_capacity_exceeded(Error) { __builtin_trap(); }
inline void conflicting_query_parameter(Error) { __builtin_trap(); }
inline void invalid_query_type(Error) { __builtin_trap(); }
inline void invalid_query_source(Error) { __builtin_trap(); }
inline void invalid_query(Error) { __builtin_trap(); }
} // namespace liorm::query

export namespace liorm::query {
constexpr auto try_compile(Query query) -> rstd::Result<CompiledQuery, Error> {
  return Compiler::run(query);
}
inline auto try_compile_expanded(Query query, ExpansionLimits limits = {})
    -> rstd::Result<ExpandedQuery, Error> {
  return Compiler::run_expanded(query, limits);
}
template <class Build>
auto try_compile_expanded(Build build, ExpansionLimits expansion = {},
                          Limits limits = {})
    -> rstd::Result<ExpandedQuery, Error> {
  QueryBuilder builder(limits);
  return try_compile_expanded(build(builder), expansion);
}
inline auto
try_plan_expansion(Query query,
                   ExpansionLimits ceiling = planned_expansion_ceiling)
    -> rstd::Result<ExpansionPlan, Error> {
  return Compiler::plan_expansion(query, ceiling);
}
template <class Build>
auto try_plan_expansion(Build build,
                        ExpansionLimits ceiling = planned_expansion_ceiling,
                        Limits limits = {})
    -> rstd::Result<ExpansionPlan, Error> {
  QueryBuilder builder(limits);
  return try_plan_expansion(build(builder), ceiling);
}
inline auto try_compile_expanded_planned(
    Query query, ExpansionLimits ceiling = planned_expansion_ceiling)
    -> rstd::Result<ExpandedQuery, Error> {
  return Compiler::run_expanded_planned(query, ceiling);
}
template <class Build>
auto try_compile_expanded_planned(
    Build build, ExpansionLimits ceiling = planned_expansion_ceiling,
    Limits limits = {}) -> rstd::Result<ExpandedQuery, Error> {
  QueryBuilder builder(limits);
  return try_compile_expanded_planned(build(builder), ceiling);
}
template <class Build>
constexpr auto try_compile(Build build, Limits limits = {})
    -> rstd::Result<CompiledQuery, Error> {
  QueryBuilder builder(limits);
  return try_compile(build(builder));
}
template <class Build>
consteval auto compile(Build build, Limits limits = {}) -> CompiledQuery {
  auto result = try_compile(build, limits);
  if (result.is_err()) {
    auto error = result.unwrap_err();
    switch (error.kind) {
    case ErrorKind::InvalidShape:
      invalid_query_shape(error);
      break;
    case ErrorKind::ForeignOwner:
    case ErrorKind::InvalidHandle:
      invalid_query_owner(error);
      break;
    case ErrorKind::Capacity:
    case ErrorKind::Depth:
      query_capacity_exceeded(error);
      break;
    case ErrorKind::TypeMismatch:
    case ErrorKind::NullableAssignment:
    case ErrorKind::UnsupportedType:
      invalid_query_type(error);
      break;
    case ErrorKind::UnknownTable:
    case ErrorKind::UnknownColumn:
    case ErrorKind::AmbiguousColumn:
    case ErrorKind::InvalidSource:
    case ErrorKind::InvalidRelation:
      invalid_query_source(error);
      break;
    case ErrorKind::ParameterConflict:
      conflicting_query_parameter(error);
      break;
    default:
      invalid_query(error);
      break;
    }
  }
  return result.unwrap();
}
} // namespace liorm::query
