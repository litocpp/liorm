module;
export module liorm:sqlite;
export import :query;
import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace liorm::query {
struct Parameter {
  usize slot{};
  ParameterType type{};
  usize name_offset{}, name_length{};
};
class CompiledQuery {
  friend class Compiler;
  friend class ExpandedQuery;
  array<rstd::byte, 16384> sql_{};
  array<rstd::byte, 8192> names_{};
  array<Parameter, 64> parameters_{};
  array<const void *, 64> member_identities_{};
  usize sql_size_{}, name_size_{}, parameter_count_{};
  constexpr CompiledQuery() = default;

public:
  constexpr auto sql() const -> rstd::ref<rstd::str> {
    return rstd::ref<rstd::str>::from_raw_parts_unchecked(
        sql_.data(), rstd::usize(sql_size_));
  }
  constexpr auto parameter_count() const -> usize {
    return parameter_count_;
  }
  constexpr auto parameter(usize index) const -> rstd::Option<Parameter> {
    if (index >= parameter_count_)
      return rstd::None();
    return rstd::Some(Parameter(parameters_[index]));
  }
  constexpr auto parameter_name(usize index) const -> ref<str> {
    if (index >= parameter_count_)
      return {};
    auto p = parameters_[index];
    return ref<str>::from_raw_parts_unchecked(
        names_.data() + p.name_offset.to_primitive(), p.name_length);
  }
  constexpr auto parameter_has_member(usize index) const -> bool {
    return index < parameter_count_ && member_identities_[index] != nullptr;
  }
  template <auto Member>
  constexpr auto parameter_is(usize index) const -> bool {
    return index < parameter_count_ &&
           member_identities_[index] == &member_parameter_identity<Member>;
  }
  constexpr auto parameter_is_group(usize) const -> bool { return false; }
  constexpr auto parameter_slot(ref<str> name) const
      -> rstd::Option<rstd::usize> {
    for (usize i = 0_usize; i < parameter_count_; ++i)
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
  usize parameters = 1048576_usize;
  usize sql = 1048576_usize;
};
struct ExpansionPlan {
  // Physical slots after logical-name deduplication, and SQL bytes excluding
  // NUL.
  usize parameters{};
  usize sql{};
};
// The planned path opts into a wide policy, not a database or memory guarantee.
inline constexpr ExpansionLimits planned_expansion_ceiling{
    2147483647_usize, usize(static_cast<rstd::size_t>(-1))};
// SQL and parameter metadata are owned; views returned by sql() remain valid
// only while this object is alive and has not been moved or assigned.
class ExpandedQuery {
  friend class Compiler;
  Vec<rstd::byte> sql_;
  CompiledQuery metadata_;
  array<bool, 64> groups_{};
  array<usize, 64> counts_{};

public:
  auto sql() const -> rstd::ref<rstd::str> {
    return rstd::ref<rstd::str>::from_raw_parts_unchecked(
        sql_.data(), rstd::usize(sql_.len()));
  }
  auto parameter_count() const -> usize {
    return metadata_.parameter_count();
  }
  auto parameter(usize index) const -> rstd::Option<Parameter> {
    return metadata_.parameter(index);
  }
  auto parameter_name(usize index) const -> ref<str> {
    return metadata_.parameter_name(index);
  }
  auto parameter_has_member(usize index) const -> bool {
    return metadata_.parameter_has_member(index);
  }
  template <auto Member> auto parameter_is(usize index) const -> bool {
    return metadata_.template parameter_is<Member>(index);
  }
  auto parameter_is_group(usize index) const -> bool {
    return index < metadata_.parameter_count_ && groups_[index];
  }
  auto parameter_slot(ref<str> name) const
      -> rstd::Option<rstd::usize> {
    for (usize i = 0_usize; i < metadata_.parameter_count_; ++i)
      if (metadata_.parameter_name(i) == name && !groups_[i])
        return rstd::Some(rstd::usize(metadata_.parameters_[i].slot));
    return rstd::None();
  }
  auto parameter_group(ref<str> name) const
      -> rstd::Option<ParameterGroupSlots> {
    for (usize i = 0_usize; i < metadata_.parameter_count_; ++i)
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
  usize table{};
  bool known{}, nullable{};
};
enum class FactKind { Unknown, Known, NullLiteral };
struct Fact {
  FactKind kind{};
  ValueType type{};
};
struct ProjectionFacts {
  Id list{};
  array<Fact, 512> values{};
};
struct Scope {
  const Scope *parent{};
  array<Source, 128> sources{};
  Id write_target{};
  const ProjectionFacts *projection{};
  bool ordering{}, conflict_update{}, window_projection{};
  usize count{};
};
struct CteScope {
  const CteScope *parent{};
  array<Text, 128> names{};
  array<Text, 128> reserved{};
  usize reserved_count{};
  usize count{};
};
struct PreparedQuery {
  array<usize, 512> parameter_slots{};
  array<Fact, 4096> facts{};
  usize count{};
};
class Compiler {
  const QueryBuilder &builder_;
  PreparedQuery prepared_{};
  CompiledQuery result_{};
  Error error_{};
  bool preparing_{true};
  bool expanded_mode_{}, measuring_{};
  usize fact_cursor_{}, measured_sql_{};
  ExpandedQuery *expanded_{};
  ExpansionLimits expansion_limits_{};
  array<bool, 64> groups_{};
  array<usize, 64> group_counts_{};
  usize next_slot_{1};

  constexpr auto text(Text t) const -> ref<str> {
    return builder_.text(t);
  }
  constexpr auto node(Id id) const -> const Node & {
    return builder_.nodes_[id];
  }
  constexpr void fail(ErrorKind kind, Id id = 0_usize, ref<str> name = {}) {
    if (error_.kind != ErrorKind::None)
      return;
    error_.kind = kind;
    error_.node = id;
    error_.length = name.len() < error_.context.len() ? name.len()
                                                      : error_.context.len();
    error_.truncated = name.len() > error_.length;
    for (usize i {}; i < error_.length; ++i)
      error_.context[i] = char(name[i].to_primitive());
  }
  constexpr auto enter(usize depth, Id id) -> bool {
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
        fail(ErrorKind::Capacity, 0_usize, "sql"_str);
        return;
      }
      ++measured_sql_;
      return;
    }
    if (expanded_) {
      if (expanded_->sql_.len() >= expansion_limits_.sql) {
        fail(ErrorKind::Capacity, 0_usize, "sql"_str);
        return;
      }
      expanded_->sql_.push_back(rstd::byte{static_cast<unsigned char>(c)});
      return;
    }
    if (result_.sql_size_ >= builder_.limits_.sql) {
      fail(ErrorKind::Capacity, 0_usize, "sql"_str);
      return;
    }
    result_.sql_[result_.sql_size_++] =
        rstd::byte{static_cast<unsigned char>(c)};
  }
  constexpr void out(ref<str> value) {
    if (measuring_ && !preparing_ && error_.kind == ErrorKind::None) {
      if (value.len() > expansion_limits_.sql - measured_sql_) {
        fail(ErrorKind::Capacity, 0_usize, "sql"_str);
        return;
      }
      measured_sql_ += usize(value.len());
      return;
    }
    for (usize i {}; i < value.len(); ++i)
      out(char(value[i].to_primitive()));
  }
  constexpr void number(long long value) {
    unsigned long long magnitude = static_cast<unsigned long long>(value);
    if (value < 0) {
      out('-');
      magnitude = 0 - magnitude;
    }
    char digits[20]{};
    usize count = 0_usize;
    do {
      digits[count.to_primitive()] = char('0' + magnitude % 10);
      ++count;
      magnitude /= 10;
    } while (magnitude != 0);
    while (count != 0_usize)
      out(digits[(--count).to_primitive()]);
  }
  constexpr void quote(Text value, char delimiter = '"') {
    if (measuring_ && error_.kind == ErrorKind::None) {
      // Every non-ASCII SQL byte comes from quoted builder text. Validate it
      // before allocating the planned output, using the same UTF-8 checker.
      auto raw = text(value);
      array<rstd::byte, 8192> bytes{};
      for (usize i = 0_usize; i < raw.len(); ++i)
        bytes[i] = rstd::byte {static_cast<unsigned char>(raw[i].to_primitive())};
      if (rstd::str_::from_utf8(rstd::slice<rstd::u8>::from_raw_parts(
                                    bytes.data(), rstd::usize(raw.len())))
              .is_err()) {
        fail(ErrorKind::InvalidName);
        return;
      }
    }
    out(delimiter);
    auto quoted = text(value);
    for (usize i {}; i < quoted.len(); ++i) {
      char c = char(quoted[i].to_primitive());
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
    if (! preparing_ || value.length == 0_usize)
      return;
    for (auto s = scope; s; s = s->parent)
      for (usize i = 0_usize; i < s->count; ++i)
        if (same(value, s->sources[i].name))
          return;
    fail(ErrorKind::UnknownQualifier, id, text(value));
  }
  constexpr auto cte_known(Text value, const CteScope *scope) const -> bool {
    for (auto s = scope; s; s = s->parent)
      for (usize i = 0_usize; i < s->count; ++i)
        if (same(value, s->names[i]))
          return true;
    return false;
  }
  constexpr void register_source(Id id, Scope &scope, bool nullable = false) {
    if (!preparing_)
      return;
    if (id == 0_usize) {
      fail(ErrorKind::InvalidShape, id, "source"_str);
      return;
    }
    auto n = node(id);
    Text name =
        n.kind == Kind::Table && n.qualifier.length != 0_usize ? n.qualifier : n.name;
    if (n.kind == Kind::Alias) {
      auto k = node(n.a).kind;
      if ((k != Kind::Table && k != Kind::Cte) || node(n.a).qualifier.length != 0_usize) {
        fail(ErrorKind::InvalidShape, id, "source alias"_str);
        return;
      }
    } else if (n.kind != Kind::Table && n.kind != Kind::Cte &&
               n.kind != Kind::Derived) {
      fail(ErrorKind::InvalidShape, id, "source"_str);
      return;
    }
    for (usize i = 0_usize; i < scope.count; ++i)
      if (same(name, scope.sources[i].name)) {
        fail(ErrorKind::DuplicateName, id, text(name));
        return;
      }
    if (scope.count == scope.sources.len()) {
      fail(ErrorKind::Capacity, id, "sources"_str);
      return;
    }
    const auto &base = n.kind == Kind::Alias ? node(n.a) : n;
    scope.sources[scope.count++] = {name, id, base.table,
                                    base.registration != 0_usize, nullable};
  }
  constexpr auto column(Id id, const Scope *scope, bool target,
                        bool alias_first = false) -> Fact {
    const auto &n = node(id);
    auto from = [&](const Source &source) -> Fact {
      if (n.source != 0_usize && n.source != source.instance) {
        fail(ErrorKind::InvalidSource, id, text(n.name));
        return {};
      }
      if (!source.known)
        return {};
      usize column_id = n.column;
      if (n.registration != 0_usize) {
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
      if (! scope || scope->write_target == 0_usize) {
        fail(ErrorKind::InvalidShape, id, "write target"_str);
        return {};
      }
      if (n.qualifier.length != 0_usize && n.source == 0_usize) {
        fail(ErrorKind::InvalidShape, id, "unqualified column"_str);
        return {};
      }
      return from(scope->sources[0_usize]);
    }
    if (n.qualifier.length != 0_usize) {
      for (auto s = scope; s; s = s->parent)
        for (usize i = 0_usize; i < s->count; ++i)
          if (same(n.qualifier, s->sources[i].name))
            return from(s->sources[i]);
      fail(n.source != 0_usize ? ErrorKind::InvalidSource : ErrorKind::UnknownQualifier,
           id, text(n.qualifier));
      return {};
    }
    auto alias = [&](const Scope *s) -> Option<Fact> {
      if (!s || !s->ordering || !s->projection)
        return {};
      usize index = 0_usize;
      for (Id l = s->projection->list; l != 0_usize; l = node(l).b, ++index) {
        const auto &projected = node(node(l).a);
        if (projected.kind == Kind::Alias && same(n.name, projected.name))
          return Some(Fact(s->projection->values[index]));
      }
      return {};
    };
    if (alias_first)
      if (auto found = alias(scope))
        return *found;
    bool has_sources = false;
    for (auto s = scope; s; s = s->parent) {
      has_sources = has_sources || s->count != 0_usize;
      usize matches = 0_usize;
      bool unknown = false;
      Source found;
      for (usize i = 0_usize; i < s->count; ++i) {
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
      if (matches > 1_usize) {
        fail(ErrorKind::AmbiguousColumn, id, text(n.name));
        return {};
      }
      if (unknown)
        return {};
      if (matches != 0_usize)
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
      fail(ErrorKind::TypeMismatch, id, "comparison"_str);
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
      fail(ErrorKind::TypeMismatch, id, "assignment"_str);
    if (!target.type.nullable && nullable(value))
      fail(ErrorKind::NullableAssignment, id, "assignment"_str);
  }
  constexpr auto count(Id list) const -> usize {
    usize n = 0_usize;
    for (; list != 0_usize; list = node(list).b)
      ++n;
    return n;
  }
  constexpr void parameter(Id id, bool group = false) {
    auto n = node(id);
    if (preparing_) {
      if (group && !expanded_mode_) {
        fail(ErrorKind::InvalidShape, id,
             "parameter group requires runtime expansion"_str);
        return;
      }
      if (usize(static_cast<rstd::size_t>(n.type.kind)) >
        usize(static_cast<rstd::size_t>(ParameterKind::Blob))) {
        fail(ErrorKind::UnsupportedType, id, text(n.name));
        return;
      }
      auto name = text(n.name);
      usize index = 0_usize;
      for (; index < result_.parameter_count_; ++index)
        if (result_.parameter_name(index) == name)
          break;
      usize amount = group ? usize(static_cast<rstd::size_t>(n.integer)) : 1_usize;
      if (index < result_.parameter_count_) {
        if (result_.parameters_[index].type != n.type ||
            result_.member_identities_[index] != n.member_identity ||
            groups_[index] != group ||
            (group && group_counts_[index] != amount))
          fail(ErrorKind::ParameterConflict, id, name);
      } else {
        if (index >= builder_.limits_.parameters ||
            name.len() > result_.names_.len() - result_.name_size_) {
          fail(ErrorKind::Capacity, id, "parameters"_str);
          return;
        }
        if (expanded_mode_) {
          constexpr auto slot_limit = 2147483648ULL;
          const auto next =
              static_cast<unsigned long long>(next_slot_.to_primitive());
          const auto add =
              static_cast<unsigned long long>(amount.to_primitive());
          auto ceiling = static_cast<unsigned long long>(
              expansion_limits_.parameters.to_primitive());
          if (ceiling != static_cast<unsigned long long>(-1)) ++ceiling;
          if (next + add > ceiling || next + add > slot_limit) {
            fail(ErrorKind::Capacity, id, "parameters"_str);
            return;
          }
        }
        usize slot = amount != 0_usize ? next_slot_ : 0_usize;
        result_.parameters_[index] = {slot, n.type, result_.name_size_,
                                      usize(name.len())};
        result_.member_identities_[index] = n.member_identity;
        groups_[index] = group;
        group_counts_[index] = amount;
        next_slot_ += amount;
        for (usize c {}; c < name.len(); ++c)
          result_.names_[result_.name_size_++] = rstd::byte {name[c].to_primitive()};
        ++result_.parameter_count_;
      }
      prepared_.parameter_slots[id] = result_.parameters_[index].slot;
    } else if (group) {
      usize first = prepared_.parameter_slots[id];
      for (usize i = 0_usize;
           i < usize(n.integer) && error_.kind == ErrorKind::None; ++i) {
        if (i != 0_usize)
          out(", "_str);
        out('?');
        number(static_cast<long long>(first.to_primitive() + i.to_primitive()));
      }
    } else {
      out('?');
      number(static_cast<long long>(prepared_.parameter_slots[id].to_primitive()));
    }
  }
  enum class Use { Value, Projection, Ordering, Assignment, Column };
  constexpr void expressions(Id list, const Scope *scope, const CteScope *ctes,
                             usize depth, Use use = Use::Value,
                             ProjectionFacts *projection = nullptr) {
    bool first = true;
    usize index = 0_usize;
    for (; list != 0_usize && error_.kind == ErrorKind::None; list = node(list).b) {
      if (!first)
        out(", "_str);
      first = false;
      auto fact = expression(node(list).a, scope, ctes, depth + 1_usize, use);
      if (projection)
        projection->values[index++] = fact;
    }
  }
  constexpr void scalar_query(Id id, const Scope *scope, const CteScope *ctes,
                              usize depth, bool one) {
    const auto &q = builder_.queries_[id];
    if (preparing_ &&
        (q.kind != StatementKind::Select ||
         (one && (count(q.get(Slot::Projection)) != 1_usize ||
                  node(node(q.get(Slot::Projection)).a).kind == Kind::Star)))) {
      fail(ErrorKind::InvalidShape, id, "subquery projection"_str);
      return;
    }
    out('(');
    query(id, scope, ctes, depth + 1_usize);
    out(')');
  }
  constexpr auto expression(Id id, const Scope *scope, const CteScope *ctes,
                            usize depth, Use use = Use::Value) -> Fact {
    if (!enter(depth, id))
      return {};
    if (id == 0_usize) {
      fail(ErrorKind::InvalidHandle);
      return {};
    }
    const auto &n = node(id);
    usize occurrence = 0_usize;
    if (preparing_) {
      if (prepared_.count == prepared_.facts.len()) {
        fail(ErrorKind::Capacity, id, "expression uses"_str);
        return {};
      }
      occurrence = prepared_.count++;
    } else
      occurrence = fact_cursor_++;
    Fact fact;
    if (preparing_ && use == Use::Column &&
        (n.kind != Kind::Column || (n.qualifier.length != 0_usize && n.source == 0_usize))) {
      fail(ErrorKind::InvalidShape, id, "unqualified column"_str);
      return {};
    }
    if (preparing_ && use == Use::Assignment && n.kind != Kind::Assignment) {
      fail(ErrorKind::InvalidShape, id, "assignment"_str);
      return {};
    }
    switch (n.kind) {
    case Kind::Column:
      if (preparing_)
        fact = column(id, scope, use == Use::Column, use == Use::Ordering);
      if (n.qualifier.length != 0_usize && use != Use::Column) {
        quote(n.qualifier);
        out('.');
      }
      quote(n.name);
      break;
    case Kind::Excluded:
      if (preparing_) {
        if (!scope || ! scope->conflict_update || scope->write_target == 0_usize || n.a == 0_usize ||
            node(n.a).kind != Kind::Column) {
          fail(ErrorKind::InvalidShape, id, "excluded scope"_str);
          return {};
        }
        // SQLite resolves a real target named excluded before its proposed-row
        // pseudo-table. Write aliases are not supported by this API yet.
        if (same_name(text(node(scope->write_target).name), "excluded"_str)) {
          fail(ErrorKind::InvalidShape, id, "excluded target name"_str);
          return {};
        }
        fact = column(n.a, scope, true);
      }
      out("\"excluded\"."_str);
      quote(node(n.a).name);
      break;
    case Kind::Star:
      if (preparing_ && use != Use::Projection) {
        fail(ErrorKind::InvalidShape, id, "star"_str);
        return {};
      }
      qualifier(n.qualifier, scope, id);
      if (n.qualifier.length != 0_usize) {
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
      out("NULL"_str);
      break;
    case Kind::Parameter:
      fact = {FactKind::Known, n.type};
      parameter(id);
      break;
    case Kind::Binary: {
      constexpr array<ref<str>, 13> ops {
          " = "_str, " <> "_str, " < "_str, " <= "_str, " > "_str, " >= "_str,
          " + "_str, " - "_str, " * "_str, " / "_str, " AND "_str, " OR "_str,
          " LIKE "_str};
      if (n.operation >= ops.len()) {
        fail(ErrorKind::InvalidShape, id, "operator"_str);
        return {};
      }
      out('(');
      auto a = expression(n.a, scope, ctes, depth + 1_usize);
      out(ops[usize(n.operation.to_primitive())]);
      auto b = expression(n.b, scope, ctes, depth + 1_usize);
      if (preparing_) {
        auto op = Binary(static_cast<int>(n.operation.to_primitive()));
        if (op <= Binary::GreaterEqual) {
          compatible(a, b, id);
          fact = boolean(a, b);
        } else if (op == Binary::Like) {
          if ((a.kind == FactKind::Known && a.type.kind != ValueKind::Text) ||
              (b.kind == FactKind::Known && b.type.kind != ValueKind::Text))
            fail(ErrorKind::TypeMismatch, id, "LIKE"_str);
          fact = boolean(a, b);
        } else if (op >= Binary::And)
          fact = boolean(a, b);
        else {
          if ((a.kind == FactKind::Known && !numeric(a.type.kind)) ||
              (b.kind == FactKind::Known && !numeric(b.type.kind)))
            fail(ErrorKind::TypeMismatch, id, "arithmetic"_str);
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
        out("NOT "_str);
      auto operand = expression(n.a, scope, ctes, depth + 1_usize);
      fact = n.kind == Kind::Not
                 ? boolean(operand)
                 : Fact{FactKind::Known, {ValueKind::Boolean, false}};
      if (n.kind == Kind::IsNull)
        out(" IS NULL"_str);
      if (n.kind == Kind::IsNotNull)
        out(" IS NOT NULL"_str);
      out(')');
      break;
    }
    case Kind::Alias:
      if (preparing_ && use != Use::Projection) {
        fail(ErrorKind::InvalidShape, id, "projection alias"_str);
        return {};
      }
      fact = expression(n.a, scope, ctes, depth + 1_usize);
      out(" AS "_str);
      quote(n.name);
      break;
    case Kind::Order:
      if (preparing_ && use != Use::Ordering) {
        fail(ErrorKind::InvalidShape, id, "ordering"_str);
        return {};
      }
      fact = expression(n.a, scope, ctes, depth + 1_usize, Use::Ordering);
      out(n.flag ? " DESC"_str : " ASC"_str);
      break;
    case Kind::Assignment: {
      if (preparing_ && use != Use::Assignment) {
        fail(ErrorKind::InvalidShape, id, "assignment"_str);
        return {};
      }
      auto target = expression(n.a, scope, ctes, depth + 1_usize, Use::Column);
      out(" = "_str);
      auto value = expression(n.b, scope, ctes, depth + 1_usize);
      if (preparing_)
        assignment(target, value, id);
      break;
    }
    case Kind::In: {
      if (preparing_ && n.b == 0_usize) {
        fail(ErrorKind::InvalidShape, id, "empty IN"_str);
        return {};
      }
      out('(');
      auto a = expression(n.a, scope, ctes, depth + 1_usize);
      out(" IN ("_str);
      fact = boolean(a);
      for (Id l = n.b; l != 0_usize; l = node(l).b) {
        if (l != n.b)
          out(", "_str);
        auto b = expression(node(l).a, scope, ctes, depth + 2_usize);
        if (preparing_) {
          compatible(a, b, id);
          if (b.kind == FactKind::Unknown)
            fact = {};
          else if (nullable(b))
            fact.type.nullable = true;
        }
      }
      out("))"_str);
      break;
    }
    case Kind::InGroup: {
      const auto &group = node(n.b);
      if (n.b == 0_usize || group.kind != Kind::ParameterGroup) {
        fail(ErrorKind::InvalidShape, id, "parameter group"_str);
        return {};
      }
      out('(');
      auto a = expression(n.a, scope, ctes, depth + 1_usize);
      out(" IN ("_str);
      parameter(n.b, true);
      if (preparing_) {
        Fact b{FactKind::Known, group.type};
        compatible(a, b, id);
        fact = group.integer == 0
                   ? Fact{FactKind::Known, {ValueKind::Boolean, false}}
                   : boolean(a, b);
      }
      out("))"_str);
      break;
    }
    case Kind::InQuery:
      out('(');
      expression(n.a, scope, ctes, depth + 1_usize);
      out(" IN "_str);
      scalar_query(n.b, scope, ctes, depth + 1_usize, true);
      out(')');
      break;
    case Kind::Scalar:
      scalar_query(n.a, scope, ctes, depth + 1_usize, true);
      break;
    case Kind::Exists:
      fact = {FactKind::Known, {ValueKind::Boolean, false}};
      out("EXISTS "_str);
      scalar_query(n.a, scope, ctes, depth + 1_usize, false);
      break;
    case Kind::RowNumber: {
      if (preparing_ && (! scope || ! scope->window_projection || n.a == 0_usize)) {
        fail(ErrorKind::InvalidShape, id, "window projection"_str);
        return {};
      }
      fact = {FactKind::Known, {ValueKind::Integer, false}};
      out("ROW_NUMBER() OVER (ORDER BY "_str);
      Scope ordering_scope = scope ? *scope : Scope{};
      ordering_scope.window_projection = false;
      ordering_scope.ordering = false;
      ordering_scope.projection = nullptr;
      expressions(n.a, &ordering_scope, ctes, depth + 1_usize, Use::Ordering);
      out(')');
      break;
    }
    case Kind::Function: {
      constexpr array<ref<str>, 10> names {
          "COUNT"_str, "SUM"_str,  "AVG"_str,    "MIN"_str, "MAX"_str,
          "LOWER"_str, "UPPER"_str, "LENGTH"_str, "COALESCE"_str, "ABS"_str};
      usize arity = count(n.a);
      if (n.operation >= names.len() ||
          (n.operation == usize(static_cast<rstd::size_t>(Function::Coalesce)) ? arity < 2_usize
                                                       : arity != 1_usize)) {
        fail(ErrorKind::InvalidShape, id, "function arity"_str);
        return {};
      }
      out(names[n.operation]);
      out('(');
      if (n.operation == usize(static_cast<rstd::size_t>(Function::Count)) &&
          node(node(n.a).a).kind == Kind::Star) {
        if (preparing_ && node(node(n.a).a).qualifier.length != 0_usize) {
          fail(ErrorKind::InvalidShape, id, "COUNT star"_str);
          return {};
        }
        out('*');
        fact = {FactKind::Known, {ValueKind::Integer, false}};
      } else {
        auto f = Function(static_cast<int>(n.operation.to_primitive()));
        bool aggregate =
            f == Function::Count || f == Function::Sum || f == Function::Avg ||
            ((f == Function::Min || f == Function::Max) && arity == 1_usize);
        Option<Scope> aggregate_scope;
        const Scope *argument_scope = scope;
        if (aggregate && scope) {
          aggregate_scope = Some(Scope(*scope));
          aggregate_scope->window_projection = false;
          argument_scope = &*aggregate_scope;
        }
        bool first = true;
        Fact argument;
        bool coalesce_known = true, coalesce_nonnull = false;
        bool only_null = true;
        for (Id l = n.a; l != 0_usize; l = node(l).b) {
          if (!first)
            out(", "_str);
          auto v = expression(node(l).a, argument_scope, ctes, depth + 2_usize);
          if (first)
            argument = v;
          if (Function(static_cast<int>(n.operation.to_primitive())) == Function::Coalesce) {
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
      fail(ErrorKind::InvalidShape, id, "expression"_str);
      break;
    }
    if (preparing_)
      prepared_.facts[occurrence] = fact;
    return preparing_ ? fact : prepared_.facts[occurrence];
  }
  constexpr void source(Id id, const CteScope *ctes, usize depth) {
    if (!enter(depth, id))
      return;
    auto n = node(id);
    switch (n.kind) {
    case Kind::Table:
      if (preparing_) {
        for (auto s = ctes; s; s = s->parent)
          for (usize i = 0_usize; i < s->reserved_count; ++i)
            if (same(n.name, s->reserved[i])) {
              fail(ErrorKind::UnknownCte, id, "CTE reference requires cte()"_str);
              return;
            }
      }
      quote(n.name);
      if (n.qualifier.length != 0_usize) {
        out(" AS "_str);
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
      scalar_query(n.a, nullptr, ctes, depth + 1_usize, false);
      out(" AS "_str);
      quote(n.name);
      break;
    case Kind::Alias:
      source(n.a, ctes, depth + 1_usize);
      out(" AS "_str);
      quote(n.name);
      break;
    default:
      fail(ErrorKind::InvalidShape, id, "source"_str);
      break;
    }
  }
  constexpr void unique_columns(Id list, bool assignments) {
    if (!preparing_)
      return;
    for (Id a = list; a != 0_usize; a = node(a).b) {
      Id ai = node(a).a;
      if (assignments)
        ai = node(ai).a;
      if (node(ai).kind != Kind::Column ||
          (node(ai).qualifier.length != 0_usize && node(ai).source == 0_usize)) {
        fail(ErrorKind::InvalidShape, ai, "column"_str);
        return;
      }
      for (Id b = node(a).b; b != 0_usize; b = node(b).b) {
        Id bi = node(b).a;
        if (assignments)
          bi = node(bi).a;
        const auto &x = node(ai);
        const auto &y = node(bi);
        bool duplicate = x.registration != 0_usize && y.registration != 0_usize
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
                              usize depth) {
    if (!enter(depth, id))
      return;
    auto groups = node(id).a;
    if (preparing_) {
      // One validation per column, not per expanded cell: the AST and facts
      // remain bounded independently of the number of VALUES rows.
      for (Id l = groups; l != 0_usize && error_.kind == ErrorKind::None;
           l = node(l).b, columns = node(columns).b) {
        auto group_id = node(l).a;
        const auto &group = node(group_id);
        parameter(group_id, true);
        assignment(column(node(columns).a, scope, true),
                   Fact{FactKind::Known, group.type}, group_id);
      }
      return;
    }
    auto rows = usize(node(node(groups).a).integer);
    for (usize row = 0_usize; row < rows && error_.kind == ErrorKind::None;
         ++row) {
      if (row != 0_usize)
        out(", "_str);
      out('(');
      for (Id l = groups; l != 0_usize && error_.kind == ErrorKind::None; l = node(l).b) {
        if (l != groups)
          out(", "_str);
        out('?');
        number(static_cast<long long>(prepared_.parameter_slots[node(l).a]
                                          .to_primitive()) +
               static_cast<long long>(row.to_primitive()));
      }
      out(')');
    }
  }
  constexpr void shape(const QueryData &q, Id id) {
    if (! preparing_)
      return;
    auto get = [&](Slot s) { return q.get(s); };
    auto set = [&](Slot s) { return q.get(s) != 0_usize; };
    if (q.kind == StatementKind::Select) {
      if (! set(Slot::Projection) || set(Slot::Values) || set(Slot::Sets) ||
          set(Slot::Conflict) || set(Slot::ConflictSets) || set(Slot::Returning) ||
          q.allow_all || q.default_values ||
          (set(Slot::Joins) && ! set(Slot::Source)) ||
          (set(Slot::Having) && ! set(Slot::Group)) ||
          (set(Slot::Offset) && ! set(Slot::Limit)))
        fail(ErrorKind::InvalidShape, id, "SELECT"_str);
    } else {
      if (! set(Slot::Source) || node(get(Slot::Source)).kind != Kind::Table ||
          node(get(Slot::Source)).qualifier.length != 0_usize || q.distinct ||
          set(Slot::Group) || set(Slot::Having) || set(Slot::Order) ||
          set(Slot::Limit) || set(Slot::Offset) || set(Slot::Joins))
        fail(ErrorKind::InvalidShape, id, "write clauses"_str);
      if (q.kind == StatementKind::Insert) {
        auto values = get(Slot::Values);
        bool grouped = values != 0_usize && node(values).kind == Kind::ValuesGroups;
        auto inputs = grouped ? node(values).a : values;
        if ((q.default_values &&
             (get(Slot::Projection) != 0_usize || values != 0_usize ||
              get(Slot::Conflict) != 0_usize || get(Slot::ConflictSets) != 0_usize)) ||
            (! q.default_values &&
             (get(Slot::Projection) == 0_usize || inputs == 0_usize ||
              count(get(Slot::Projection)) != count(inputs))) ||
            (get(Slot::ConflictSets) != 0_usize && get(Slot::Conflict) == 0_usize) ||
            get(Slot::Where) != 0_usize || get(Slot::Sets) != 0_usize ||
            q.allow_all)
          fail(ErrorKind::InvalidShape, id, "INSERT"_str);
        if (grouped) {
          long long rows = 0;
          for (Id l = inputs; l != 0_usize && error_.kind == ErrorKind::None;
               l = node(l).b) {
            const auto &group = node(node(l).a);
            if (group.kind != Kind::ParameterGroup || group.integer <= 0 ||
                (rows && rows != group.integer)) {
              fail(ErrorKind::InvalidShape, id, "VALUES groups"_str);
              break;
            }
            rows = group.integer;
          }
        }
        unique_columns(get(Slot::Projection), false);
        if (get(Slot::Conflict) != 0_usize)
          unique_columns(get(Slot::Conflict), false);
        if (get(Slot::ConflictSets) != 0_usize)
          unique_columns(get(Slot::ConflictSets), true);
      } else {
        if (q.default_values || get(Slot::Projection) != 0_usize ||
            get(Slot::Values) != 0_usize || get(Slot::Conflict) != 0_usize ||
            get(Slot::ConflictSets) != 0_usize ||
            (q.kind == StatementKind::Delete && get(Slot::Returning) != 0_usize) ||
            (q.kind == StatementKind::Update ? get(Slot::Sets) == 0_usize
                                             : get(Slot::Sets) != 0_usize))
          fail(ErrorKind::InvalidShape, id, "UPDATE/DELETE"_str);
        if (get(Slot::Where) == 0_usize && !q.allow_all)
          fail(ErrorKind::UnsafeWrite, id);
        if (get(Slot::Sets) != 0_usize)
          unique_columns(get(Slot::Sets), true);
      }
    }
  }
  constexpr void query(Id id, const Scope *parent, const CteScope *outer_ctes,
                       usize depth) {
    if (!enter(depth, id))
      return;
    const auto &q = builder_.queries_[id];
    shape(q, id);
    if (error_.kind != ErrorKind::None)
      return;
    CteScope ctes{.parent = outer_ctes};
    for (Id l = q.get(Slot::Ctes); l != 0_usize; l = node(l).b) {
      if (ctes.reserved_count == ctes.reserved.len()) {
        fail(ErrorKind::Capacity, id, "CTEs"_str);
        return;
      }
      ctes.reserved[ctes.reserved_count++] = node(node(l).a).name;
    }
    if (auto list = q.get(Slot::Ctes); list != 0_usize) {
      out("WITH "_str);
      bool first = true;
      for (; list != 0_usize; list = node(list).b) {
        auto n = node(node(list).a);
        for (usize i = 0_usize; i < ctes.count; ++i)
          if (same(n.name, ctes.names[i]))
            fail(ErrorKind::DuplicateName, id, text(n.name));
        if (!first)
          out(", "_str);
        first = false;
        quote(n.name);
        out(" AS "_str);
        scalar_query(n.a, nullptr, &ctes, depth + 1_usize, false);
        if (ctes.count >= ctes.names.len()) {
          fail(ErrorKind::Capacity, id, "CTEs"_str);
          return;
        }
        ctes.names[ctes.count++] = n.name;
      }
      out(' ');
    }
    Option<ProjectionFacts> projection;
    if (preparing_ && q.kind == StatementKind::Select)
      for (Id l = q.get(Slot::Projection); l != 0_usize; l = node(l).b)
        if (node(node(l).a).kind == Kind::Alias) {
          projection = Some(ProjectionFacts{});
          projection->list = q.get(Slot::Projection);
          break;
        }
    Scope scope{.parent = parent};
    if (projection)
      scope.projection = &*projection;
    if (q.get(Slot::Source) != 0_usize)
      register_source(q.get(Slot::Source), scope);
    for (Id l = q.get(Slot::Joins); l != 0_usize; l = node(l).b)
      register_source(node(node(l).a).a, scope, node(node(l).a).flag);
    if (q.kind != StatementKind::Select)
      scope.write_target = q.get(Slot::Source);
    if (error_.kind != ErrorKind::None)
      return;
    if (q.kind == StatementKind::Select) {
      out(q.distinct ? "SELECT DISTINCT "_str : "SELECT "_str);
      Scope projection_scope = scope;
      projection_scope.window_projection = true;
      expressions(q.get(Slot::Projection), &projection_scope, &ctes, depth + 1_usize,
                  Use::Projection, projection ? &*projection : nullptr);
      if (q.get(Slot::Source) != 0_usize) {
        out(" FROM "_str);
        source(q.get(Slot::Source), &ctes, depth + 1_usize);
      }
      Scope join_scope{.parent = parent};
      if (q.get(Slot::Source) != 0_usize)
        register_source(q.get(Slot::Source), join_scope);
      for (Id l = q.get(Slot::Joins); l != 0_usize; l = node(l).b) {
        const auto &n = node(node(l).a);
        register_source(n.a, join_scope);
        out(n.flag ? " LEFT JOIN "_str : " INNER JOIN "_str);
        source(n.a, &ctes, depth + 1_usize);
        out(" ON "_str);
        expression(n.b, &join_scope, &ctes, depth + 1_usize);
        if (n.flag && join_scope.count != 0_usize)
          join_scope.sources[usize(join_scope.count.to_primitive() - 1)].nullable = true;
      }
    } else if (q.kind == StatementKind::Insert) {
      out("INSERT INTO "_str);
      source(q.get(Slot::Source), &ctes, depth + 1_usize);
      if (q.default_values) {
        out(" DEFAULT VALUES"_str);
      } else {
        out(" ("_str);
        expressions(q.get(Slot::Projection), &scope, &ctes, depth + 1_usize,
                    Use::Column);
        out(") VALUES "_str);
        if (node(q.get(Slot::Values)).kind == Kind::ValuesGroups) {
          group_values(q.get(Slot::Values), q.get(Slot::Projection), &scope,
                       depth + 1_usize);
        } else {
          out('(');
          Id column_list = q.get(Slot::Projection);
          for (Id l = q.get(Slot::Values); l != 0_usize; l = node(l).b) {
            if (l != q.get(Slot::Values))
              out(", "_str);
            auto value = expression(node(l).a, nullptr, &ctes, depth + 2_usize);
            if (preparing_)
              assignment(column(node(column_list).a, &scope, true), value,
                         node(l).a);
            column_list = node(column_list).b;
          }
          out(')');
        }
      }
      if (q.get(Slot::Conflict) != 0_usize) {
        out(" ON CONFLICT ("_str);
        expressions(q.get(Slot::Conflict), &scope, &ctes, depth + 1_usize,
                    Use::Column);
        if (q.get(Slot::ConflictSets) != 0_usize) {
          out(") DO UPDATE SET "_str);
          Scope conflict_scope = scope;
          conflict_scope.conflict_update = true;
          expressions(q.get(Slot::ConflictSets), &conflict_scope, &ctes,
                      depth + 1_usize, Use::Assignment);
        } else {
          out(") DO NOTHING"_str);
        }
      }
    } else if (q.kind == StatementKind::Update) {
      out("UPDATE "_str);
      source(q.get(Slot::Source), &ctes, depth + 1_usize);
      out(" SET "_str);
      expressions(q.get(Slot::Sets), &scope, &ctes, depth + 1_usize, Use::Assignment);
    } else {
      out("DELETE FROM "_str);
      source(q.get(Slot::Source), &ctes, depth + 1_usize);
    }
    if (q.get(Slot::Where) != 0_usize) {
      out(" WHERE "_str);
      expression(q.get(Slot::Where), &scope, &ctes, depth + 1_usize);
    }
    if (q.get(Slot::Group) != 0_usize) {
      out(" GROUP BY "_str);
      expressions(q.get(Slot::Group), &scope, &ctes, depth + 1_usize);
    }
    if (q.get(Slot::Having) != 0_usize) {
      out(" HAVING "_str);
      expression(q.get(Slot::Having), &scope, &ctes, depth + 1_usize);
    }
    if (q.get(Slot::Order) != 0_usize) {
      out(" ORDER BY "_str);
      scope.ordering = true;
      expressions(q.get(Slot::Order), &scope, &ctes, depth + 1_usize, Use::Ordering);
    }
    if (q.get(Slot::Returning) != 0_usize) {
      out(" RETURNING "_str);
      expressions(q.get(Slot::Returning), &scope, &ctes, depth + 1_usize,
                  Use::Column);
    }
    if (q.get(Slot::Limit) != 0_usize) {
      out(" LIMIT "_str);
      expression(q.get(Slot::Limit), nullptr, &ctes, depth + 1_usize);
    }
    if (q.get(Slot::Offset) != 0_usize) {
      out(" OFFSET "_str);
      expression(q.get(Slot::Offset), nullptr, &ctes, depth + 1_usize);
    }
  }

  void prepare_and_measure(Id id) {
    query(id, nullptr, nullptr, 0_usize);
    if (error_.kind != ErrorKind::None)
      return;
    preparing_ = false;
    measuring_ = true;
    fact_cursor_ = 0_usize;
    measured_sql_ = 0_usize;
    query(id, nullptr, nullptr, 0_usize);
    measuring_ = false;
    if (error_.kind == ErrorKind::None && fact_cursor_ != prepared_.count)
      fail(ErrorKind::InvalidShape, id, "expression uses"_str);
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
    if (! value.owner_ || value.id_ == 0_usize)
      return rstd::Err(Error{.kind = ErrorKind::InvalidHandle});
    ExpandedQuery result;
    Compiler c(*value.owner_, result, limits);
    if (value.owner_->error_.kind != ErrorKind::None)
      return rstd::Err(Error(value.owner_->error_));
    c.query(value.id_, nullptr, nullptr, 0_usize);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    c.preparing_ = false;
    c.query(value.id_, nullptr, nullptr, 0_usize);
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
    if (! value.owner_ || value.id_ == 0_usize)
      return rstd::Err(Error{.kind = ErrorKind::InvalidHandle});
    Compiler c(*value.owner_, ceiling);
    if (value.owner_->error_.kind != ErrorKind::None)
      return rstd::Err(Error(value.owner_->error_));
    c.prepare_and_measure(value.id_);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    return rstd::Ok(ExpansionPlan{c.next_slot_ - 1_usize, c.measured_sql_});
  }

  static auto run_expanded_planned(Query value, ExpansionLimits ceiling)
      -> rstd::Result<ExpandedQuery, Error> {
    if (! value.owner_ || value.id_ == 0_usize)
      return rstd::Err(Error{.kind = ErrorKind::InvalidHandle});
    Compiler c(*value.owner_, ceiling);
    if (value.owner_->error_.kind != ErrorKind::None)
      return rstd::Err(Error(value.owner_->error_));
    c.prepare_and_measure(value.id_);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    ExpansionPlan plan{c.next_slot_ - 1_usize, c.measured_sql_};
    ExpandedQuery result;
    result.sql_.reserve(plan.sql);
    c.expanded_ = &result;
    c.expansion_limits_ = {plan.parameters, plan.sql};
    c.fact_cursor_ = 0_usize;
    c.query(value.id_, nullptr, nullptr, 0_usize);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    if (result.sql_.len() != plan.sql || c.next_slot_ - 1_usize != plan.parameters ||
        c.fact_cursor_ != c.prepared_.count) {
      c.fail(ErrorKind::InvalidShape, value.id_, "expansion plan"_str);
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
    if (! value.owner_ || value.id_ == 0_usize)
      return rstd::Err(Error{.kind = ErrorKind::InvalidHandle});
    Compiler c(*value.owner_);
    if (value.owner_->error_.kind != ErrorKind::None)
      return rstd::Err(Error(value.owner_->error_));
    c.query(value.id_, nullptr, nullptr, 0_usize);
    if (c.error_.kind != ErrorKind::None)
      return rstd::Err(Error(c.error_));
    c.preparing_ = false;
    c.query(value.id_, nullptr, nullptr, 0_usize);
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
