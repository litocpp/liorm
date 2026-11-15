module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>
export module liorm.sqlito;
export import liorm;
export import sqlito;
import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace liorm
{
template<>
struct ValueTraits<u64> : ValueTraits<i64> {};
template<>
struct ValueTraits<Vec<u8>> : ValueTraits<BlobParameter> {};
template<>
struct ValueTraits<slice<u8>> : ValueTraits<BlobParameter> {};
template<>
struct ValueTraits<ref<str>> : ValueTraits<TextParameter> {};
} // namespace liorm

export namespace liorm::sqlito
{
using namespace rstd::prelude;
using namespace rstd::literals;

template<class T>
struct Codec;

template<class T>
auto codec_null(const T& value) -> Option<bool>
    requires requires { Codec<T>::parameter_type(); }
{
    if (! Codec<T>::parameter_type().nullable) return Some(false);
    if constexpr (requires { Codec<T>::is_null(value); }) {
        using Status = rstd::mtp::rm_cvf<decltype(Codec<T>::is_null(value))>;
        if constexpr (rstd::mtp::same_as<Status, bool>)
            return Some(Codec<T>::is_null(value));
        else if constexpr (rstd::mtp::same_as<Status, Option<bool>>)
            return Codec<T>::is_null(value);
    }
    return None();
}

auto invalid_column() -> ::sqlito::Error {
    return ::sqlito::Error::from(
        ::sqlito::ErrorKind::Type, 0, String::make("invalid model column"_str));
}

template<>
struct Codec<i64> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return { ValueKind::Integer, false };
    }
    static auto read(::sqlito::Statement& statement, usize column) -> Result<i64, ::sqlito::Error> {
        auto value = rstd_try(statement.integer(column));
        if (value.is_none()) return Err(invalid_column());
        return Ok(*value);
    }
    static auto write(::sqlito::Statement& statement, usize slot, i64 value)
        -> Result<empty, ::sqlito::Error> {
        return statement.bind(slot, value);
    }
};

template<>
struct Codec<u64> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return ValueTraits<u64>::type;
    }
    static auto read(::sqlito::Statement& statement, usize column) -> Result<u64, ::sqlito::Error> {
        auto value = rstd_try(Codec<i64>::read(statement, column));
        return rstd::try_into<u64>(value).map_err([](auto) {
            return ::sqlito::Error::from(
                ::sqlito::ErrorKind::Range, 0, String::make("integer is out of range for u64"_str));
        });
    }
    static auto write(::sqlito::Statement& statement, usize slot, u64 value)
        -> Result<empty, ::sqlito::Error> {
        auto integer = rstd_try(rstd::try_into<i64>(value).map_err([](auto) {
            return ::sqlito::Error::from(::sqlito::ErrorKind::Range,
                                         0,
                                         String::make("u64 exceeds SQLite integer range"_str));
        }));
        return Codec<i64>::write(statement, slot, integer);
    }
};

template<>
struct Codec<i32> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return { ValueKind::Integer, false };
    }
    static auto read(::sqlito::Statement& statement, usize column) -> Result<i32, ::sqlito::Error> {
        auto value = rstd_try(Codec<i64>::read(statement, column));
        if (value.to_primitive() < rstd::i32::MIN.to_primitive() ||
            value.to_primitive() > rstd::i32::MAX.to_primitive())
            return Err(invalid_column());
        return Ok(i32(static_cast<int>(value.to_primitive())));
    }
    static auto write(::sqlito::Statement& statement, usize slot, i32 value)
        -> Result<empty, ::sqlito::Error> {
        return statement.bind(slot, i64(value.to_primitive()));
    }
};

template<>
struct Codec<bool> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return { ValueKind::Boolean, false };
    }
    static auto read(::sqlito::Statement& statement, usize column)
        -> Result<bool, ::sqlito::Error> {
        auto value = rstd_try(Codec<i64>::read(statement, column));
        return Ok(value != i64());
    }
    static auto write(::sqlito::Statement& statement, usize slot, bool value)
        -> Result<empty, ::sqlito::Error> {
        return Codec<i64>::write(statement, slot, i64(value ? 1 : 0));
    }
};

template<>
struct Codec<f64> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return { ValueKind::Real, false };
    }
    static auto read(::sqlito::Statement& statement, usize column) -> Result<f64, ::sqlito::Error> {
        auto value = rstd_try(statement.real(column));
        if (value.is_none()) return Err(invalid_column());
        return Ok(f64(*value));
    }
    static auto write(::sqlito::Statement& statement, usize slot, f64 value)
        -> Result<empty, ::sqlito::Error> {
        return statement.bind(slot, value.to_primitive());
    }
};

template<>
struct Codec<String> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return { ValueKind::Text, false };
    }
    static auto read(::sqlito::Statement& statement, usize column)
        -> Result<String, ::sqlito::Error> {
        auto value = rstd_try(statement.text(column));
        if (value.is_none()) return Err(invalid_column());
        return Ok(rstd::move(value).unwrap());
    }
    static auto write(::sqlito::Statement& statement, usize slot, const String& value)
        -> Result<empty, ::sqlito::Error> {
        return statement.bind_text(slot, value.as_str());
    }
};

template<>
struct Codec<ref<str>> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return ValueTraits<ref<str>>::type;
    }
    static auto write(::sqlito::Statement& statement, usize slot, ref<str> value)
        -> Result<empty, ::sqlito::Error> {
        return statement.bind_text(slot, value);
    }
};

template<>
struct Codec<slice<u8>> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return ValueTraits<slice<u8>>::type;
    }
    static auto write(::sqlito::Statement& statement, usize slot, slice<u8> value)
        -> Result<empty, ::sqlito::Error> {
        return statement.bind_blob(slot, value);
    }
};

template<>
struct Codec<Vec<u8>> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return ValueTraits<Vec<u8>>::type;
    }
    static auto read(::sqlito::Statement& statement, usize column)
        -> Result<Vec<u8>, ::sqlito::Error> {
        auto value = rstd_try(statement.blob(column));
        if (value.is_none()) return Err(invalid_column());
        return Ok(rstd::move(value).unwrap());
    }
    static auto write(::sqlito::Statement& statement, usize slot, const Vec<u8>& value)
        -> Result<empty, ::sqlito::Error> {
        return Codec<slice<u8>>::write(statement, slot, value.as_slice());
    }
};

template<class T>
struct Codec<Option<T>> {
    static auto is_null(const Option<T>& value) -> Option<bool>
        requires requires { Codec<T>::parameter_type(); }
    {
        if (value.is_none()) return Some(true);
        return codec_null(*value);
    }
    static constexpr auto parameter_type() -> query::ParameterType
        requires requires { Codec<T>::parameter_type(); }
    {
        auto type     = Codec<T>::parameter_type();
        type.nullable = true;
        return type;
    }
    static auto read(::sqlito::Statement& statement, usize column)
        -> Result<Option<T>, ::sqlito::Error> {
        auto type = rstd_try(statement.column_type(column));
        if (type == ::sqlito::ColumnType::Null) return Ok<Option<T>>(None());
        return Ok(Some(rstd_try(Codec<T>::read(statement, column))));
    }
    static auto write(::sqlito::Statement& statement, usize slot, const Option<T>& value)
        -> Result<empty, ::sqlito::Error>
        requires requires(const T& item) { Codec<T>::write(statement, slot, item); }
    {
        if (value.is_none()) return statement.bind_null(slot);
        return Codec<T>::write(statement, slot, *value);
    }
};

template<class T>
concept sqlite_enum = requires(T* value) { liorm_sqlite_enum_values(value); };

template<class T>
    requires sqlite_enum<T>
struct Codec<T> {
    static constexpr auto parameter_type() -> query::ParameterType {
        return Codec<i64>::parameter_type();
    }
    static auto read(::sqlito::Statement& statement, usize column) -> Result<T, ::sqlito::Error> {
        auto value = rstd_try(Codec<i64>::read(statement, column));
        for (auto candidate : liorm_sqlite_enum_values(static_cast<T*>(nullptr)))
            if (i64(static_cast<long long>(candidate)) == value) return Ok(candidate);
        return Err(invalid_column());
    }
    static auto write(::sqlito::Statement& statement, usize slot, T value)
        -> Result<empty, ::sqlito::Error> {
        for (auto candidate : liorm_sqlite_enum_values(static_cast<T*>(nullptr)))
            if (candidate == value)
                return Codec<i64>::write(statement, slot, i64(static_cast<long long>(value)));
        return Err(invalid_column());
    }
};

template<class V>
class ExpectedReader {
    ::sqlito::Statement& statement_;
    usize                offset_;
    Option<V>            expected_;

public:
    using error_type = ::sqlito::Error;
    ExpectedReader(::sqlito::Statement& statement, usize offset, Option<V> expected)
        : statement_(statement), offset_(offset), expected_(rstd::move(expected)) {}

    template<class T>
    auto read(usize column) -> Result<T, error_type> {
        auto value = rstd_try(Codec<T>::read(statement_, offset_ + column));
        if constexpr (rstd::mtp::same_as<T, V>) {
            if (expected_.is_some() && value != *expected_) return Err(invalid_column());
        }
        return Ok(rstd::move(value));
    }
};

class Reader {
    ::sqlito::Statement& statement_;

public:
    using error_type = ::sqlito::Error;
    explicit Reader(::sqlito::Statement& statement): statement_(statement) {}
    template<class T>
    auto read(usize column) -> Result<T, error_type> {
        return Codec<T>::read(statement_, column);
    }
};

template<class Query>
class PreparedQuery;
class ReadError {
    RSTD_ENUM(ReadError,
              (Storage, (::sqlito::Error error;)),
              (Binding, (liorm::BindError<::sqlito::Error> error;)))
};
class Writer;
template<class Model, class Policy = Writer>
class PreparedWrite;

class Writer {
    template<class Query>
    friend class PreparedQuery;
    template<class Model, class Policy>
    friend class PreparedWrite;
    ::sqlito::Statement& statement_;
    explicit Writer(::sqlito::Statement& statement): statement_(statement) {}

public:
    using error_type = ::sqlito::Error;
    template<class T>
    static consteval auto supports() -> bool {
        return requires(::sqlito::Statement& statement, usize slot, const T& value) {
            Codec<T>::parameter_type();
            Codec<T>::write(statement, slot, value);
        };
    }
    template<class T>
    static constexpr auto compatible(query::ParameterType target) -> bool {
        if constexpr (supports<T>()) {
            auto source = Codec<T>::parameter_type();
            return source.kind == target.kind && (! source.nullable || target.nullable);
        } else {
            return false;
        }
    }
    template<class T>
    static constexpr auto parameter_type() -> query::ParameterType
        requires(supports<T>())
    {
        return Codec<T>::parameter_type();
    }
    template<class T>
    static auto key_is_null(const T& value) -> Option<bool>
        requires(supports<T>())
    {
        return codec_null(value);
    }
    template<class T>
    auto write(usize slot, const T& value) -> Result<empty, error_type>
        requires(supports<T>())
    {
        return Codec<T>::write(statement_, slot, value);
    }
};

// The adapter adds only error/lifetime construction requirements to the core
// writer protocol. Field support and parameter metadata remain Policy-owned.
template<class Policy>
concept WritePolicy = requires { typename Policy::error_type; } &&
                      rstd::mtp::same_as<typename Policy::error_type, ::sqlito::Error> &&
                      rstd::mtp::init<Policy, Writer&>;

template<const query::CompiledQuery& Query>
struct StaticQuery {
    static constexpr const auto& definition = Query;
};

template<class Query>
class PreparedQuery {
    Query               query_;
    ::sqlito::Statement statement_;
    PreparedQuery(Query query, ::sqlito::Statement statement)
        : query_(rstd::move(query)), statement_(rstd::move(statement)) {}

public:
    PreparedQuery(const PreparedQuery&)                    = delete;
    auto operator=(const PreparedQuery&) -> PreparedQuery& = delete;
    PreparedQuery(PreparedQuery&&)                         = default;
    auto operator=(PreparedQuery&&) -> PreparedQuery&      = default;

    template<class Session>
    static auto create(Session& session, Query query) -> Result<PreparedQuery, ::sqlito::Error>
        requires((rstd::mtp::same_as<Query, liorm::query::CompiledQuery> ||
                  rstd::mtp::same_as<Query, liorm::query::ExpandedQuery>) &&
                 (rstd::mtp::same_as<Session, ::sqlito::Connection> ||
                  rstd::mtp::same_as<Session, ::sqlito::Transaction>))
    {
        auto statement = session.prepare(query.sql());
        if (statement.is_err()) return Err(rstd::move(statement).unwrap_err());
        return Ok(PreparedQuery(rstd::move(query), rstd::move(statement).unwrap()));
    }

    template<const query::CompiledQuery& StaticQuery, class Session, class... Fields>
    static auto create(Session& session, liorm::Parameters<Fields...> parameters)
        -> Result<PreparedQuery, ReadError>
        requires rstd::mtp::same_as<Query, liorm::sqlito::StaticQuery<StaticQuery>> &&
                 (rstd::mtp::same_as<Session, ::sqlito::Connection> ||
                  rstd::mtp::same_as<Session, ::sqlito::Transaction>)
    {
        liorm::Parameters<Fields...>::template validate<StaticQuery, Writer>();
        auto statement = session.prepare(StaticQuery.sql());
        if (statement.is_err()) return Err(ReadError::Storage(rstd::move(statement).unwrap_err()));
        auto   owned = rstd::move(statement).unwrap();
        Writer writer(owned);
        auto   bound = parameters.template bind<StaticQuery>(writer);
        if (bound.is_err()) return Err(ReadError::Binding(rstd::move(bound).unwrap_err()));
        return Ok(PreparedQuery(Query {}, rstd::move(owned)));
    }

    auto query() const& -> decltype(auto) {
        if constexpr (requires { Query::definition; })
            return (Query::definition);
        else
            return (query_);
    }
    auto query() const&& = delete;
    template<class... Fields>
    auto bind(liorm::Parameters<Fields...> parameters)
        -> Result<empty, liorm::BindError<::sqlito::Error>>
        requires requires { Query::definition; }
    {
        Writer writer(statement_);
        return parameters.template bind<Query::definition>(writer);
    }
    template<class Model>
    auto bind_model(const Model& model) -> Result<empty, liorm::BindError<::sqlito::Error>> {
        Writer writer(statement_);
        return liorm::bind_model(query(), writer, model);
    }
    template<class Model>
    auto bind_key(liorm::Key<Model> key) -> Result<empty, liorm::BindError<::sqlito::Error>> {
        auto   values = rstd::move(key).into_active();
        Writer writer(statement_);
        return liorm::bind_values<Model>(query(), writer, liorm::ActiveValues<Model> { values });
    }
    template<class T>
    auto bind(ref<str> name, const T& value) -> Result<empty, liorm::BindError<::sqlito::Error>> {
        Writer writer(statement_);
        return liorm::bind_parameter(query(), writer, name, value);
    }
    template<class T>
    auto bind_group(ref<str> name, slice<T> values)
        -> Result<empty, liorm::BindError<::sqlito::Error>> {
        return bind_group_with(name, values.len(), [values](usize index) -> decltype(auto) {
            return values[usize(index)];
        });
    }
    template<class Getter>
    auto bind_group_with(ref<str> name, usize count, Getter getter)
        -> Result<empty, liorm::BindError<::sqlito::Error>> {
        Writer writer(statement_);
        return liorm::bind_group_values(query(), writer, name, count, rstd::move(getter));
    }
    template<class T>
    auto bind_repeat(ref<str> name, const T& value)
        -> Result<empty, liorm::BindError<::sqlito::Error>> {
        Writer writer(statement_);
        return liorm::bind_repeated(query(), writer, name, value);
    }
    template<class Shape, class GetRow>
    auto bind_rows(const Shape& shape, usize count, GetRow get_row)
        -> Result<empty, liorm::BindError<::sqlito::Error>> {
        Writer writer(statement_);
        return shape.bind(query(), writer, count, rstd::move(get_row));
    }
    template<class T>
    auto bind(usize slot, const T& value) -> Result<empty, ::sqlito::Error>
        requires(Writer::template supports<T>())
    {
        return Codec<T>::write(statement_, slot, value);
    }
    auto bind_text(usize slot, ref<str> value) -> Result<empty, ::sqlito::Error> {
        return statement_.bind_text(slot, value);
    }
    auto bind_blob(usize slot, slice<u8> value) -> Result<empty, ::sqlito::Error> {
        return statement_.bind_blob(slot, value);
    }
    auto bind_null(usize slot) -> Result<empty, ::sqlito::Error> {
        return statement_.bind_null(slot);
    }
    auto step() -> Result<::sqlito::Step, ::sqlito::Error> { return statement_.step(); }
    auto reset() -> Result<empty, ::sqlito::Error> { return statement_.reset(); }
    // The reader borrows this prepared query and cannot outlive it.
    auto reader() & -> Reader { return Reader(statement_); }
    auto reader() && -> Reader = delete;
};

template<class Session, class Query>
auto prepare(Session& session, Query query) -> Result<PreparedQuery<Query>, ::sqlito::Error>
    requires((rstd::mtp::same_as<Query, liorm::query::CompiledQuery> ||
              rstd::mtp::same_as<Query, liorm::query::ExpandedQuery>) &&
             (rstd::mtp::same_as<Session, ::sqlito::Connection> ||
              rstd::mtp::same_as<Session, ::sqlito::Transaction>))
{
    return PreparedQuery<Query>::create(session, rstd::move(query));
}

// Terminals never finish the caller's transaction. A failure leaves
// recovery/reset to the caller.
template<class Prepared>
auto discard(Prepared& statement) -> Result<empty, ::sqlito::Error> {
    while (rstd_try(statement.step()) == ::sqlito::Step::Row) {
    }
    return Ok(empty {});
}

enum class RowConsumption
{
    Exact,
    First,
    Drain
};
} // namespace liorm::sqlito

namespace liorm::sqlito
{
template<class Prepared>
auto finish_row(Prepared& statement, RowConsumption consumption, ref<str> cardinality)
    -> Result<empty, ::sqlito::Error> {
    if (consumption == RowConsumption::First) return Ok(empty {});
    bool extra = false;
    while (rstd_try(statement.step()) == ::sqlito::Step::Row) extra = true;
    if (extra && consumption == RowConsumption::Exact)
        return Err(::sqlito::Error::from(::sqlito::ErrorKind::Range, 0, String::make(cardinality)));
    return Ok(empty {});
}
} // namespace liorm::sqlito

// Exact preserves strict cardinality. First leaves later rows untouched;
// Drain consumes them without decoding or rejecting additional rows.
export namespace liorm::sqlito
{
template<class T, class Prepared>
    requires requires(::sqlito::Statement& statement, usize column) {
        Codec<T>::read(statement, column);
    }
auto scalar(Prepared&      statement,
            usize          column      = usize(),
            RowConsumption consumption = RowConsumption::Exact) -> Result<T, ::sqlito::Error> {
    if (rstd_try(statement.step()) == ::sqlito::Step::Done)
        return Err(::sqlito::Error::from(
            ::sqlito::ErrorKind::Range, 0, String::make("expected exactly one row"_str)));
    auto reader = statement.reader();
    auto result = rstd_try(reader.template read<T>(column));
    rstd_try(finish_row(statement, consumption, "expected exactly one row"_str));
    return Ok(rstd::move(result));
}

template<class T, class Prepared>
    requires requires(::sqlito::Statement& statement, usize column) {
        Codec<T>::read(statement, column);
    }
auto optional_scalar(Prepared&      statement,
                     usize          column      = usize(),
                     RowConsumption consumption = RowConsumption::Exact)
    -> Result<Option<T>, ::sqlito::Error> {
    if (rstd_try(statement.step()) == ::sqlito::Step::Done) return Ok<Option<T>>(None());
    auto reader = statement.reader();
    auto result = rstd_try(reader.template read<T>(column));
    rstd_try(finish_row(statement, consumption, "expected at most one row"_str));
    return Ok(Some(rstd::move(result)));
}

template<class Prepared, class Projection>
auto one_row(Prepared&         statement,
             const Projection& projection,
             RowConsumption    consumption = RowConsumption::Drain)
    -> Result<Option<typename Projection::value_type>,
              typename decltype(projection.decode(rstd::mtp::declval<Reader&>()))::error_type> {
    using Row = typename Projection::value_type;
    if (rstd_try(statement.step()) == ::sqlito::Step::Done) return Ok<Option<Row>>(None());
    auto reader = statement.reader();
    auto result = rstd_try(projection.decode(reader));
    rstd_try(finish_row(statement, consumption, "expected at most one row"_str));
    return Ok(Some(rstd::move(result)));
}

template<class Prepared, class Projection>
auto all_rows(Prepared& statement, const Projection& projection)
    -> Result<Vec<typename Projection::value_type>,
              typename decltype(projection.decode(rstd::mtp::declval<Reader&>()))::error_type> {
    Vec<typename Projection::value_type> result;
    while (rstd_try(statement.step()) == ::sqlito::Step::Row) {
        auto reader = statement.reader();
        result.push(rstd_try(projection.decode(reader)));
    }
    return Ok(rstd::move(result));
}

// Return the first model, or None, while still consuming subsequent rows and
// propagating later step errors. Projection offsets and factories stay
// explicit.
template<class Prepared,
         class Model,
         rstd::size_t Offset,
         class Factory = liorm::DefaultModelFactory<Model>>
    requires requires(query::ProjectionGroup<query::ModelGroup<Model>, Offset> group,
                      Reader&                                                  reader,
                      Factory factory) { liorm::decode_model(group, reader, rstd::move(factory)); }
auto one_model(Prepared&                                                statement,
               query::ProjectionGroup<query::ModelGroup<Model>, Offset> group,
               Factory                                                  factory = {},
               RowConsumption consumption = RowConsumption::Drain)
    -> Result<Option<Model>, ::sqlito::Error> {
    if (rstd_try(statement.step()) == ::sqlito::Step::Done) return Ok<Option<Model>>(None());
    auto reader = statement.reader();
    auto result = rstd_try(liorm::decode_model(group, reader, rstd::move(factory)));
    rstd_try(finish_row(statement, consumption, "expected at most one row"_str));
    return Ok(Some(rstd::move(result)));
}

template<class Row>
struct PageRows {
    i64      total;
    Vec<Row> rows;
};

class PreparedPage {
    PreparedQuery<query::ExpandedQuery> count_;
    PreparedQuery<query::ExpandedQuery> rows_;

    PreparedPage(PreparedQuery<query::ExpandedQuery> count,
                 PreparedQuery<query::ExpandedQuery> rows)
        : count_(rstd::move(count)), rows_(rstd::move(rows)) {}

public:
    PreparedPage(const PreparedPage&)                    = delete;
    auto operator=(const PreparedPage&) -> PreparedPage& = delete;
    PreparedPage(PreparedPage&&)                         = default;
    auto operator=(PreparedPage&&) -> PreparedPage&      = default;

    template<class Session>
    static auto create(Session& session, query::PageQuery query)
        -> Result<PreparedPage, ::sqlito::Error>
        requires(rstd::mtp::same_as<Session, ::sqlito::Connection> ||
                 rstd::mtp::same_as<Session, ::sqlito::Transaction>)
    {
        auto count = rstd_try(prepare(session, rstd::move(query.count)));
        auto rows  = rstd_try(prepare(session, rstd::move(query.rows)));
        return Ok(PreparedPage(rstd::move(count), rstd::move(rows)));
    }

    template<class T>
    auto bind(ref<str> name, const T& value) -> Result<empty, liorm::BindError<::sqlito::Error>> {
        rstd_try(count_.bind(name, value));
        return rows_.bind(name, value);
    }
    template<class T>
    auto bind_group(ref<str> name, slice<T> values)
        -> Result<empty, liorm::BindError<::sqlito::Error>> {
        rstd_try(count_.bind_group(name, values));
        return rows_.bind_group(name, values);
    }
    template<class Getter>
    auto bind_group_with(ref<str> name, usize count, Getter getter)
        -> Result<empty, liorm::BindError<::sqlito::Error>> {
        auto read = [&getter](usize index) -> decltype(auto) {
            return getter(index);
        };
        rstd_try(count_.bind_group_with(name, count, read));
        return rows_.bind_group_with(name, count, read);
    }
    template<class Projection>
    auto fetch(const Projection& projection)
        -> Result<PageRows<typename Projection::value_type>,
                  typename decltype(projection.decode(rstd::mtp::declval<Reader&>()))::error_type> {
        // An active count row keeps the Connection's read snapshot alive for rows_.
        auto total = rstd_try(scalar<i64>(count_, usize(), RowConsumption::First));
        auto rows  = rstd_try(all_rows(rows_, projection));
        rstd_try(finish_row(count_, RowConsumption::Exact, "expected exactly one count row"_str));
        return Ok(PageRows<typename Projection::value_type> { total, rstd::move(rows) });
    }
};

template<class Session>
auto prepare_page(Session& session, query::PageQuery query) -> Result<PreparedPage, ::sqlito::Error>
    requires(rstd::mtp::same_as<Session, ::sqlito::Connection> ||
             rstd::mtp::same_as<Session, ::sqlito::Transaction>)
{
    return PreparedPage::create(session, rstd::move(query));
}

class ActiveWriteError {
    RSTD_ENUM(ActiveWriteError,
              (Planning, (liorm::WriteError error;)),
              (Storage, (::sqlito::Error error;)),
              (Binding, (liorm::BindError<::sqlito::Error> error;)),
              (NotFound),
              (NotUpdated),
              (NotInserted))
};

template<class Model, class Policy>
class PreparedWrite {
    liorm::WriteCommand<Policy, Model> command_;
    ::sqlito::Statement                statement_;
    PreparedWrite(liorm::WriteCommand<Policy, Model> command, ::sqlito::Statement statement)
        : command_(rstd::move(command)), statement_(rstd::move(statement)) {}

public:
    PreparedWrite(const PreparedWrite&)                    = delete;
    auto operator=(const PreparedWrite&) -> PreparedWrite& = delete;
    PreparedWrite(PreparedWrite&&)                         = default;
    auto operator=(PreparedWrite&&) -> PreparedWrite&      = default;

    template<class Session>
    static auto create(Session& session, liorm::WriteCommand<Policy, Model> command)
        -> Result<PreparedWrite, ::sqlito::Error>
        requires(WritePolicy<Policy> && (rstd::mtp::same_as<Session, ::sqlito::Connection> ||
                                         rstd::mtp::same_as<Session, ::sqlito::Transaction>))
    {
        auto statement = session.prepare(command.query().sql());
        if (statement.is_err()) return Err(rstd::move(statement).unwrap_err());
        return Ok(PreparedWrite(rstd::move(command), rstd::move(statement).unwrap()));
    }

    auto kind() const -> liorm::WriteKind { return command_.kind(); }
    auto query() const& -> const query::CompiledQuery& { return command_.query(); }
    auto query() const&& -> const query::CompiledQuery& = delete;
    auto projection() const { return command_.projection(); }
    auto bind() -> Result<empty, liorm::BindError<::sqlito::Error>>
        requires WritePolicy<Policy>
    {
        Writer base(statement_);
        Policy writer(base);
        return command_.bind(writer);
    }
    auto step() -> Result<::sqlito::Step, ::sqlito::Error> { return statement_.step(); }
    auto reset() -> Result<empty, ::sqlito::Error> { return statement_.reset(); }
    auto reader() & -> Reader { return Reader(statement_); }
    auto reader() && -> Reader = delete;
};

template<class Session, class Model, class Policy = Writer>
auto prepare(Session& session, liorm::WriteCommand<Policy, Model> command)
    -> Result<PreparedWrite<Model, Policy>, ::sqlito::Error>
    requires(WritePolicy<Policy> && (rstd::mtp::same_as<Session, ::sqlito::Connection> ||
                                     rstd::mtp::same_as<Session, ::sqlito::Transaction>))
{
    return PreparedWrite<Model, Policy>::create(session, rstd::move(command));
}

// Policy follows Factory to preserve execute<Session, Model, Factory> callers.
template<class Session,
         class Model,
         class Factory = liorm::DefaultModelFactory<Model>,
         class Policy  = Writer>
    requires(WritePolicy<Policy> &&
             (rstd::mtp::same_as<Session, ::sqlito::Connection> ||
              rstd::mtp::same_as<Session, ::sqlito::Transaction>) &&
             requires(query::ProjectionGroup<query::ModelGroup<Model>, 0> group,
                      Reader&                                             reader,
                      Factory factory) { liorm::decode_model(group, reader, rstd::move(factory)); })
auto execute(Session& session, liorm::WriteCommand<Policy, Model> command, Factory factory = {})
    -> Result<Model, ActiveWriteError> {
    auto prepared = prepare(session, rstd::move(command));
    if (prepared.is_err()) return Err(ActiveWriteError::Storage(rstd::move(prepared).unwrap_err()));
    auto statement = rstd::move(prepared).unwrap();
    auto bound     = statement.bind();
    if (bound.is_err()) return Err(ActiveWriteError::Binding(rstd::move(bound).unwrap_err()));
    auto step = statement.step();
    if (step.is_err()) return Err(ActiveWriteError::Storage(rstd::move(step).unwrap_err()));
    if (step.unwrap() == ::sqlito::Step::Done) {
        if (statement.kind() == liorm::WriteKind::SelectExisting)
            return Err(ActiveWriteError::NotFound());
        if (statement.kind() == liorm::WriteKind::Update)
            return Err(ActiveWriteError::NotUpdated());
        return Err(ActiveWriteError::NotInserted());
    }
    auto reader  = statement.reader();
    auto decoded = liorm::decode_model(statement.projection(), reader, rstd::move(factory));
    if (decoded.is_err()) return Err(ActiveWriteError::Storage(rstd::move(decoded).unwrap_err()));
    auto result = rstd::move(decoded).unwrap();
    while (true) {
        auto next = statement.step();
        if (next.is_err()) return Err(ActiveWriteError::Storage(rstd::move(next).unwrap_err()));
        if (next.unwrap() == ::sqlito::Step::Done) break;
    }
    return Ok(rstd::move(result));
}
} // namespace liorm::sqlito

namespace liorm::sqlito
{
template<class Session, class Model, class Policy, class Factory>
concept ExecutableWrite =
    requires(Session& session, liorm::WriteCommand<Policy, Model> command, Factory factory) {
        execute(session, rstd::move(command), rstd::move(factory));
    };

template<class Session, class Model, class Policy, class Factory>
auto execute_planned(Session&                                                      session,
                     Result<liorm::WriteCommand<Policy, Model>, liorm::WriteError> command,
                     Factory factory) -> Result<Model, ActiveWriteError> {
    if (command.is_err()) return Err(ActiveWriteError::Planning(rstd::move(command).unwrap_err()));
    return execute(session, rstd::move(command).unwrap(), rstd::move(factory));
}
} // namespace liorm::sqlito

export namespace liorm::sqlito
{
template<const liorm::schema::Schema& Tables,
         class Policy = Writer,
         class Session,
         class Model,
         class Factory = liorm::DefaultModelFactory<Model>>
    requires ExecutableWrite<Session, Model, Policy, Factory>
auto insert(Session& session, liorm::InsertValues<Model> values, Factory factory = {})
    -> Result<Model, ActiveWriteError> {
    return execute_planned(
        session,
        liorm::try_cached_insert<Tables, Policy>(rstd::move(values).into_active()),
        rstd::move(factory));
}

template<const liorm::schema::Schema& Tables,
         class Policy = Writer,
         class Session,
         class KeyValue,
         class Model,
         class Factory = liorm::DefaultModelFactory<Model>>
    requires ExecutableWrite<Session, Model, Policy, Factory> &&
             rstd::mtp::init<liorm::Key<Model>, KeyValue>
auto update(Session& session, KeyValue&& key, liorm::Changeset<Model> changes, Factory factory = {})
    -> Result<Model, ActiveWriteError> {
    auto values =
        liorm::update_values(liorm::Key<Model>(rstd::forward<KeyValue>(key)), rstd::move(changes));
    return execute_planned(
        session, liorm::try_cached_update<Tables, Policy>(rstd::move(values)), rstd::move(factory));
}

template<const liorm::schema::Schema& Tables,
         class Policy = Writer,
         class Session,
         class Model,
         class Factory = liorm::DefaultModelFactory<Model>>
    requires requires { liorm_table_definition(static_cast<Model*>(nullptr)); } &&
             ExecutableWrite<Session, Model, Policy, Factory>
auto insert_all(Session& session, Model model, Factory factory = {})
    -> Result<Model, ActiveWriteError> {
    auto values = liorm::ActiveModel<Model>::from_model(rstd::move(model));
    values.reset_all();
    return execute_planned(
        session, liorm::try_cached_insert<Tables, Policy>(rstd::move(values)), rstd::move(factory));
}

template<const liorm::schema::Schema& Tables,
         class Policy = Writer,
         class Session,
         class Model,
         class Factory = liorm::DefaultModelFactory<Model>>
    requires requires { liorm_table_definition(static_cast<Model*>(nullptr)); } &&
             ExecutableWrite<Session, Model, Policy, Factory>
auto update_all(Session& session, Model model, Factory factory = {})
    -> Result<Model, ActiveWriteError> {
    auto values = liorm::ActiveModel<Model>::from_model(rstd::move(model));
    values.reset_all();
    return execute_planned(
        session, liorm::try_cached_update<Tables, Policy>(rstd::move(values)), rstd::move(factory));
}
} // namespace liorm::sqlito

export namespace rstd
{
template<>
struct Impl<convert::From<::sqlito::Error>, liorm::sqlito::ReadError> {
    static auto from(::sqlito::Error error) -> liorm::sqlito::ReadError {
        return liorm::sqlito::ReadError::Storage(rstd::move(error));
    }
};
template<>
struct Impl<convert::From<liorm::BindError<::sqlito::Error>>, liorm::sqlito::ReadError> {
    static auto from(liorm::BindError<::sqlito::Error> error) -> liorm::sqlito::ReadError {
        return liorm::sqlito::ReadError::Binding(rstd::move(error));
    }
};
template<class E>
    requires(! rstd::mtp::same_as<E, liorm::sqlito::ReadError>) &&
            requires(::sqlito::Error storage, liorm::BindError<::sqlito::Error> binding) {
                rstd::into<E>(rstd::move(storage));
                rstd::into<E>(rstd::move(binding));
            }
struct Impl<convert::From<liorm::sqlito::ReadError>, E> {
    static auto from(liorm::sqlito::ReadError error) -> E {
        RSTD_MATCH(error) {
            RSTD_CASE(Storage, cause) {
                return rstd::into<E>(rstd::move(cause));
            }
            RSTD_CASE(Binding, cause) {
                return rstd::into<E>(rstd::move(cause));
            }
        }
    }
};
} // namespace rstd

namespace liorm::sqlito
{
template<class E>
struct ReadResultError {
    using type = E;
};
template<>
struct ReadResultError<::sqlito::Error> {
    using type = ReadError;
};
template<class Projection>
using ProjectionReadError =
    typename ReadResultError<typename decltype(rstd::mtp::declval<const Projection&>().decode(
        rstd::mtp::declval<Reader&>()))::error_type>::type;

template<const schema::Schema& Tables, class Model, bool Exists>
inline constexpr auto primary_read_query = query::compile([](query::QueryBuilder& b) {
    using Definition          = typename query::ModelGroup<Model>::definition;
    auto                table = b.use(Tables).template table<Model>();
    Option<query::Expr> condition;
    [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
        static_assert((Definition::template field<I>::primary || ...),
                      "primary read requires a primary key");
        auto append = [&]<class Field>() {
            if constexpr (Field::primary) {
                using Value = typename Field::value_type;
                static_assert(Writer::template supports<Value>(), "unsupported primary key codec");
                auto equal = table.template col<Field::member>() ==
                             b.template member_param<Field::member>(
                                 Field::name.view(), Writer::template parameter_type<Value>());
                condition  = condition.is_some() ? Some(*condition && equal) : Some(equal);
            }
        };
        (append.template operator()<typename Definition::template field<I>>(), ...);
    }(rstd::mtp::make_index_sequence<Definition::field_count> {});
    auto select = [&] {
        if constexpr (Exists)
            return b.select(b.integer(1));
        else {
            query::ProjectionLayout<query::ModelGroup<Model>> layout;
            return b.select(query::project(layout, table).as_slice());
        }
    }();
    return select.from(table.source()).where(*condition).limit(b.integer(1));
});
} // namespace liorm::sqlito

export namespace liorm::sqlito
{
template<const query::CompiledQuery& Query, class Session, class... Fields>
auto prepare(Session& session, liorm::Parameters<Fields...> parameters = liorm::parameters())
    -> Result<PreparedQuery<StaticQuery<Query>>, ReadError> {
    return PreparedQuery<StaticQuery<Query>>::template create<Query>(session,
                                                                     rstd::move(parameters));
}

template<const query::CompiledQuery& Query, class Session, class Projection, class... Fields>
auto fetch_one(Session&                     session,
               const Projection&            projection,
               liorm::Parameters<Fields...> parameters  = liorm::parameters(),
               RowConsumption               consumption = RowConsumption::Drain)
    -> Result<Option<typename Projection::value_type>, ProjectionReadError<Projection>> {
    auto statement = rstd_try(prepare<Query>(session, rstd::move(parameters)));
    auto result    = rstd_try(one_row(statement, projection, consumption));
    return Ok(rstd::move(result));
}

template<const query::CompiledQuery& Query, class Session, class Projection, class... Fields>
auto fetch_all(Session&                     session,
               const Projection&            projection,
               liorm::Parameters<Fields...> parameters = liorm::parameters())
    -> Result<Vec<typename Projection::value_type>, ProjectionReadError<Projection>> {
    auto statement = rstd_try(prepare<Query>(session, rstd::move(parameters)));
    auto result    = rstd_try(all_rows(statement, projection));
    return Ok(rstd::move(result));
}

template<const query::CompiledQuery& Query,
         class Session,
         class Model,
         rstd::size_t Offset,
         class... Fields,
         class Factory = liorm::DefaultModelFactory<Model>>
    requires requires(query::ProjectionGroup<query::ModelGroup<Model>, Offset> group,
                      Reader&                                                  reader,
                      Factory factory) { liorm::decode_model(group, reader, rstd::move(factory)); }
auto fetch_model(Session&                                                 session,
                 query::ProjectionGroup<query::ModelGroup<Model>, Offset> group,
                 liorm::Parameters<Fields...> parameters  = liorm::parameters(),
                 RowConsumption               consumption = RowConsumption::Drain,
                 Factory                      factory = {}) -> Result<Option<Model>, ReadError> {
    auto statement = rstd_try(prepare<Query>(session, rstd::move(parameters)));
    auto result    = rstd_try(one_model(statement, group, rstd::move(factory), consumption));
    return Ok(rstd::move(result));
}

template<const query::CompiledQuery& Query, class T, class Session, class... Fields>
auto fetch_scalar(Session&                     session,
                  liorm::Parameters<Fields...> parameters  = liorm::parameters(),
                  RowConsumption               consumption = RowConsumption::Exact,
                  usize                        column      = usize()) -> Result<T, ReadError> {
    auto statement = rstd_try(prepare<Query>(session, rstd::move(parameters)));
    return Ok(rstd_try(scalar<T>(statement, column, consumption)));
}

template<const query::CompiledQuery& Query, class T, class Session, class... Fields>
auto fetch_optional_scalar(Session&                     session,
                           liorm::Parameters<Fields...> parameters  = liorm::parameters(),
                           RowConsumption               consumption = RowConsumption::Exact,
                           usize column = usize()) -> Result<Option<T>, ReadError> {
    auto statement = rstd_try(prepare<Query>(session, rstd::move(parameters)));
    return Ok(rstd_try(optional_scalar<T>(statement, column, consumption)));
}

template<class Prepared>
auto row_exists(Prepared& statement, RowConsumption consumption = RowConsumption::First)
    -> Result<bool, ::sqlito::Error> {
    if (rstd_try(statement.step()) == ::sqlito::Step::Done) return Ok(false);
    rstd_try(finish_row(statement, consumption, "expected at most one row"_str));
    return Ok(true);
}

template<const query::CompiledQuery& Query, class Session, class... Fields>
auto fetch_exists(Session&                     session,
                  liorm::Parameters<Fields...> parameters = liorm::parameters(),
                  RowConsumption consumption = RowConsumption::First) -> Result<bool, ReadError> {
    auto statement = rstd_try(prepare<Query>(session, rstd::move(parameters)));
    return Ok(rstd_try(row_exists(statement, consumption)));
}

template<const schema::Schema& Tables,
         class Model,
         class Session,
         class KeyValue,
         class Factory = liorm::DefaultModelFactory<Model>>
    requires rstd::mtp::init<liorm::Key<Model>, KeyValue> &&
             requires(Reader& reader, Factory factory) {
                 liorm::decode_model(
                     query::ProjectionLayout<query::ModelGroup<Model>> {}.template group<0>(),
                     reader,
                     rstd::move(factory));
             }
auto find(Session&       session,
          KeyValue&&     key,
          RowConsumption consumption = RowConsumption::Drain,
          Factory        factory     = {}) -> Result<Option<Model>, ReadError> {
    auto statement = rstd_try(prepare(session, primary_read_query<Tables, Model, false>));
    rstd_try(statement.bind_key(liorm::Key<Model>(rstd::forward<KeyValue>(key))));
    auto result =
        rstd_try(one_model(statement,
                           query::ProjectionLayout<query::ModelGroup<Model>> {}.template group<0>(),
                           rstd::move(factory),
                           consumption));
    return Ok(rstd::move(result));
}

template<const schema::Schema& Tables, class Model, class Session, class KeyValue>
    requires rstd::mtp::init<liorm::Key<Model>, KeyValue>
auto exists(Session& session, KeyValue&& key) -> Result<bool, ReadError> {
    auto statement = rstd_try(prepare(session, primary_read_query<Tables, Model, true>));
    rstd_try(statement.bind_key(liorm::Key<Model>(rstd::forward<KeyValue>(key))));
    return Ok(rstd_try(row_exists(statement)));
}
} // namespace liorm::sqlito
