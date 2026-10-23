module;
#include <array>
#include <span>
#include <string_view>
#include <type_traits>
export module liorm:query;
export import rstd.core;

namespace liorm::query {
class Compiler;
}

export namespace liorm::query {
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
  UnsupportedType
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
enum class ParameterKind { Integer, Real, Boolean, Text, Blob };
struct ParameterType {
  ParameterKind kind{};
  bool nullable{};
  constexpr bool operator==(const ParameterType &) const = default;
};
struct TextParameter {};
struct BlobParameter {};
template <class T> struct Nullable {};
template <class T> struct ParameterTraits;
template <> struct ParameterTraits<rstd::i32> {
  static constexpr ParameterType type{ParameterKind::Integer, false};
};
template <> struct ParameterTraits<rstd::i64> : ParameterTraits<rstd::i32> {};
template <> struct ParameterTraits<rstd::f64> {
  static constexpr ParameterType type{ParameterKind::Real, false};
};
template <> struct ParameterTraits<bool> {
  static constexpr ParameterType type{ParameterKind::Boolean, false};
};
template <> struct ParameterTraits<TextParameter> {
  static constexpr ParameterType type{ParameterKind::Text, false};
};
template <> struct ParameterTraits<BlobParameter> {
  static constexpr ParameterType type{ParameterKind::Blob, false};
};
template <class T> struct ParameterTraits<Nullable<T>> {
  static constexpr ParameterType type{ParameterTraits<T>::type.kind, true};
};
struct Limits {
  unsigned nodes = 512;
  unsigned queries = 128;
  unsigned text = 8192;
  unsigned sql = 16384;
  unsigned parameters = 64;
  unsigned depth = 64;
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
class Expr {
  friend class QueryBuilder;
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
  constexpr auto set(std::span<const Expr> values) const -> Query;
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
  Star,
  Table,
  Cte,
  Derived,
  Parameter,
  Integer,
  Text,
  Null,
  Binary,
  Not,
  IsNull,
  IsNotNull,
  In,
  InQuery,
  Scalar,
  Exists,
  Function,
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
  Count
};
struct QueryData {
  StatementKind kind{};
  std::array<Id, static_cast<unsigned>(Slot::Count)> slots{};
  bool distinct{}, allow_all{};
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
class QueryBuilder {
  friend class Expr;
  friend class Query;
  friend class Compiler;
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
  constexpr auto copy_node(const QueryBuilder &source, Id id,
                           std::array<Id, 512> &nm, std::array<Id, 128> &qm,
                           unsigned depth) -> Id {
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
    switch (node.kind) {
    case Kind::Derived:
    case Kind::Scalar:
    case Kind::Exists:
    case Kind::CteDefinition:
      node.a = copy_query(source, node.a, nm, qm, depth + 1);
      break;
    case Kind::InQuery:
      node.a = copy_node(source, node.a, nm, qm, depth + 1);
      node.b = copy_query(source, node.b, nm, qm, depth + 1);
      break;
    default:
      node.a = copy_node(source, node.a, nm, qm, depth + 1);
      node.b = copy_node(source, node.b, nm, qm, depth + 1);
      break;
    }
    return nm[id] = add(node);
  }
  constexpr auto copy_query(const QueryBuilder &source, Id id,
                            std::array<Id, 512> &nm, std::array<Id, 128> &qm,
                            unsigned depth) -> Id {
    if (!id || depth >= limits_.depth) {
      fail(ErrorKind::Depth);
      return 0;
    }
    if (qm[id])
      return qm[id];
    auto data = source.queries_[id];
    for (auto &slot : data.slots)
      slot = copy_node(source, slot, nm, qm, depth + 1);
    return qm[id] = add_query(data).id_;
  }

public:
  constexpr explicit QueryBuilder(Limits limits = {}) : limits_(limits) {
    if (limits.nodes > nodes_.size() || limits.nodes < 2 ||
        limits.queries > queries_.size() || limits.queries < 2 ||
        limits.text > text_.size() || limits.sql > 16384 ||
        limits.parameters > 64 || limits.depth > 128 || !limits.depth)
      fail(ErrorKind::Capacity, 0, "limits");
    if (error_.kind != ErrorKind::None)
      limits_ = Limits{};
  }
  QueryBuilder(const QueryBuilder &) = delete;
  QueryBuilder(QueryBuilder &&) = delete;
  auto operator=(const QueryBuilder &) -> QueryBuilder & = delete;
  auto operator=(QueryBuilder &&) -> QueryBuilder & = delete;
  constexpr auto error() const -> Error { return error_; }
  constexpr auto col(std::string_view name) -> Expr {
    return named(Kind::Column, name);
  }
  constexpr auto col(std::string_view qualifier, std::string_view name)
      -> Expr {
    return named(Kind::Column, name, qualifier);
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
  template <class T>
    requires requires { ParameterTraits<T>::type; }
  constexpr auto param(std::string_view name) -> Expr {
    return Expr(this, add(Node{.kind = Kind::Parameter,
                               .name = store(name),
                               .type = ParameterTraits<T>::type}));
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
    return Query(this, copy_query(*query.owner_, query.id_, nodes, queries, 0));
  }
};

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
constexpr auto Query::set(std::span<const Expr> x) const -> Query {
  if (owner_ && x.empty())
    owner_->fail(ErrorKind::InvalidShape, id_, "empty SET");
  return owner_ ? owner_->change(*this, Slot::Sets, owner_->list(x)) : Query{};
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
