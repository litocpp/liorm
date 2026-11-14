module;
export module liorm:query;
export import :table;
import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace liorm::query
{
class Compiler;
}

export namespace liorm::query
{
template<auto Member>
inline constexpr unsigned char member_parameter_identity {};

enum class ErrorKind
{
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
    ErrorKind       kind {};
    usize           node {};
    array<char, 96> context {};
    usize           length {};
    bool            truncated {};
    inline auto     name() const -> ref<str> {
        return ref<str>::from_raw_parts_unchecked(
            reinterpret_cast<rstd::byte const*>(context.data()), usize(length));
    }
};
struct Limits {
    usize                nodes         = 512_usize;
    usize                queries       = 128_usize;
    usize                text          = 8192_usize;
    usize                sql           = 16384_usize;
    usize                parameters    = 64_usize;
    usize                depth         = 64_usize;
    usize                registrations = 16_usize;
    schema::SchemaLimits schema {};
};
enum class Function
{
    Count,
    Sum,
    Avg,
    Min,
    Max,
    Lower,
    Upper,
    Length,
    Coalesce,
    Abs,
    Random
};
enum class Binary
{
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
    Like,
    Match
};
class QueryBuilder;
class Query;
class ParameterGroup {
    friend class QueryBuilder;
    friend class Expr;
    QueryBuilder* owner_ {};
    usize         id_ {};
    constexpr ParameterGroup(QueryBuilder* owner, usize id): owner_(owner), id_(id) {}

public:
    constexpr ParameterGroup() = default;
};
class Expr {
    friend class QueryBuilder;
    friend class BoundTable;
    friend class Query;
    QueryBuilder* owner_ {};
    usize         id_ {};
    constexpr Expr(QueryBuilder* owner, usize id): owner_(owner), id_(id) {}

public:
    constexpr Expr() = default;
    constexpr auto as(ref<str> name) const -> Expr;
    constexpr auto asc() const -> Expr;
    constexpr auto desc() const -> Expr;
    constexpr auto is_null() const -> Expr;
    constexpr auto is_not_null() const -> Expr;
    constexpr auto like(Expr other) const -> Expr;
    constexpr auto match(Expr other) const -> Expr;
    constexpr auto in(slice<Expr> values) const -> Expr;
    constexpr auto in(Query query) const -> Expr;
    constexpr auto in(ParameterGroup values) const -> Expr;
    constexpr auto set(Expr value) const -> Expr;
    constexpr auto binary(Binary op, Expr other) const -> Expr;
    constexpr auto operator!() const -> Expr;
};
// AST handles borrow their builder; only compiled queries own their data.
class Query {
    friend class QueryBuilder;
    friend class Compiler;
    QueryBuilder* owner_ {};
    usize         id_ {};
    constexpr Query(QueryBuilder* owner, usize id): owner_(owner), id_(id) {}

public:
    constexpr Query() = default;
    // On UPDATE, FROM supplies a source without replacing the write target.
    constexpr auto from(Expr source) const -> Query;
    constexpr auto where(Expr condition) const -> Query;
    constexpr auto having(Expr condition) const -> Query;
    constexpr auto limit(Expr value) const -> Query;
    constexpr auto offset(Expr value) const -> Query;
    constexpr auto distinct() const -> Query;
    constexpr auto or_replace() const -> Query;
    constexpr auto allow_all() const -> Query;
    constexpr auto join(Expr source, Expr on) const -> Query;
    constexpr auto left_join(Expr source, Expr on) const -> Query;
    constexpr auto with(ref<str> name, Query body) const -> Query;
    constexpr auto as(ref<str> name) const -> Expr;
    constexpr auto group_by(slice<Expr> values) const -> Query;
    constexpr auto order_by(slice<Expr> values) const -> Query;
    constexpr auto values(slice<Expr> values) const -> Query;
    // Zip nonempty, equally sized parameter groups into ordered VALUES rows.
    // This form requires try_compile_expanded; each column keeps contiguous
    // slots.
    constexpr auto values_groups(slice<ParameterGroup> columns) const -> Query;
    constexpr auto set(slice<Expr> values) const -> Query;
    constexpr auto on_conflict_do_nothing(slice<Expr> columns) const -> Query;
    constexpr auto on_conflict_do_update(slice<Expr> columns, slice<Expr> assignments) const
        -> Query;
    constexpr auto returning(slice<Expr> columns) const -> Query;
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto on_conflict_do_nothing(T... items) const -> Query {
        const array<Expr, sizeof...(T)> list { items... };
        return on_conflict_do_nothing(list.as_slice());
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto returning(T... items) const -> Query {
        const array<Expr, sizeof...(T)> list { items... };
        return returning(list.as_slice());
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto group_by(T... items) const -> Query {
        const array<Expr, sizeof...(T)> list { items... };
        return group_by(list.as_slice());
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto order_by(T... items) const -> Query {
        const array<Expr, sizeof...(T)> list { items... };
        return order_by(list.as_slice());
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto values(T... items) const -> Query {
        const array<Expr, sizeof...(T)> list { items... };
        return values(list.as_slice());
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, ParameterGroup>) && ...)
    constexpr auto values_groups(T... items) const -> Query {
        const array<ParameterGroup, sizeof...(T)> list { items... };
        return values_groups(list.as_slice());
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto set(T... items) const -> Query {
        const array<Expr, sizeof...(T)> list { items... };
        return set(list.as_slice());
    }
};
} // namespace liorm::query

namespace liorm::query
{
using Id = usize;
enum class Kind
{
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
    usize offset {}, length {};
};
struct Node {
    Kind          kind {};
    Id            a {}, b {};
    Text          name {}, qualifier {};
    long long     integer {};
    ParameterType type {};
    usize         operation {};
    bool          flag {};
    Id            source {};
    usize         registration {}, table {}, column {};
    const void*   member_identity {};
};
enum class StatementKind
{
    Select,
    Insert,
    Update,
    Delete,
    Values
};
enum class Slot
{
    Projection,
    Source,
    UpdateFrom,
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
    StatementKind                                     kind {};
    array<Id, static_cast<rstd::size_t>(Slot::Count)> slots {};
    bool           distinct {}, allow_all {}, default_values {}, or_replace {};
    constexpr auto get(Slot slot) const -> Id {
        return slots[usize(static_cast<rstd::size_t>(slot))];
    }
    constexpr auto get(Slot slot) -> Id& { return slots[usize(static_cast<rstd::size_t>(slot))]; }
};
class Compiler;
} // namespace liorm::query

export namespace liorm::query
{
template<class Model>
class TypedBoundTable;
class BoundTable {
    friend class QueryBuilder;
    friend class BoundSchema;
    QueryBuilder* owner_ {};
    usize         source_ {};
    constexpr BoundTable(QueryBuilder* owner, usize source): owner_(owner), source_(source) {}

public:
    constexpr BoundTable() = default;
    constexpr auto source() const -> Expr;
    constexpr auto col(ref<str> name) const -> Expr;
};
template<class Model>
class TypedBoundTable {
    friend class BoundSchema;
    BoundTable table_;
    constexpr explicit TypedBoundTable(BoundTable table): table_(table) {}

public:
    constexpr TypedBoundTable() = default;
    constexpr auto source() const -> Expr { return table_.source(); }
    constexpr      operator BoundTable() const { return table_; }
    template<auto Member>
        requires requires {
            requires rstd::mtp::
                same_as<typename schema::MemberPointer<decltype(Member)>::owner_type, Model>;
            schema::column_name<Member>();
        }
    constexpr auto col() const -> Expr {
        return table_.col(schema::column_name<Member>());
    }
};

template<class Model>
struct ModelGroup {
    using model_type = Model;
    using definition = decltype(liorm_table_definition(static_cast<Model*>(nullptr)));
    static constexpr rstd::size_t count = definition::field_count;
};
template<rstd::size_t N>
struct ColumnsGroup {
    static constexpr rstd::size_t count = N;
};
template<class Segment, rstd::size_t Offset>
struct ProjectionGroup {
    using segment_type                   = Segment;
    static constexpr rstd::size_t offset = Offset;
    static constexpr rstd::size_t count  = Segment::count;
};
template<class... Segments>
struct ProjectionLayout {
    static constexpr rstd::size_t count = (rstd::size_t { 0 } + ... + Segments::count);
    template<rstd::size_t I>
    static consteval auto offset() -> rstd::size_t {
        static_assert(I < sizeof...(Segments));
        return []<rstd::size_t... N>(rstd::mtp::index_sequence<N...>) {
            return (rstd::size_t { 0 } + ... +
                    rstd::mtp::tuple_element<N, rstd::tuple<Segments...>>::count);
        }(rstd::mtp::make_index_sequence<I> {});
    }
    template<rstd::size_t I>
    constexpr auto group() const {
        return ProjectionGroup<rstd::mtp::tuple_element<I, rstd::tuple<Segments...>>,
                               offset<I>()> {};
    }
};

template<class Model, rstd::size_t... I>
constexpr auto model_projection(TypedBoundTable<Model> table, rstd::mtp::index_sequence<I...>) {
    using Definition = typename ModelGroup<Model>::definition;
    return array<Expr, sizeof...(I)> {
        table.template col<Definition::template field<I>::member>()...
    };
}
template<class Model>
constexpr auto projection_values(ModelGroup<Model>, TypedBoundTable<Model> table) {
    return model_projection(table, rstd::mtp::make_index_sequence<ModelGroup<Model>::count> {});
}
template<rstd::size_t N>
constexpr auto projection_values(ColumnsGroup<N>, array<Expr, N> values) {
    return values;
}
template<class... Segments, class... Args>
    requires(sizeof...(Segments) == sizeof...(Args)) &&
            (requires(Segments segment, Args arg) { projection_values(segment, arg); } && ...)
constexpr auto project(ProjectionLayout<Segments...>, Args... args) {
    array<Expr, ProjectionLayout<Segments...>::count> result {};
    usize                                             index {};
    auto                                              append = [&](auto values) {
        for (auto value : values) result[index] = value, ++index;
    };
    (append(projection_values(Segments {}, args)), ...);
    return result;
}
class BoundForeignKey {
    friend class QueryBuilder;
    friend class BoundSchema;
    QueryBuilder* owner_ {};
    usize         registration_ {}, id_ {};
    constexpr BoundForeignKey(QueryBuilder* owner, usize registration, usize id)
        : owner_(owner), registration_(registration), id_(id) {}

public:
    constexpr BoundForeignKey() = default;
};
class BoundSchema {
    friend class QueryBuilder;
    QueryBuilder* owner_ {};
    usize         registration_ {};
    constexpr BoundSchema(QueryBuilder* owner, usize registration)
        : owner_(owner), registration_(registration) {}

public:
    constexpr BoundSchema() = default;
    constexpr auto table(ref<str> name, ref<str> alias = {}) const -> BoundTable;
    // Every mapped column must have the same type; extra columns are allowed.
    template<class Model>
    constexpr auto table(ref<str> alias = {}) const -> TypedBoundTable<Model>;
    constexpr auto foreign_key(ref<str> name) const -> BoundForeignKey;
};
class QueryBuilder {
    friend class Expr;
    friend class Query;
    friend class Compiler;
    friend class ParameterGroup;
    friend class BoundSchema;
    friend class BoundTable;
    schema::Registry        schemas_;
    array<Node, 512>        nodes_ {};
    array<QueryData, 128>   queries_ {};
    array<rstd::byte, 8192> text_ {};
    usize                   node_count_ { 1_usize }, query_count_ { 1_usize }, text_count_ {};
    Limits                  limits_;
    Error                   error_ {};

    constexpr void fail(ErrorKind kind, Id node = 0_usize, ref<str> name = {}) {
        if (error_.kind != ErrorKind::None) return;
        error_.kind = kind;
        error_.node = node;
        error_.length =
            usize(name.len() < error_.context.len() ? name.len().to_primitive()
                                                    : error_.context.len().to_primitive());
        error_.truncated = name.len() > usize(error_.length);
        for (usize i {}; i < usize(error_.length); ++i)
            error_.context[i] = char(name[i].to_primitive());
    }
    constexpr auto store(ref<str> name, bool empty = false) -> Text {
        if (! empty && name.is_empty()) {
            fail(ErrorKind::InvalidName);
            return {};
        }
        for (usize i {}; i < name.len(); ++i)
            if (! name[i].to_primitive()) {
                fail(ErrorKind::InvalidName, 0_usize, name);
                return {};
            }
        if (name.len() > limits_.text - text_count_) {
            fail(ErrorKind::Capacity, 0_usize, "text"_str);
            return {};
        }
        Text result { text_count_, name.len() };
        for (usize i {}; i < name.len(); ++i)
            text_[text_count_++] = rstd::byte { name[i].to_primitive() };
        return result;
    }
    constexpr auto text(Text value) const -> ref<str> {
        return ref<str>::from_raw_parts_unchecked(text_.data() + value.offset.to_primitive(),
                                                  value.length);
    }
    constexpr auto add(Node node) -> Id {
        if (node_count_ >= limits_.nodes) {
            fail(ErrorKind::Capacity, 0_usize, "nodes"_str);
            return 0_usize;
        }
        nodes_[node_count_] = node;
        return node_count_++;
    }
    constexpr auto add_query(QueryData data) -> Query {
        if (query_count_ >= limits_.queries) {
            fail(ErrorKind::Capacity, 0_usize, "queries"_str);
            return {};
        }
        queries_[query_count_] = data;
        return Query(this, query_count_++);
    }
    constexpr auto check(Expr value) -> Id {
        if (! value.owner_ || value.id_ == 0_usize) {
            fail(ErrorKind::InvalidHandle);
            return 0_usize;
        }
        if (value.owner_ != this) {
            fail(ErrorKind::ForeignOwner);
            return 0_usize;
        }
        if (value.id_ >= node_count_) {
            fail(ErrorKind::InvalidHandle);
            return 0_usize;
        }
        return value.id_;
    }
    constexpr auto check(ParameterGroup value) -> Id {
        if (! value.owner_ || value.id_ == 0_usize) {
            fail(ErrorKind::InvalidHandle);
            return 0_usize;
        }
        if (value.owner_ != this) {
            fail(ErrorKind::ForeignOwner);
            return 0_usize;
        }
        if (value.id_ >= node_count_) {
            fail(ErrorKind::InvalidHandle);
            return 0_usize;
        }
        return value.id_;
    }
    constexpr auto check(Query value) -> Id {
        if (! value.owner_ || value.id_ == 0_usize) {
            fail(ErrorKind::InvalidHandle);
            return 0_usize;
        }
        if (value.owner_ != this) {
            fail(ErrorKind::ForeignOwner);
            return 0_usize;
        }
        if (value.id_ >= query_count_) {
            fail(ErrorKind::InvalidHandle);
            return 0_usize;
        }
        return value.id_;
    }
    constexpr auto list(slice<Expr> values) -> Id {
        Id result = 0_usize;
        for (usize i = values.len(); i > 0_usize; --i)
            result = add(Node { .kind = Kind::Link, .a = check(values[i - 1_usize]), .b = result });
        return result;
    }
    constexpr auto list(slice<ParameterGroup> values) -> Id {
        Id result = 0_usize;
        for (usize i = values.len(); i > 0_usize; --i)
            result = add(Node { .kind = Kind::Link, .a = check(values[i - 1_usize]), .b = result });
        return result;
    }
    constexpr auto append(Id list_id, Id value, usize depth = 0_usize) -> Id {
        if (depth >= limits_.depth) {
            fail(ErrorKind::Depth);
            return 0_usize;
        }
        if (list_id == 0_usize) return add(Node { .kind = Kind::Link, .a = value });
        Node copy = nodes_[list_id];
        copy.b    = append(copy.b, value, depth + 1_usize);
        return add(copy);
    }
    constexpr auto change(Query query, Slot slot, Id value, bool repeated = false) -> Query {
        Id   id   = check(query);
        auto data = queries_[id];
        if (! repeated && data.get(slot) != 0_usize) fail(ErrorKind::DuplicateClause, id);
        data.get(slot) = repeated ? append(data.get(slot), value) : value;
        return add_query(data);
    }
    constexpr auto unary(Kind kind, Expr value, bool flag = false) -> Expr {
        return Expr(this, add(Node { .kind = kind, .a = check(value), .flag = flag }));
    }
    constexpr auto named(Kind kind, ref<str> name, ref<str> qualifier = {}) -> Expr {
        Node node { .kind = kind, .name = store(name) };
        if (! qualifier.is_empty()) node.qualifier = store(qualifier);
        return Expr(this, add(node));
    }
    constexpr auto bound_column(Id source_id, usize column_id) -> Expr {
        const auto& source = nodes_[source_id];
        auto        column = schemas_.column(column_id);
        return Expr(this,
                    add(Node { .kind         = Kind::Column,
                               .name         = store(column.name),
                               .qualifier    = source.qualifier.length != 0_usize ? source.qualifier
                                                                                  : source.name,
                               .type         = column.type,
                               .source       = source_id,
                               .registration = source.registration,
                               .table        = source.table,
                               .column       = column_id }));
    }
    constexpr auto copy_node(const QueryBuilder& source,
                             Id                  id,
                             array<Id, 512>&     nm,
                             array<Id, 128>&     qm,
                             usize               depth,
                             array<Id, 16>&      sm) -> Id {
        if (id == 0_usize) return 0_usize;
        if (depth >= limits_.depth) {
            fail(ErrorKind::Depth);
            return 0_usize;
        }
        if (nm[id] != 0_usize) return nm[id];
        Node node      = source.nodes_[id];
        node.name      = store(source.text(node.name), true);
        node.qualifier = store(source.text(node.qualifier), true);
        if (node.registration != 0_usize) {
            usize old = node.registration - 1_usize;
            if (sm[old] == 0_usize) {
                auto imported = schemas_.import(source.schemas_, old);
                if (imported.is_err()) {
                    fail(ErrorKind::Capacity, 0_usize, "schemas"_str);
                    return 0_usize;
                }
                sm[old] = imported.unwrap() + 1_usize;
            }
            node.registration = sm[old];
            node.table =
                schemas_.remap_table(source.schemas_, node.table, old, node.registration - 1_usize);
            if (node.kind == Kind::Column)
                node.column = schemas_.remap_column(
                    source.schemas_, node.column, old, node.registration - 1_usize);
        }
        node.source = copy_node(source, node.source, nm, qm, depth + 1_usize, sm);
        switch (node.kind) {
        case Kind::Derived:
        case Kind::Scalar:
        case Kind::Exists:
        case Kind::CteDefinition:
            node.a = copy_query(source, node.a, nm, qm, depth + 1_usize, sm);
            break;
        case Kind::InQuery:
            node.a = copy_node(source, node.a, nm, qm, depth + 1_usize, sm);
            node.b = copy_query(source, node.b, nm, qm, depth + 1_usize, sm);
            break;
        default:
            node.a = copy_node(source, node.a, nm, qm, depth + 1_usize, sm);
            node.b = copy_node(source, node.b, nm, qm, depth + 1_usize, sm);
            break;
        }
        return nm[id] = add(node);
    }
    constexpr auto copy_query(const QueryBuilder& source,
                              Id                  id,
                              array<Id, 512>&     nm,
                              array<Id, 128>&     qm,
                              usize               depth,
                              array<Id, 16>&      sm) -> Id {
        if (id == 0_usize || depth >= limits_.depth) {
            fail(ErrorKind::Depth);
            return 0_usize;
        }
        if (qm[id] != 0_usize) return qm[id];
        auto data = source.queries_[id];
        for (auto& slot : data.slots) slot = copy_node(source, slot, nm, qm, depth + 1_usize, sm);
        return qm[id] = add_query(data).id_;
    }

public:
    constexpr explicit QueryBuilder(Limits limits = {})
        : schemas_(limits.schema, limits.registrations), limits_(limits) {
        if (limits.nodes > nodes_.len() || limits.nodes < 2_usize ||
            limits.queries > queries_.len() || limits.queries < 2_usize ||
            limits.text > text_.len() || limits.sql > 16384_usize || limits.parameters > 64_usize ||
            limits.depth > 128_usize || limits.depth == 0_usize ||
            limits.registrations > 16_usize || ! schema::valid_limits(limits.schema))
            fail(ErrorKind::Capacity, 0_usize, "limits"_str);
        if (error_.kind != ErrorKind::None) limits_ = Limits {};
    }
    QueryBuilder(const QueryBuilder&)                              = delete;
    QueryBuilder(QueryBuilder&&)                                   = delete;
    auto           operator=(const QueryBuilder&) -> QueryBuilder& = delete;
    auto           operator=(QueryBuilder&&) -> QueryBuilder&      = delete;
    constexpr auto error() const -> Error { return error_; }
    constexpr auto use(const schema::Schema& schema) -> BoundSchema {
        if (error_.kind != ErrorKind::None) return {};
        auto registered = schemas_.use(schema);
        if (registered.is_err()) {
            fail(ErrorKind::Capacity, 0_usize, "schemas"_str);
            return {};
        }
        return BoundSchema(this, registered.unwrap() + 1_usize);
    }
    constexpr auto on(BoundForeignKey fk, BoundTable source, BoundTable target) -> Expr {
        if (! fk.owner_ || ! source.owner_ || ! target.owner_) {
            fail(ErrorKind::InvalidHandle);
            return {};
        }
        if (fk.owner_ != this || source.owner_ != this || target.owner_ != this) {
            fail(ErrorKind::ForeignOwner);
            return {};
        }
        auto f   = schemas_.foreign_key(fk.id_);
        auto key = schemas_.key(f.target);
        auto a = nodes_[source.source_], b = nodes_[target.source_];
        if (a.registration != fk.registration_ || b.registration != fk.registration_ ||
            a.table != f.table || b.table != key.table || source.source_ == target.source_) {
            fail(ErrorKind::InvalidRelation, 0_usize, f.name);
            return {};
        }
        Expr result;
        for (usize i = 0_usize; i < f.columns.len(); ++i) {
            auto condition =
                bound_column(source.source_, f.columns[i])
                    .binary(Binary::Equal, bound_column(target.source_, key.columns[i]));
            result = i != 0_usize ? result.binary(Binary::And, condition) : condition;
        }
        return result;
    }
    constexpr auto col(ref<str> name) -> Expr { return named(Kind::Column, name); }
    constexpr auto col(ref<str> qualifier, ref<str> name) -> Expr {
        return named(Kind::Column, name, qualifier);
    }
    // Only the current INSERT's conflict-update RHS may use this expression.
    constexpr auto excluded(Expr column) -> Expr {
        auto id = check(column);
        if (nodes_[id].kind != Kind::Column)
            fail(ErrorKind::InvalidShape, id, "excluded column"_str);
        return Expr(this, add(Node { .kind = Kind::Excluded, .a = id }));
    }
    constexpr auto table(ref<str> name) -> Expr { return named(Kind::Table, name); }
    constexpr auto cte(ref<str> name) -> Expr { return named(Kind::Cte, name); }
    constexpr auto star(ref<str> qualifier = {}) -> Expr {
        return Expr(this, add(Node { .kind = Kind::Star, .qualifier = store(qualifier, true) }));
    }
    template<class T>
    constexpr auto param(ref<str> name) -> Expr {
        if constexpr (requires { ParameterTraits<T>::type; })
            return Expr(this,
                        add(Node { .kind = Kind::Parameter,
                                   .name = store(name),
                                   .type = ParameterTraits<T>::type }));
        else {
            fail(ErrorKind::UnsupportedType, 0_usize, name);
            return {};
        }
    }
    template<auto Member, class T>
        requires requires { schema::column_name<Member>(); }
    constexpr auto member_param(ref<str> name) -> Expr {
        if constexpr (requires { ParameterTraits<T>::type; })
            return member_param<Member>(name, ParameterTraits<T>::type);
        else {
            fail(ErrorKind::UnsupportedType, 0_usize, name);
            return {};
        }
    }
    template<auto Member>
        requires requires { schema::column_name<Member>(); }
    constexpr auto member_param(ref<str> name, ParameterType type) -> Expr {
        if (! valid_type(type)) {
            fail(ErrorKind::UnsupportedType, 0_usize, name);
            return {};
        }
        return Expr(this,
                    add(Node { .kind            = Kind::Parameter,
                               .name            = store(name),
                               .type            = type,
                               .member_identity = &member_parameter_identity<Member> }));
    }
    // Groups sharing a name must have the same type and count. Scalar parameters
    // and groups cannot share a name.
    template<class T>
    constexpr auto param_group(ref<str> name, rstd::usize count) -> ParameterGroup {
        if constexpr (requires { ParameterTraits<T>::type; })
            return param_group(name, count, ParameterTraits<T>::type);
        else {
            fail(ErrorKind::UnsupportedType, 0_usize, name);
            return {};
        }
    }
    constexpr auto param_group(ref<str> name, rstd::usize count, ParameterType type)
        -> ParameterGroup {
        if (! valid_type(type)) {
            fail(ErrorKind::UnsupportedType, 0_usize, name);
            return {};
        }
        auto size = count.to_primitive();
        if (size > 2147483647ULL) {
            fail(ErrorKind::Capacity, 0_usize, "parameters"_str);
            return {};
        }
        return ParameterGroup(this,
                              add(Node { .kind    = Kind::ParameterGroup,
                                         .name    = store(name),
                                         .integer = static_cast<long long>(size),
                                         .type    = type }));
    }
    template<auto Member>
        requires requires { schema::column_name<Member>(); }
    constexpr auto member_param_group(ref<str> name, rstd::usize count, ParameterType type)
        -> ParameterGroup {
        auto result = param_group(name, count, type);
        if (result.owner_ == this && result.id_ != 0_usize)
            nodes_[result.id_].member_identity = &member_parameter_identity<Member>;
        return result;
    }
    constexpr auto integer(long long value) -> Expr {
        return Expr(this, add(Node { .kind = Kind::Integer, .integer = value }));
    }
    constexpr auto text_value(ref<str> value) -> Expr {
        return Expr(this, add(Node { .kind = Kind::Text, .name = store(value, true) }));
    }
    constexpr auto null() -> Expr { return Expr(this, add(Node { .kind = Kind::Null })); }
    constexpr auto function(Function function, slice<Expr> args) -> Expr {
        return Expr(this,
                    add(Node { .kind      = Kind::Function,
                               .a         = list(args),
                               .operation = rstd::usize(static_cast<rstd::size_t>(function)) }));
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto function(Function f, T... args) -> Expr {
        const array<Expr, sizeof...(T)> values { args... };
        return function(f, values.as_slice());
    }
    // Only SELECT projection scalar expressions may contain this window call.
    // The nonempty ordering list accepts ordinary expressions and asc()/desc().
    constexpr auto row_number(slice<Expr> ordering) -> Expr {
        if (ordering.is_empty())
            fail(ErrorKind::InvalidShape, 0_usize, "empty window ordering"_str);
        return Expr(this, add(Node { .kind = Kind::RowNumber, .a = list(ordering) }));
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto row_number(T... ordering) -> Expr {
        const array<Expr, sizeof...(T)> list { ordering... };
        return row_number(list.as_slice());
    }
    constexpr auto select(slice<Expr> values) -> Query {
        QueryData data;
        data.get(Slot::Projection) = list(values);
        return add_query(data);
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto select(T... args) -> Query {
        const array<Expr, sizeof...(T)> values { args... };
        return select(values.as_slice());
    }
    // A standalone VALUES query for CTEs and derived sources; SQLite names its
    // output columns column1, column2, and so on. Requires runtime expansion.
    constexpr auto values_groups(slice<ParameterGroup> columns) -> Query {
        return add_query(QueryData { .kind = StatementKind::Values }).values_groups(columns);
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, ParameterGroup>) && ...)
    constexpr auto values_groups(T... columns) -> Query {
        const array<ParameterGroup, sizeof...(T)> groups { columns... };
        return values_groups(groups.as_slice());
    }
    constexpr auto insert_into(Expr table, slice<Expr> columns) -> Query {
        QueryData data { .kind = StatementKind::Insert };
        data.get(Slot::Source)     = check(table);
        data.get(Slot::Projection) = list(columns);
        return add_query(data);
    }
    constexpr auto insert_default_values(Expr table) -> Query {
        QueryData data { .kind = StatementKind::Insert, .default_values = true };
        data.get(Slot::Source) = check(table);
        return add_query(data);
    }
    template<class... T>
        requires((rstd::mtp::same_as<T, Expr>) && ...)
    constexpr auto insert_into(Expr table, T... columns) -> Query {
        const array<Expr, sizeof...(T)> values { columns... };
        return insert_into(table, values.as_slice());
    }
    constexpr auto update(Expr table) -> Query {
        QueryData data { .kind = StatementKind::Update };
        data.get(Slot::Source) = check(table);
        return add_query(data);
    }
    constexpr auto delete_from(Expr table) -> Query {
        QueryData data { .kind = StatementKind::Delete };
        data.get(Slot::Source) = check(table);
        return add_query(data);
    }
    constexpr auto scalar(Query query) -> Expr {
        return Expr(this, add(Node { .kind = Kind::Scalar, .a = check(query) }));
    }
    constexpr auto exists(Query query) -> Expr {
        return Expr(this, add(Node { .kind = Kind::Exists, .a = check(query) }));
    }
    constexpr auto import_query(Query query) -> Query {
        if (error_.kind != ErrorKind::None) return {};
        if (! query.owner_ || query.id_ == 0_usize) {
            fail(ErrorKind::InvalidHandle);
            return {};
        }
        if (query.owner_->error_.kind != ErrorKind::None) {
            error_ = query.owner_->error_;
            return {};
        }
        array<Id, 512> nodes {};
        array<Id, 128> queries {};
        array<Id, 16>  schemas {};
        return Query(this, copy_query(*query.owner_, query.id_, nodes, queries, 0_usize, schemas));
    }
};

constexpr auto BoundSchema::table(ref<str> name, ref<str> alias) const -> BoundTable {
    if (! owner_) return {};
    auto id = owner_->schemas_.find_table(registration_ - 1_usize, name);
    if (id.is_none()) {
        owner_->fail(ErrorKind::UnknownTable, 0_usize, name);
        return {};
    }
    Node n { .kind         = Kind::Table,
             .name         = owner_->store(owner_->schemas_.table(*id).name),
             .registration = registration_,
             .table        = *id };
    if (! alias.is_empty()) n.qualifier = owner_->store(alias);
    return BoundTable(owner_, owner_->add(n));
}
template<class Model>
constexpr auto BoundSchema::table(ref<str> alias) const -> TypedBoundTable<Model> {
    using Definition = typename ModelGroup<Model>::definition;
    auto bound       = table(Definition::name.view(), alias);
    if (! owner_ || bound.source_ == 0_usize) return {};
    auto table_id = owner_->nodes_[bound.source_].table;
    bool valid    = true;
    [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
        auto check = [&]<class Field>() {
            auto id = owner_->schemas_.find_column(table_id, Field::name.view());
            if (id.is_none()) {
                owner_->fail(ErrorKind::UnknownColumn, bound.source_, Field::name.view());
                valid = false;
            } else if (owner_->schemas_.column(*id).type != Field::type) {
                owner_->fail(ErrorKind::TypeMismatch, bound.source_, Field::name.view());
                valid = false;
            }
        };
        (check.template operator()<typename Definition::template field<I>>(), ...);
    }(rstd::mtp::make_index_sequence<Definition::field_count> {});
    return valid ? TypedBoundTable<Model>(bound) : TypedBoundTable<Model>();
}
constexpr auto BoundSchema::foreign_key(ref<str> name) const -> BoundForeignKey {
    if (! owner_) return {};
    auto id = owner_->schemas_.find_foreign_key(registration_ - 1_usize, name);
    if (id.is_none()) {
        owner_->fail(ErrorKind::InvalidRelation, 0_usize, name);
        return {};
    }
    return BoundForeignKey(owner_, registration_, *id);
}
constexpr auto BoundTable::source() const -> Expr {
    return owner_ ? Expr(owner_, source_) : Expr {};
}
constexpr auto BoundTable::col(ref<str> name) const -> Expr {
    if (! owner_ || source_ == 0_usize) return {};
    auto source = owner_->nodes_[source_];
    auto id     = owner_->schemas_.find_column(source.table, name);
    if (id.is_none()) {
        owner_->fail(ErrorKind::UnknownColumn, source_, name);
        return {};
    }
    return owner_->bound_column(source_, *id);
}
constexpr auto Expr::binary(Binary op, Expr other) const -> Expr {
    if (! owner_) return {};
    return Expr(owner_,
                owner_->add(Node { .kind      = Kind::Binary,
                                   .a         = owner_->check(*this),
                                   .b         = owner_->check(other),
                                   .operation = rstd::usize(static_cast<rstd::size_t>(op)) }));
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
    return owner_ ? owner_->unary(Kind::Not, *this) : Expr {};
}
constexpr auto Expr::is_null() const -> Expr {
    return owner_ ? owner_->unary(Kind::IsNull, *this) : Expr {};
}
constexpr auto Expr::is_not_null() const -> Expr {
    return owner_ ? owner_->unary(Kind::IsNotNull, *this) : Expr {};
}
constexpr auto Expr::asc() const -> Expr {
    return owner_ ? owner_->unary(Kind::Order, *this) : Expr {};
}
constexpr auto Expr::desc() const -> Expr {
    return owner_ ? owner_->unary(Kind::Order, *this, true) : Expr {};
}
constexpr auto Expr::like(Expr other) const -> Expr {
    return binary(Binary::Like, other);
}
constexpr auto Expr::match(Expr other) const -> Expr {
    return binary(Binary::Match, other);
}
constexpr auto Expr::as(ref<str> name) const -> Expr {
    if (! owner_) return {};
    return Expr(owner_,
                owner_->add(Node {
                    .kind = Kind::Alias, .a = owner_->check(*this), .name = owner_->store(name) }));
}
constexpr auto Expr::in(slice<Expr> values) const -> Expr {
    if (! owner_) return {};
    return Expr(owner_,
                owner_->add(Node {
                    .kind = Kind::In, .a = owner_->check(*this), .b = owner_->list(values) }));
}
constexpr auto Expr::in(ParameterGroup values) const -> Expr {
    if (! owner_) return {};
    return Expr(
        owner_,
        owner_->add(
            Node { .kind = Kind::InGroup, .a = owner_->check(*this), .b = owner_->check(values) }));
}
constexpr auto Expr::in(Query query) const -> Expr {
    if (! owner_) return {};
    return Expr(owner_,
                owner_->add(Node {
                    .kind = Kind::InQuery, .a = owner_->check(*this), .b = owner_->check(query) }));
}
constexpr auto Expr::set(Expr value) const -> Expr {
    if (! owner_) return {};
    return Expr(owner_,
                owner_->add(Node { .kind = Kind::Assignment,
                                   .a    = owner_->check(*this),
                                   .b    = owner_->check(value) }));
}
constexpr auto Query::from(Expr x) const -> Query {
    if (! owner_) return {};
    auto slot = owner_->queries_[owner_->check(*this)].kind == StatementKind::Update
                    ? Slot::UpdateFrom
                    : Slot::Source;
    return owner_->change(*this, slot, owner_->check(x));
}
constexpr auto Query::where(Expr x) const -> Query {
    return owner_ ? owner_->change(*this, Slot::Where, owner_->check(x)) : Query {};
}
constexpr auto Query::having(Expr x) const -> Query {
    return owner_ ? owner_->change(*this, Slot::Having, owner_->check(x)) : Query {};
}
constexpr auto Query::limit(Expr x) const -> Query {
    return owner_ ? owner_->change(*this, Slot::Limit, owner_->check(x)) : Query {};
}
constexpr auto Query::offset(Expr x) const -> Query {
    return owner_ ? owner_->change(*this, Slot::Offset, owner_->check(x)) : Query {};
}
constexpr auto Query::group_by(slice<Expr> x) const -> Query {
    if (owner_ && x.is_empty()) owner_->fail(ErrorKind::InvalidShape, id_, "empty GROUP BY"_str);
    return owner_ ? owner_->change(*this, Slot::Group, owner_->list(x)) : Query {};
}
constexpr auto Query::order_by(slice<Expr> x) const -> Query {
    if (owner_ && x.is_empty()) owner_->fail(ErrorKind::InvalidShape, id_, "empty ORDER BY"_str);
    return owner_ ? owner_->change(*this, Slot::Order, owner_->list(x)) : Query {};
}
constexpr auto Query::values(slice<Expr> x) const -> Query {
    if (owner_ && x.is_empty()) owner_->fail(ErrorKind::InvalidShape, id_, "empty VALUES"_str);
    return owner_ ? owner_->change(*this, Slot::Values, owner_->list(x)) : Query {};
}
constexpr auto Query::values_groups(slice<ParameterGroup> columns) const -> Query {
    if (! owner_) return {};
    if (columns.is_empty()) owner_->fail(ErrorKind::InvalidShape, id_, "empty VALUES groups"_str);
    auto groups = owner_->add(Node { .kind = Kind::ValuesGroups, .a = owner_->list(columns) });
    return owner_->change(*this, Slot::Values, groups);
}
constexpr auto Query::set(slice<Expr> x) const -> Query {
    if (owner_ && x.is_empty()) owner_->fail(ErrorKind::InvalidShape, id_, "empty SET"_str);
    return owner_ ? owner_->change(*this, Slot::Sets, owner_->list(x)) : Query {};
}
constexpr auto Query::on_conflict_do_nothing(slice<Expr> columns) const -> Query {
    if (owner_ && columns.is_empty())
        owner_->fail(ErrorKind::InvalidShape, id_, "empty conflict target"_str);
    return owner_ ? owner_->change(*this, Slot::Conflict, owner_->list(columns)) : Query {};
}
constexpr auto Query::on_conflict_do_update(slice<Expr> columns, slice<Expr> assignments) const
    -> Query {
    if (! owner_) return {};
    if (columns.is_empty()) owner_->fail(ErrorKind::InvalidShape, id_, "empty conflict target"_str);
    if (assignments.is_empty())
        owner_->fail(ErrorKind::InvalidShape, id_, "empty conflict SET"_str);
    auto data = owner_->queries_[owner_->check(*this)];
    if (data.get(Slot::Conflict) != 0_usize || data.get(Slot::ConflictSets) != 0_usize)
        owner_->fail(ErrorKind::DuplicateClause, id_);
    data.get(Slot::Conflict)     = owner_->list(columns);
    data.get(Slot::ConflictSets) = owner_->list(assignments);
    return owner_->add_query(data);
}
constexpr auto Query::returning(slice<Expr> columns) const -> Query {
    if (owner_ && columns.is_empty())
        owner_->fail(ErrorKind::InvalidShape, id_, "empty RETURNING"_str);
    return owner_ ? owner_->change(*this, Slot::Returning, owner_->list(columns)) : Query {};
}
constexpr auto Query::distinct() const -> Query {
    if (! owner_) return {};
    auto data = owner_->queries_[owner_->check(*this)];
    if (data.distinct) owner_->fail(ErrorKind::DuplicateClause);
    data.distinct = true;
    return owner_->add_query(data);
}
constexpr auto Query::allow_all() const -> Query {
    if (! owner_) return {};
    auto data      = owner_->queries_[owner_->check(*this)];
    data.allow_all = true;
    return owner_->add_query(data);
}
constexpr auto Query::or_replace() const -> Query {
    if (! owner_) return {};
    auto data = owner_->queries_[owner_->check(*this)];
    if (data.kind != StatementKind::Insert)
        owner_->fail(ErrorKind::InvalidShape, id_, "INSERT OR REPLACE"_str);
    if (data.or_replace) owner_->fail(ErrorKind::DuplicateClause, id_, "OR REPLACE"_str);
    data.or_replace = true;
    return owner_->add_query(data);
}
constexpr auto Query::join(Expr source, Expr on) const -> Query {
    if (! owner_) return {};
    auto join = owner_->add(
        Node { .kind = Kind::Join, .a = owner_->check(source), .b = owner_->check(on) });
    return owner_->change(*this, Slot::Joins, join, true);
}
constexpr auto Query::left_join(Expr source, Expr on) const -> Query {
    if (! owner_) return {};
    auto join = owner_->add(Node {
        .kind = Kind::Join, .a = owner_->check(source), .b = owner_->check(on), .flag = true });
    return owner_->change(*this, Slot::Joins, join, true);
}
constexpr auto Query::with(ref<str> name, Query body) const -> Query {
    if (! owner_) return {};
    auto node = owner_->add(Node {
        .kind = Kind::CteDefinition, .a = owner_->check(body), .name = owner_->store(name) });
    return owner_->change(*this, Slot::Ctes, node, true);
}
constexpr auto Query::as(ref<str> name) const -> Expr {
    if (! owner_) return {};
    return Expr(owner_,
                owner_->add(Node { .kind = Kind::Derived,
                                   .a    = owner_->check(*this),
                                   .name = owner_->store(name) }));
}
} // namespace liorm::query
