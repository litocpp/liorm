module;
#include <array>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
export module liorm:query;
export import :table;

namespace liorm::query {
class Compiler;
}

export namespace liorm::query {
template <auto Member>
inline constexpr unsigned char member_parameter_identity{};

enum class ErrorKind {
  None,
  Capacity,
  Depth,
  InvalidHandle,
  ForeignOwner,
  InvalidName,
  InvalidShape,
  DuplicateClause,
  DuplicateName,
  UnknownQualifier,
  UnknownCte,
  ParameterConflict,
  UnsafeWrite,
  UnsupportedType,
  UnknownTable,
  UnknownColumn,
  AmbiguousColumn,
  InvalidSource,
  InvalidRelation,
  TypeMismatch,
  NullableAssignment
};
struct Error {
  ErrorKind kind{};
  unsigned node{};
  std::array<char, 96> context{};
  unsigned length{};
  bool truncated{};
  constexpr auto name() const -> std::string_view {
    return {context.data(), length};
  }
};
struct Limits {
  unsigned nodes = 512;
  unsigned queries = 128;
  unsigned text = 8192;
  unsigned sql = 16384;
  unsigned parameters = 64;
  unsigned depth = 64;
  unsigned registrations = 16;
  schema::SchemaLimits schema{};
};
enum class Function {
  Count,
  Sum,
  Avg,
  Min,
  Max,
  Lower,
  Upper,
  Length,
  Coalesce,
  Abs
};
enum class Binary {
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Add,
  Subtract,
  Multiply,
  Divide,
  And,
  Or,
  Like
};
class QueryBuilder;
class Query;
class ParameterGroup {
  friend class QueryBuilder;
  friend class Expr;
  QueryBuilder *owner_{};
  unsigned id_{};
  constexpr ParameterGroup(QueryBuilder *owner, unsigned id)
      : owner_(owner), id_(id) {}

public:
  constexpr ParameterGroup() = default;
};
class Expr {
  friend class QueryBuilder;
  friend class BoundTable;
  friend class Query;
  QueryBuilder *owner_{};
  unsigned id_{};
  constexpr Expr(QueryBuilder *owner, unsigned id) : owner_(owner), id_(id) {}

public:
  constexpr Expr() = default;
  constexpr auto as(std::string_view name) const -> Expr;
  constexpr auto asc() const -> Expr;
  constexpr auto desc() const -> Expr;
  constexpr auto is_null() const -> Expr;
  constexpr auto is_not_null() const -> Expr;
  constexpr auto like(Expr other) const -> Expr;
  constexpr auto in(std::span<const Expr> values) const -> Expr;
  constexpr auto in(Query query) const -> Expr;
  constexpr auto in(ParameterGroup values) const -> Expr;
  constexpr auto set(Expr value) const -> Expr;
  constexpr auto binary(Binary op, Expr other) const -> Expr;
  constexpr auto operator!() const -> Expr;
};
class Query {
  friend class QueryBuilder;
  friend class Compiler;
  QueryBuilder *owner_{};
  unsigned id_{};
  constexpr Query(QueryBuilder *owner, unsigned id) : owner_(owner), id_(id) {}

public:
  constexpr Query() = default;
  constexpr auto from(Expr source) const -> Query;
  constexpr auto where(Expr condition) const -> Query;
  constexpr auto having(Expr condition) const -> Query;
  constexpr auto limit(Expr value) const -> Query;
  constexpr auto offset(Expr value) const -> Query;
  constexpr auto distinct() const -> Query;
  constexpr auto allow_all() const -> Query;
  constexpr auto join(Expr source, Expr on) const -> Query;
  constexpr auto left_join(Expr source, Expr on) const -> Query;
  constexpr auto with(std::string_view name, Query body) const -> Query;
  constexpr auto as(std::string_view name) const -> Expr;
  constexpr auto group_by(std::span<const Expr> values) const -> Query;
  constexpr auto order_by(std::span<const Expr> values) const -> Query;
  constexpr auto values(std::span<const Expr> values) const -> Query;
  // Zip nonempty, equally sized parameter groups into ordered VALUES rows.
  // This form requires try_compile_expanded; each column keeps contiguous
  // slots.
  constexpr auto values_groups(std::span<const ParameterGroup> columns) const
      -> Query;
  constexpr auto set(std::span<const Expr> values) const -> Query;
  constexpr auto on_conflict_do_nothing(std::span<const Expr> columns) const
      -> Query;
  constexpr auto on_conflict_do_update(std::span<const Expr> columns,
                                       std::span<const Expr> assignments) const
      -> Query;
  constexpr auto returning(std::span<const Expr> columns) const -> Query;
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto on_conflict_do_nothing(T... items) const -> Query {
    const std::array<Expr, sizeof...(T)> list{items...};
    return on_conflict_do_nothing(std::span<const Expr>(list));
  }
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto returning(T... items) const -> Query {
    const std::array<Expr, sizeof...(T)> list{items...};
    return returning(std::span<const Expr>(list));
  }
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto group_by(T... items) const -> Query {
    const std::array<Expr, sizeof...(T)> list{items...};
    return group_by(list);
  }
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto order_by(T... items) const -> Query {
    const std::array<Expr, sizeof...(T)> list{items...};
    return order_by(list);
  }
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto values(T... items) const -> Query {
    const std::array<Expr, sizeof...(T)> list{items...};
    return values(list);
  }
  template <class... T>
    requires((std::is_same_v<T, ParameterGroup>) && ...)
  constexpr auto values_groups(T... items) const -> Query {
    const std::array<ParameterGroup, sizeof...(T)> list{items...};
    return values_groups(std::span<const ParameterGroup>(list));
  }
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto set(T... items) const -> Query {
    const std::array<Expr, sizeof...(T)> list{items...};
    return set(list);
  }
};
} // namespace liorm::query

namespace liorm::query {
using Id = unsigned;
enum class Kind {
  Invalid,
  Column,
  Excluded,
  Star,
  Table,
  Cte,
  Derived,
  Parameter,
  ParameterGroup,
  ValuesGroups,
  Integer,
  Text,
  Null,
  Binary,
  Not,
  IsNull,
  IsNotNull,
  In,
  InGroup,
  InQuery,
  Scalar,
  Exists,
  Function,
  RowNumber,
  Alias,
  Order,
  Assignment,
  Link,
  Join,
  CteDefinition
};
struct Text {
  unsigned offset{}, length{};
};
struct Node {
  Kind kind{};
  Id a{}, b{};
  Text name{}, qualifier{};
  long long integer{};
  ParameterType type{};
  unsigned operation{};
  bool flag{};
  Id source{};
  unsigned registration{}, table{}, column{};
  const void *member_identity{};
};
enum class StatementKind { Select, Insert, Update, Delete };
enum class Slot {
  Projection,
  Source,
  Where,
  Group,
  Having,
  Order,
  Limit,
  Offset,
  Joins,
  Ctes,
  Values,
  Sets,
  Conflict,
  ConflictSets,
  Returning,
  Count
};
struct QueryData {
  StatementKind kind{};
  std::array<Id, static_cast<unsigned>(Slot::Count)> slots{};
  bool distinct{}, allow_all{}, default_values{};
  constexpr auto get(Slot slot) const -> Id {
    return slots[static_cast<unsigned>(slot)];
  }
  constexpr auto get(Slot slot) -> Id & {
    return slots[static_cast<unsigned>(slot)];
  }
};
class Compiler;
} // namespace liorm::query

export namespace liorm::query {
template <class Model> class TypedBoundTable;
class BoundTable {
  friend class QueryBuilder;
  friend class BoundSchema;
  QueryBuilder *owner_{};
  unsigned source_{};
  constexpr BoundTable(QueryBuilder *owner, unsigned source)
      : owner_(owner), source_(source) {}

public:
  constexpr BoundTable() = default;
  constexpr auto source() const -> Expr;
  constexpr auto col(std::string_view name) const -> Expr;
};
template <class Model> class TypedBoundTable {
  friend class BoundSchema;
  BoundTable table_;
  constexpr explicit TypedBoundTable(BoundTable table) : table_(table) {}

public:
  constexpr TypedBoundTable() = default;
  constexpr auto source() const -> Expr { return table_.source(); }
  constexpr operator BoundTable() const { return table_; }
  template <auto Member>
    requires requires {
      requires std::is_same_v<
          typename schema::MemberPointer<decltype(Member)>::owner_type, Model>;
      schema::column_name<Member>();
    }
  constexpr auto col() const -> Expr {
    return table_.col(schema::column_name<Member>());
  }
};

template <class Model> struct ModelGroup {
  using model_type = Model;
  using definition =
      decltype(liorm_table_definition(static_cast<Model *>(nullptr)));
  static constexpr std::size_t count = definition::field_count;
};
template <std::size_t N> struct ColumnsGroup {
  static constexpr std::size_t count = N;
};
template <class Segment, std::size_t Offset> struct ProjectionGroup {
  using segment_type = Segment;
  static constexpr std::size_t offset = Offset;
  static constexpr std::size_t count = Segment::count;
};
template <class... Segments> struct ProjectionLayout {
  static constexpr std::size_t count = (std::size_t{0} + ... + Segments::count);
  template <std::size_t I> static consteval auto offset() -> std::size_t {
    static_assert(I < sizeof...(Segments));
    return []<std::size_t... N>(std::index_sequence<N...>) {
      return (std::size_t{0} + ... +
              std::tuple_element_t<N, std::tuple<Segments...>>::count);
    }(std::make_index_sequence<I>{});
  }
  template <std::size_t I> constexpr auto group() const {
    return ProjectionGroup<std::tuple_element_t<I, std::tuple<Segments...>>,
                           offset<I>()>{};
  }
};

template <class Model, std::size_t... I>
constexpr auto model_projection(TypedBoundTable<Model> table,
                                std::index_sequence<I...>) {
  using Definition = typename ModelGroup<Model>::definition;
  return std::array<Expr, sizeof...(I)>{
      table.template col<Definition::template field<I>::member>()...};
}
template <class Model>
constexpr auto projection_values(ModelGroup<Model>,
                                 TypedBoundTable<Model> table) {
  return model_projection(table,
                          std::make_index_sequence<ModelGroup<Model>::count>{});
}
template <std::size_t N>
constexpr auto projection_values(ColumnsGroup<N>, std::array<Expr, N> values) {
  return values;
}
template <class... Segments, class... Args>
  requires(sizeof...(Segments) == sizeof...(Args)) &&
          (requires(Segments segment, Args arg) {
            projection_values(segment, arg);
          } && ...)
constexpr auto project(ProjectionLayout<Segments...>, Args... args) {
  std::array<Expr, ProjectionLayout<Segments...>::count> result{};
  std::size_t index = 0;
  auto append = [&](auto values) {
    for (auto value : values)
      result[index++] = value;
  };
  (append(projection_values(Segments{}, args)), ...);
  return result;
}
class BoundForeignKey {
  friend class QueryBuilder;
  friend class BoundSchema;
  QueryBuilder *owner_{};
  unsigned registration_{}, id_{};
  constexpr BoundForeignKey(QueryBuilder *owner, unsigned registration,
                            unsigned id)
      : owner_(owner), registration_(registration), id_(id) {}

public:
  constexpr BoundForeignKey() = default;
};
class BoundSchema {
  friend class QueryBuilder;
  QueryBuilder *owner_{};
  unsigned registration_{};
  constexpr BoundSchema(QueryBuilder *owner, unsigned registration)
      : owner_(owner), registration_(registration) {}

public:
  constexpr BoundSchema() = default;
  constexpr auto table(std::string_view name, std::string_view alias = {}) const
      -> BoundTable;
  // Every mapped column must have the same type; extra columns are allowed.
  template <class Model>
  constexpr auto table(std::string_view alias = {}) const
      -> TypedBoundTable<Model>;
  constexpr auto foreign_key(std::string_view name) const -> BoundForeignKey;
};
class QueryBuilder {
  friend class Expr;
  friend class Query;
  friend class Compiler;
  friend class ParameterGroup;
  friend class BoundSchema;
  friend class BoundTable;
  schema::Registry schemas_;
  std::array<Node, 512> nodes_{};
  std::array<QueryData, 128> queries_{};
  std::array<char, 8192> text_{};
  unsigned node_count_{1}, query_count_{1}, text_count_{};
  Limits limits_;
  Error error_{};

  constexpr void fail(ErrorKind kind, Id node = 0, std::string_view name = {}) {
    if (error_.kind != ErrorKind::None)
      return;
    error_.kind = kind;
    error_.node = node;
    error_.length = name.size() < error_.context.size()
                        ? unsigned(name.size())
                        : unsigned(error_.context.size());
    error_.truncated = name.size() > error_.length;
    for (unsigned i = 0; i < error_.length; ++i)
      error_.context[i] = name[i];
  }
  constexpr auto store(std::string_view name, bool empty = false) -> Text {
    if (!empty && name.empty()) {
      fail(ErrorKind::InvalidName);
      return {};
    }
    for (char c : name)
      if (!c) {
        fail(ErrorKind::InvalidName, 0, name);
        return {};
      }
    if (name.size() > limits_.text - text_count_) {
      fail(ErrorKind::Capacity, 0, "text");
      return {};
    }
    Text result{text_count_, unsigned(name.size())};
    for (char c : name)
      text_[text_count_++] = c;
    return result;
  }
  constexpr auto text(Text value) const -> std::string_view {
    return {text_.data() + value.offset, value.length};
  }
  constexpr auto add(Node node) -> Id {
    if (node_count_ >= limits_.nodes) {
      fail(ErrorKind::Capacity, 0, "nodes");
      return 0;
    }
    nodes_[node_count_] = node;
    return node_count_++;
  }
  constexpr auto add_query(QueryData data) -> Query {
    if (query_count_ >= limits_.queries) {
      fail(ErrorKind::Capacity, 0, "queries");
      return {};
    }
    queries_[query_count_] = data;
    return Query(this, query_count_++);
  }
  constexpr auto check(Expr value) -> Id {
    if (!value.owner_ || !value.id_) {
      fail(ErrorKind::InvalidHandle);
      return 0;
    }
    if (value.owner_ != this) {
      fail(ErrorKind::ForeignOwner);
      return 0;
    }
    if (value.id_ >= node_count_) {
      fail(ErrorKind::InvalidHandle);
      return 0;
    }
    return value.id_;
  }
  constexpr auto check(ParameterGroup value) -> Id {
    if (!value.owner_ || !value.id_) {
      fail(ErrorKind::InvalidHandle);
      return 0;
    }
    if (value.owner_ != this) {
      fail(ErrorKind::ForeignOwner);
      return 0;
    }
    if (value.id_ >= node_count_) {
      fail(ErrorKind::InvalidHandle);
      return 0;
    }
    return value.id_;
  }
  constexpr auto check(Query value) -> Id {
    if (!value.owner_ || !value.id_) {
      fail(ErrorKind::InvalidHandle);
      return 0;
    }
    if (value.owner_ != this) {
      fail(ErrorKind::ForeignOwner);
      return 0;
    }
    if (value.id_ >= query_count_) {
      fail(ErrorKind::InvalidHandle);
      return 0;
    }
    return value.id_;
  }
  constexpr auto list(std::span<const Expr> values) -> Id {
    Id result = 0;
    for (auto i = values.size(); i > 0; --i)
      result =
          add(Node{.kind = Kind::Link, .a = check(values[i - 1]), .b = result});
    return result;
  }
  constexpr auto list(std::span<const ParameterGroup> values) -> Id {
    Id result = 0;
    for (auto i = values.size(); i > 0; --i)
      result =
          add(Node{.kind = Kind::Link, .a = check(values[i - 1]), .b = result});
    return result;
  }
  constexpr auto append(Id list_id, Id value, unsigned depth = 0) -> Id {
    if (depth >= limits_.depth) {
      fail(ErrorKind::Depth);
      return 0;
    }
    if (!list_id)
      return add(Node{.kind = Kind::Link, .a = value});
    Node copy = nodes_[list_id];
    copy.b = append(copy.b, value, depth + 1);
    return add(copy);
  }
  constexpr auto change(Query query, Slot slot, Id value, bool repeated = false)
      -> Query {
    Id id = check(query);
    auto data = queries_[id];
    if (!repeated && data.get(slot))
      fail(ErrorKind::DuplicateClause, id);
    data.get(slot) = repeated ? append(data.get(slot), value) : value;
    return add_query(data);
  }
  constexpr auto unary(Kind kind, Expr value, bool flag = false) -> Expr {
    return Expr(this, add(Node{.kind = kind, .a = check(value), .flag = flag}));
  }
  constexpr auto named(Kind kind, std::string_view name,
                       std::string_view qualifier = {}) -> Expr {
    Node node{.kind = kind, .name = store(name)};
    if (!qualifier.empty())
      node.qualifier = store(qualifier);
    return Expr(this, add(node));
  }
  constexpr auto bound_column(Id source_id, unsigned column_id) -> Expr {
    const auto &source = nodes_[source_id];
    auto column = schemas_.column(column_id);
    return Expr(this,
                add(Node{.kind = Kind::Column,
                         .name = store(column.name),
                         .qualifier = source.qualifier.length ? source.qualifier
                                                              : source.name,
                         .type = column.type,
                         .source = source_id,
                         .registration = source.registration,
                         .table = source.table,
                         .column = column_id}));
  }
  constexpr auto copy_node(const QueryBuilder &source, Id id,
                           std::array<Id, 512> &nm, std::array<Id, 128> &qm,
                           unsigned depth, std::array<Id, 16> &sm) -> Id {
    if (!id)
      return 0;
    if (depth >= limits_.depth) {
      fail(ErrorKind::Depth);
      return 0;
    }
    if (nm[id])
      return nm[id];
    Node node = source.nodes_[id];
    node.name = store(source.text(node.name), true);
    node.qualifier = store(source.text(node.qualifier), true);
    if (node.registration) {
      unsigned old = node.registration - 1;
      if (!sm[old]) {
        auto imported = schemas_.import(source.schemas_, old);
        if (imported.is_err()) {
          fail(ErrorKind::Capacity, 0, "schemas");
          return 0;
        }
        sm[old] = imported.unwrap() + 1;
      }
      node.registration = sm[old];
      node.table = schemas_.remap_table(source.schemas_, node.table, old,
                                        node.registration - 1);
      if (node.kind == Kind::Column)
        node.column = schemas_.remap_column(source.schemas_, node.column, old,
                                            node.registration - 1);
    }
    node.source = copy_node(source, node.source, nm, qm, depth + 1, sm);
    switch (node.kind) {
    case Kind::Derived:
    case Kind::Scalar:
    case Kind::Exists:
    case Kind::CteDefinition:
      node.a = copy_query(source, node.a, nm, qm, depth + 1, sm);
      break;
    case Kind::InQuery:
      node.a = copy_node(source, node.a, nm, qm, depth + 1, sm);
      node.b = copy_query(source, node.b, nm, qm, depth + 1, sm);
      break;
    default:
      node.a = copy_node(source, node.a, nm, qm, depth + 1, sm);
      node.b = copy_node(source, node.b, nm, qm, depth + 1, sm);
      break;
    }
    return nm[id] = add(node);
  }
  constexpr auto copy_query(const QueryBuilder &source, Id id,
                            std::array<Id, 512> &nm, std::array<Id, 128> &qm,
                            unsigned depth, std::array<Id, 16> &sm) -> Id {
    if (!id || depth >= limits_.depth) {
      fail(ErrorKind::Depth);
      return 0;
    }
    if (qm[id])
      return qm[id];
    auto data = source.queries_[id];
    for (auto &slot : data.slots)
      slot = copy_node(source, slot, nm, qm, depth + 1, sm);
    return qm[id] = add_query(data).id_;
  }

public:
  constexpr explicit QueryBuilder(Limits limits = {})
      : schemas_(limits.schema, limits.registrations), limits_(limits) {
    if (limits.nodes > nodes_.size() || limits.nodes < 2 ||
        limits.queries > queries_.size() || limits.queries < 2 ||
        limits.text > text_.size() || limits.sql > 16384 ||
        limits.parameters > 64 || limits.depth > 128 || !limits.depth ||
        limits.registrations > 16 || !schema::valid_limits(limits.schema))
      fail(ErrorKind::Capacity, 0, "limits");
    if (error_.kind != ErrorKind::None)
      limits_ = Limits{};
  }
  QueryBuilder(const QueryBuilder &) = delete;
  QueryBuilder(QueryBuilder &&) = delete;
  auto operator=(const QueryBuilder &) -> QueryBuilder & = delete;
  auto operator=(QueryBuilder &&) -> QueryBuilder & = delete;
  constexpr auto error() const -> Error { return error_; }
  constexpr auto use(const schema::Schema &schema) -> BoundSchema {
    if (error_.kind != ErrorKind::None)
      return {};
    auto registered = schemas_.use(schema);
    if (registered.is_err()) {
      fail(ErrorKind::Capacity, 0, "schemas");
      return {};
    }
    return BoundSchema(this, registered.unwrap() + 1);
  }
  constexpr auto on(BoundForeignKey fk, BoundTable source, BoundTable target)
      -> Expr {
    if (!fk.owner_ || !source.owner_ || !target.owner_) {
      fail(ErrorKind::InvalidHandle);
      return {};
    }
    if (fk.owner_ != this || source.owner_ != this || target.owner_ != this) {
      fail(ErrorKind::ForeignOwner);
      return {};
    }
    auto f = schemas_.foreign_key(fk.id_);
    auto key = schemas_.key(f.target);
    auto a = nodes_[source.source_], b = nodes_[target.source_];
    if (a.registration != fk.registration_ ||
        b.registration != fk.registration_ || a.table != f.table ||
        b.table != key.table || source.source_ == target.source_) {
      fail(ErrorKind::InvalidRelation, 0, f.name);
      return {};
    }
    Expr result;
    for (unsigned i = 0; i < f.columns.size(); ++i) {
      auto condition = bound_column(source.source_, f.columns[i])
                           .binary(Binary::Equal, bound_column(target.source_,
                                                               key.columns[i]));
      result = i ? result.binary(Binary::And, condition) : condition;
    }
    return result;
  }
  constexpr auto col(std::string_view name) -> Expr {
    return named(Kind::Column, name);
  }
  constexpr auto col(std::string_view qualifier, std::string_view name)
      -> Expr {
    return named(Kind::Column, name, qualifier);
  }
  // Only the current INSERT's conflict-update RHS may use this expression.
  constexpr auto excluded(Expr column) -> Expr {
    auto id = check(column);
    if (nodes_[id].kind != Kind::Column)
      fail(ErrorKind::InvalidShape, id, "excluded column");
    return Expr(this, add(Node{.kind = Kind::Excluded, .a = id}));
  }
  constexpr auto table(std::string_view name) -> Expr {
    return named(Kind::Table, name);
  }
  constexpr auto cte(std::string_view name) -> Expr {
    return named(Kind::Cte, name);
  }
  constexpr auto star(std::string_view qualifier = {}) -> Expr {
    return Expr(this, add(Node{.kind = Kind::Star,
                               .qualifier = store(qualifier, true)}));
  }
  template <class T> constexpr auto param(std::string_view name) -> Expr {
    if constexpr (requires { ParameterTraits<T>::type; })
      return Expr(this, add(Node{.kind = Kind::Parameter,
                                 .name = store(name),
                                 .type = ParameterTraits<T>::type}));
    else {
      fail(ErrorKind::UnsupportedType, 0, name);
      return {};
    }
  }
  template <auto Member, class T>
    requires requires { schema::column_name<Member>(); }
  constexpr auto member_param(std::string_view name) -> Expr {
    if constexpr (requires { ParameterTraits<T>::type; })
      return member_param<Member>(name, ParameterTraits<T>::type);
    else {
      fail(ErrorKind::UnsupportedType, 0, name);
      return {};
    }
  }
  template <auto Member>
    requires requires { schema::column_name<Member>(); }
  constexpr auto member_param(std::string_view name, ParameterType type)
      -> Expr {
    if (!valid_type(type)) {
      fail(ErrorKind::UnsupportedType, 0, name);
      return {};
    }
    return Expr(
        this, add(Node{.kind = Kind::Parameter,
                       .name = store(name),
                       .type = type,
                       .member_identity = &member_parameter_identity<Member>}));
  }
  // Groups sharing a name must have the same type and count. Scalar parameters
  // and groups cannot share a name.
  template <class T>
  constexpr auto param_group(std::string_view name, rstd::usize count)
      -> ParameterGroup {
    if constexpr (requires { ParameterTraits<T>::type; })
      return param_group(name, count, ParameterTraits<T>::type);
    else {
      fail(ErrorKind::UnsupportedType, 0, name);
      return {};
    }
  }
  constexpr auto param_group(std::string_view name, rstd::usize count,
                             ParameterType type) -> ParameterGroup {
    if (!valid_type(type)) {
      fail(ErrorKind::UnsupportedType, 0, name);
      return {};
    }
    auto size = count.to_primitive();
    if (size > 2147483647ULL) {
      fail(ErrorKind::Capacity, 0, "parameters");
      return {};
    }
    return ParameterGroup(this,
                          add(Node{.kind = Kind::ParameterGroup,
                                   .name = store(name),
                                   .integer = static_cast<long long>(size),
                                   .type = type}));
  }
  template <auto Member>
    requires requires { schema::column_name<Member>(); }
  constexpr auto member_param_group(std::string_view name, rstd::usize count,
                                    ParameterType type) -> ParameterGroup {
    auto result = param_group(name, count, type);
    if (result.owner_ == this && result.id_)
      nodes_[result.id_].member_identity = &member_parameter_identity<Member>;
    return result;
  }
  constexpr auto integer(long long value) -> Expr {
    return Expr(this, add(Node{.kind = Kind::Integer, .integer = value}));
  }
  constexpr auto text_value(std::string_view value) -> Expr {
    return Expr(this,
                add(Node{.kind = Kind::Text, .name = store(value, true)}));
  }
  constexpr auto null() -> Expr {
    return Expr(this, add(Node{.kind = Kind::Null}));
  }
  constexpr auto function(Function function, std::span<const Expr> args)
      -> Expr {
    return Expr(this, add(Node{.kind = Kind::Function,
                               .a = list(args),
                               .operation = unsigned(function)}));
  }
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto function(Function f, T... args) -> Expr {
    const std::array<Expr, sizeof...(T)> values{args...};
    return function(f, values);
  }
  // Only SELECT projection scalar expressions may contain this window call.
  // The nonempty ordering list accepts ordinary expressions and asc()/desc().
  constexpr auto row_number(std::span<const Expr> ordering) -> Expr {
    if (ordering.empty())
      fail(ErrorKind::InvalidShape, 0, "empty window ordering");
    return Expr(this, add(Node{.kind = Kind::RowNumber, .a = list(ordering)}));
  }
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto row_number(T... ordering) -> Expr {
    const std::array<Expr, sizeof...(T)> list{ordering...};
    return row_number(std::span<const Expr>(list));
  }
  constexpr auto select(std::span<const Expr> values) -> Query {
    QueryData data;
    data.get(Slot::Projection) = list(values);
    return add_query(data);
  }
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto select(T... args) -> Query {
    const std::array<Expr, sizeof...(T)> values{args...};
    return select(values);
  }
  constexpr auto insert_into(Expr table, std::span<const Expr> columns)
      -> Query {
    QueryData data{.kind = StatementKind::Insert};
    data.get(Slot::Source) = check(table);
    data.get(Slot::Projection) = list(columns);
    return add_query(data);
  }
  constexpr auto insert_default_values(Expr table) -> Query {
    QueryData data{.kind = StatementKind::Insert, .default_values = true};
    data.get(Slot::Source) = check(table);
    return add_query(data);
  }
  template <class... T>
    requires((std::is_same_v<T, Expr>) && ...)
  constexpr auto insert_into(Expr table, T... columns) -> Query {
    const std::array<Expr, sizeof...(T)> values{columns...};
    return insert_into(table, values);
  }
  constexpr auto update(Expr table) -> Query {
    QueryData data{.kind = StatementKind::Update};
    data.get(Slot::Source) = check(table);
    return add_query(data);
  }
  constexpr auto delete_from(Expr table) -> Query {
    QueryData data{.kind = StatementKind::Delete};
    data.get(Slot::Source) = check(table);
    return add_query(data);
  }
  constexpr auto scalar(Query query) -> Expr {
    return Expr(this, add(Node{.kind = Kind::Scalar, .a = check(query)}));
  }
  constexpr auto exists(Query query) -> Expr {
    return Expr(this, add(Node{.kind = Kind::Exists, .a = check(query)}));
  }
  constexpr auto import_query(Query query) -> Query {
    if (error_.kind != ErrorKind::None)
      return {};
    if (!query.owner_ || !query.id_) {
      fail(ErrorKind::InvalidHandle);
      return {};
    }
    if (query.owner_->error_.kind != ErrorKind::None) {
      error_ = query.owner_->error_;
      return {};
    }
    std::array<Id, 512> nodes{};
    std::array<Id, 128> queries{};
    std::array<Id, 16> schemas{};
    return Query(
        this, copy_query(*query.owner_, query.id_, nodes, queries, 0, schemas));
  }
};

constexpr auto BoundSchema::table(std::string_view name,
                                  std::string_view alias) const -> BoundTable {
  if (!owner_)
    return {};
  auto id = owner_->schemas_.find_table(registration_ - 1, name);
  if (!id) {
    owner_->fail(ErrorKind::UnknownTable, 0, name);
    return {};
  }
  Node n{.kind = Kind::Table,
         .name = owner_->store(owner_->schemas_.table(*id).name),
         .registration = registration_,
         .table = *id};
  if (!alias.empty())
    n.qualifier = owner_->store(alias);
  return BoundTable(owner_, owner_->add(n));
}
template <class Model>
constexpr auto BoundSchema::table(std::string_view alias) const
    -> TypedBoundTable<Model> {
  using Definition = typename ModelGroup<Model>::definition;
  auto bound = table(Definition::name.view(), alias);
  if (!owner_ || !bound.source_)
    return {};
  auto table_id = owner_->nodes_[bound.source_].table;
  bool valid = true;
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    auto check = [&]<class Field>() {
      auto id = owner_->schemas_.find_column(table_id, Field::name.view());
      if (!id) {
        owner_->fail(ErrorKind::UnknownColumn, bound.source_,
                     Field::name.view());
        valid = false;
      } else if (owner_->schemas_.column(*id).type != Field::type) {
        owner_->fail(ErrorKind::TypeMismatch, bound.source_,
                     Field::name.view());
        valid = false;
      }
    };
    (check.template operator()<typename Definition::template field<I>>(), ...);
  }(std::make_index_sequence<Definition::field_count>{});
  return valid ? TypedBoundTable<Model>(bound) : TypedBoundTable<Model>();
}
constexpr auto BoundSchema::foreign_key(std::string_view name) const
    -> BoundForeignKey {
  if (!owner_)
    return {};
  auto id = owner_->schemas_.find_foreign_key(registration_ - 1, name);
  if (!id) {
    owner_->fail(ErrorKind::InvalidRelation, 0, name);
    return {};
  }
  return BoundForeignKey(owner_, registration_, *id);
}
constexpr auto BoundTable::source() const -> Expr {
  return owner_ ? Expr(owner_, source_) : Expr{};
}
constexpr auto BoundTable::col(std::string_view name) const -> Expr {
  if (!owner_ || !source_)
    return {};
  auto source = owner_->nodes_[source_];
  auto id = owner_->schemas_.find_column(source.table, name);
  if (!id) {
    owner_->fail(ErrorKind::UnknownColumn, source_, name);
    return {};
  }
  return owner_->bound_column(source_, *id);
}
constexpr auto Expr::binary(Binary op, Expr other) const -> Expr {
  if (!owner_)
    return {};
  return Expr(owner_, owner_->add(Node{.kind = Kind::Binary,
                                       .a = owner_->check(*this),
                                       .b = owner_->check(other),
                                       .operation = unsigned(op)}));
}
constexpr auto operator==(Expr a, Expr b) -> Expr {
  return a.binary(Binary::Equal, b);
}
constexpr auto operator!=(Expr a, Expr b) -> Expr {
  return a.binary(Binary::NotEqual, b);
}
constexpr auto operator<(Expr a, Expr b) -> Expr {
  return a.binary(Binary::Less, b);
}
constexpr auto operator<=(Expr a, Expr b) -> Expr {
  return a.binary(Binary::LessEqual, b);
}
constexpr auto operator>(Expr a, Expr b) -> Expr {
  return a.binary(Binary::Greater, b);
}
constexpr auto operator>=(Expr a, Expr b) -> Expr {
  return a.binary(Binary::GreaterEqual, b);
}
constexpr auto operator+(Expr a, Expr b) -> Expr {
  return a.binary(Binary::Add, b);
}
constexpr auto operator-(Expr a, Expr b) -> Expr {
  return a.binary(Binary::Subtract, b);
}
constexpr auto operator*(Expr a, Expr b) -> Expr {
  return a.binary(Binary::Multiply, b);
}
constexpr auto operator/(Expr a, Expr b) -> Expr {
  return a.binary(Binary::Divide, b);
}
constexpr auto operator&&(Expr a, Expr b) -> Expr {
  return a.binary(Binary::And, b);
}
constexpr auto operator||(Expr a, Expr b) -> Expr {
  return a.binary(Binary::Or, b);
}
constexpr auto Expr::operator!() const -> Expr {
  return owner_ ? owner_->unary(Kind::Not, *this) : Expr{};
}
constexpr auto Expr::is_null() const -> Expr {
  return owner_ ? owner_->unary(Kind::IsNull, *this) : Expr{};
}
constexpr auto Expr::is_not_null() const -> Expr {
  return owner_ ? owner_->unary(Kind::IsNotNull, *this) : Expr{};
}
constexpr auto Expr::asc() const -> Expr {
  return owner_ ? owner_->unary(Kind::Order, *this) : Expr{};
}
constexpr auto Expr::desc() const -> Expr {
  return owner_ ? owner_->unary(Kind::Order, *this, true) : Expr{};
}
constexpr auto Expr::like(Expr other) const -> Expr {
  return binary(Binary::Like, other);
}
constexpr auto Expr::as(std::string_view name) const -> Expr {
  if (!owner_)
    return {};
  return Expr(owner_, owner_->add(Node{.kind = Kind::Alias,
                                       .a = owner_->check(*this),
                                       .name = owner_->store(name)}));
}
constexpr auto Expr::in(std::span<const Expr> values) const -> Expr {
  if (!owner_)
    return {};
  return Expr(owner_, owner_->add(Node{.kind = Kind::In,
                                       .a = owner_->check(*this),
                                       .b = owner_->list(values)}));
}
constexpr auto Expr::in(ParameterGroup values) const -> Expr {
  if (!owner_)
    return {};
  return Expr(owner_, owner_->add(Node{.kind = Kind::InGroup,
                                       .a = owner_->check(*this),
                                       .b = owner_->check(values)}));
}
constexpr auto Expr::in(Query query) const -> Expr {
  if (!owner_)
    return {};
  return Expr(owner_, owner_->add(Node{.kind = Kind::InQuery,
                                       .a = owner_->check(*this),
                                       .b = owner_->check(query)}));
}
constexpr auto Expr::set(Expr value) const -> Expr {
  if (!owner_)
    return {};
  return Expr(owner_, owner_->add(Node{.kind = Kind::Assignment,
                                       .a = owner_->check(*this),
                                       .b = owner_->check(value)}));
}
constexpr auto Query::from(Expr x) const -> Query {
  return owner_ ? owner_->change(*this, Slot::Source, owner_->check(x))
                : Query{};
}
constexpr auto Query::where(Expr x) const -> Query {
  return owner_ ? owner_->change(*this, Slot::Where, owner_->check(x))
                : Query{};
}
constexpr auto Query::having(Expr x) const -> Query {
  return owner_ ? owner_->change(*this, Slot::Having, owner_->check(x))
                : Query{};
}
constexpr auto Query::limit(Expr x) const -> Query {
  return owner_ ? owner_->change(*this, Slot::Limit, owner_->check(x))
                : Query{};
}
constexpr auto Query::offset(Expr x) const -> Query {
  return owner_ ? owner_->change(*this, Slot::Offset, owner_->check(x))
                : Query{};
}
constexpr auto Query::group_by(std::span<const Expr> x) const -> Query {
  if (owner_ && x.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty GROUP BY");
  return owner_ ? owner_->change(*this, Slot::Group, owner_->list(x)) : Query{};
}
constexpr auto Query::order_by(std::span<const Expr> x) const -> Query {
  if (owner_ && x.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty ORDER BY");
  return owner_ ? owner_->change(*this, Slot::Order, owner_->list(x)) : Query{};
}
constexpr auto Query::values(std::span<const Expr> x) const -> Query {
  if (owner_ && x.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty VALUES");
  return owner_ ? owner_->change(*this, Slot::Values, owner_->list(x))
                : Query{};
}
constexpr auto
Query::values_groups(std::span<const ParameterGroup> columns) const -> Query {
  if (!owner_)
    return {};
  if (columns.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty VALUES groups");
  auto groups =
      owner_->add(Node{.kind = Kind::ValuesGroups, .a = owner_->list(columns)});
  return owner_->change(*this, Slot::Values, groups);
}
constexpr auto Query::set(std::span<const Expr> x) const -> Query {
  if (owner_ && x.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty SET");
  return owner_ ? owner_->change(*this, Slot::Sets, owner_->list(x)) : Query{};
}
constexpr auto
Query::on_conflict_do_nothing(std::span<const Expr> columns) const -> Query {
  if (owner_ && columns.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty conflict target");
  return owner_ ? owner_->change(*this, Slot::Conflict, owner_->list(columns))
                : Query{};
}
constexpr auto
Query::on_conflict_do_update(std::span<const Expr> columns,
                             std::span<const Expr> assignments) const -> Query {
  if (!owner_)
    return {};
  if (columns.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty conflict target");
  if (assignments.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty conflict SET");
  auto data = owner_->queries_[owner_->check(*this)];
  if (data.get(Slot::Conflict) || data.get(Slot::ConflictSets))
    owner_->fail(ErrorKind::DuplicateClause, id_);
  data.get(Slot::Conflict) = owner_->list(columns);
  data.get(Slot::ConflictSets) = owner_->list(assignments);
  return owner_->add_query(data);
}
constexpr auto Query::returning(std::span<const Expr> columns) const -> Query {
  if (owner_ && columns.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty RETURNING");
  return owner_ ? owner_->change(*this, Slot::Returning, owner_->list(columns))
                : Query{};
}
constexpr auto Query::distinct() const -> Query {
  if (!owner_)
    return {};
  auto data = owner_->queries_[owner_->check(*this)];
  if (data.distinct)
    owner_->fail(ErrorKind::DuplicateClause);
  data.distinct = true;
  return owner_->add_query(data);
}
constexpr auto Query::allow_all() const -> Query {
  if (!owner_)
    return {};
  auto data = owner_->queries_[owner_->check(*this)];
  data.allow_all = true;
  return owner_->add_query(data);
}
constexpr auto Query::join(Expr source, Expr on) const -> Query {
  if (!owner_)
    return {};
  auto join = owner_->add(Node{
      .kind = Kind::Join, .a = owner_->check(source), .b = owner_->check(on)});
  return owner_->change(*this, Slot::Joins, join, true);
}
constexpr auto Query::left_join(Expr source, Expr on) const -> Query {
  if (!owner_)
    return {};
  auto join = owner_->add(Node{.kind = Kind::Join,
                               .a = owner_->check(source),
                               .b = owner_->check(on),
                               .flag = true});
  return owner_->change(*this, Slot::Joins, join, true);
}
constexpr auto Query::with(std::string_view name, Query body) const -> Query {
  if (!owner_)
    return {};
  auto node = owner_->add(Node{.kind = Kind::CteDefinition,
                               .a = owner_->check(body),
                               .name = owner_->store(name)});
  return owner_->change(*this, Slot::Ctes, node, true);
}
constexpr auto Query::as(std::string_view name) const -> Expr {
  if (!owner_)
    return {};
  return Expr(owner_, owner_->add(Node{.kind = Kind::Derived,
                                       .a = owner_->check(*this),
                                       .name = owner_->store(name)}));
}
} // namespace liorm::query
