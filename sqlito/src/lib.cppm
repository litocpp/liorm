module;
#include <cstdint>
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
export module liorm.sqlito;
export import liorm;
export import sqlito;

export namespace liorm::sqlito {
using namespace rstd::prelude;
using namespace rstd::literals;

template <class T> struct Codec;

template <class T>
auto codec_null(const T &value) -> Option<bool>
  requires requires { Codec<T>::parameter_type(); }
{
  if (!Codec<T>::parameter_type().nullable)
    return Some(false);
  if constexpr (requires { Codec<T>::is_null(value); }) {
    using Status = std::remove_cvref_t<decltype(Codec<T>::is_null(value))>;
    if constexpr (std::is_same_v<Status, bool>)
      return Some(Codec<T>::is_null(value));
    else if constexpr (std::is_same_v<Status, Option<bool>>)
      return Codec<T>::is_null(value);
  }
  return None();
}

auto invalid_column() -> ::sqlito::Error {
  return {::sqlito::ErrorKind::Type, 0,
          String::make("invalid model column"_str)};
}

template <> struct Codec<i64> {
  static constexpr auto parameter_type() -> query::ParameterType {
    return {ValueKind::Integer, false};
  }
  static auto read(::sqlito::Statement &statement, usize column)
      -> Result<i64, ::sqlito::Error> {
    auto value = rstd_try(statement.integer(column));
    if (value.is_none())
      return Err(invalid_column());
    return Ok(*value);
  }
  static auto write(::sqlito::Statement &statement, usize slot, i64 value)
      -> Result<empty, ::sqlito::Error> {
    return statement.bind(slot, value);
  }
};

template <> struct Codec<i32> {
  static constexpr auto parameter_type() -> query::ParameterType {
    return {ValueKind::Integer, false};
  }
  static auto read(::sqlito::Statement &statement, usize column)
      -> Result<i32, ::sqlito::Error> {
    auto value = rstd_try(Codec<i64>::read(statement, column));
    if (value.to_primitive() < INT32_MIN || value.to_primitive() > INT32_MAX)
      return Err(invalid_column());
    return Ok(i32(static_cast<int>(value.to_primitive())));
  }
  static auto write(::sqlito::Statement &statement, usize slot, i32 value)
      -> Result<empty, ::sqlito::Error> {
    return statement.bind(slot, i64(value.to_primitive()));
  }
};

template <> struct Codec<String> {
  static constexpr auto parameter_type() -> query::ParameterType {
    return {ValueKind::Text, false};
  }
  static auto read(::sqlito::Statement &statement, usize column)
      -> Result<String, ::sqlito::Error> {
    auto value = rstd_try(statement.text(column));
    if (value.is_none())
      return Err(invalid_column());
    return Ok(rstd::move(value).unwrap());
  }
  static auto write(::sqlito::Statement &statement, usize slot,
                    const String &value) -> Result<empty, ::sqlito::Error> {
    return statement.bind_text(slot, value.as_str());
  }
};

template <class T> struct Codec<Option<T>> {
  static auto is_null(const Option<T> &value) -> Option<bool>
    requires requires { Codec<T>::parameter_type(); }
  {
    if (value.is_none())
      return Some(true);
    return codec_null(*value);
  }
  static constexpr auto parameter_type() -> query::ParameterType
    requires requires { Codec<T>::parameter_type(); }
  {
    auto type = Codec<T>::parameter_type();
    type.nullable = true;
    return type;
  }
  static auto read(::sqlito::Statement &statement, usize column)
      -> Result<Option<T>, ::sqlito::Error> {
    auto type = rstd_try(statement.column_type(column));
    if (type == ::sqlito::ColumnType::Null)
      return Ok<Option<T>>(None());
    return Ok(Some(rstd_try(Codec<T>::read(statement, column))));
  }
  static auto write(::sqlito::Statement &statement, usize slot,
                    const Option<T> &value) -> Result<empty, ::sqlito::Error>
    requires requires(const T &item) { Codec<T>::write(statement, slot, item); }
  {
    if (value.is_none())
      return statement.bind_null(slot);
    return Codec<T>::write(statement, slot, *value);
  }
};

class Reader {
  ::sqlito::Statement &statement_;

public:
  using error_type = ::sqlito::Error;
  explicit Reader(::sqlito::Statement &statement) : statement_(statement) {}
  template <class T> auto read(usize column) -> Result<T, error_type> {
    return Codec<T>::read(statement_, column);
  }
};

template <class Query> class PreparedQuery;
template <class Model> class PreparedWrite;

class Writer {
  template <class Query> friend class PreparedQuery;
  template <class Model> friend class PreparedWrite;
  ::sqlito::Statement &statement_;
  explicit Writer(::sqlito::Statement &statement) : statement_(statement) {}

public:
  using error_type = ::sqlito::Error;
  template <class T> static consteval auto supports() -> bool {
    return requires(::sqlito::Statement &statement, usize slot,
                    const T &value) {
      Codec<T>::parameter_type();
      Codec<T>::write(statement, slot, value);
    };
  }
  template <class T>
  static constexpr auto compatible(query::ParameterType target) -> bool {
    if constexpr (supports<T>()) {
      auto source = Codec<T>::parameter_type();
      return source.kind == target.kind &&
             (!source.nullable || target.nullable);
    } else {
      return false;
    }
  }
  template <class T>
  static constexpr auto parameter_type() -> query::ParameterType
    requires(supports<T>())
  {
    return Codec<T>::parameter_type();
  }
  template <class T>
  static auto key_is_null(const T &value) -> Option<bool>
    requires(supports<T>())
  {
    return codec_null(value);
  }
  template <class T>
  auto write(usize slot, const T &value) -> Result<empty, error_type>
    requires(supports<T>())
  {
    return Codec<T>::write(statement_, slot, value);
  }
};

template <class Query> class PreparedQuery {
  Query query_;
  ::sqlito::Statement statement_;
  PreparedQuery(Query query, ::sqlito::Statement statement)
      : query_(std::move(query)), statement_(rstd::move(statement)) {}

public:
  PreparedQuery(const PreparedQuery &) = delete;
  auto operator=(const PreparedQuery &) -> PreparedQuery & = delete;
  PreparedQuery(PreparedQuery &&) = default;
  auto operator=(PreparedQuery &&) -> PreparedQuery & = default;

  template <class Session>
  static auto create(Session &session, Query query)
      -> Result<PreparedQuery, ::sqlito::Error>
    requires((std::is_same_v<Query, liorm::query::CompiledQuery> ||
              std::is_same_v<Query, liorm::query::ExpandedQuery>) &&
             (std::is_same_v<Session, ::sqlito::Connection> ||
              std::is_same_v<Session, ::sqlito::Transaction>))
  {
    auto statement = session.prepare(query.sql());
    if (statement.is_err())
      return Err(rstd::move(statement).unwrap_err());
    return Ok(PreparedQuery(std::move(query), rstd::move(statement).unwrap()));
  }

  auto query() const & -> const Query & { return query_; }
  auto query() const && -> const Query & = delete;
  template <class Model>
  auto bind_model(const Model &model)
      -> Result<empty, liorm::BindError<::sqlito::Error>> {
    Writer writer(statement_);
    return liorm::bind_model(query_, writer, model);
  }
  template <class T>
  auto bind(std::string_view name, const T &value)
      -> Result<empty, liorm::BindError<::sqlito::Error>> {
    Writer writer(statement_);
    return liorm::bind_parameter(query_, writer, name, value);
  }
  template <class T>
  auto bind_group(std::string_view name, slice<T> values)
      -> Result<empty, liorm::BindError<::sqlito::Error>> {
    return bind_group_with(name, values.len(),
                           [values](std::size_t index) -> decltype(auto) {
                             return values[usize(index)];
                           });
  }
  template <class T, std::size_t Extent>
  auto bind_group(std::string_view name, std::span<T, Extent> values)
      -> Result<empty, liorm::BindError<::sqlito::Error>> {
    return bind_group_with(name, usize(values.size()),
                           [values](std::size_t index) -> decltype(auto) {
                             return values[index];
                           });
  }
  template <class Getter>
  auto bind_group_with(std::string_view name, usize count, Getter getter)
      -> Result<empty, liorm::BindError<::sqlito::Error>> {
    Writer writer(statement_);
    return liorm::bind_group_values(query_, writer, name, count.to_primitive(),
                                    std::move(getter));
  }
  template <class T>
  auto bind_repeat(std::string_view name, const T &value)
      -> Result<empty, liorm::BindError<::sqlito::Error>> {
    Writer writer(statement_);
    return liorm::bind_repeated(query_, writer, name, value);
  }
  template <class Shape, class GetRow>
  auto bind_rows(const Shape &shape, usize count, GetRow get_row)
      -> Result<empty, liorm::BindError<::sqlito::Error>> {
    Writer writer(statement_);
    return shape.bind(query_, writer, count, std::move(get_row));
  }
  template <class T>
  auto bind(usize slot, const T &value) -> Result<empty, ::sqlito::Error>
    requires(Writer::template supports<T>())
  {
    return Codec<T>::write(statement_, slot, value);
  }
  auto bind_text(usize slot, ref<str> value) -> Result<empty, ::sqlito::Error> {
    return statement_.bind_text(slot, value);
  }
  auto bind_blob(usize slot, slice<u8> value)
      -> Result<empty, ::sqlito::Error> {
    return statement_.bind_blob(slot, value);
  }
  auto bind_null(usize slot) -> Result<empty, ::sqlito::Error> {
    return statement_.bind_null(slot);
  }
  auto step() -> Result<::sqlito::Step, ::sqlito::Error> {
    return statement_.step();
  }
  auto reset() -> Result<empty, ::sqlito::Error> { return statement_.reset(); }
  // The reader borrows this prepared query and cannot outlive it.
  auto reader() & -> Reader { return Reader(statement_); }
  auto reader() && -> Reader = delete;
};

template <class Session, class Query>
auto prepare(Session &session, Query query)
    -> Result<PreparedQuery<Query>, ::sqlito::Error>
  requires((std::is_same_v<Query, liorm::query::CompiledQuery> ||
            std::is_same_v<Query, liorm::query::ExpandedQuery>) &&
           (std::is_same_v<Session, ::sqlito::Connection> ||
            std::is_same_v<Session, ::sqlito::Transaction>))
{
  return PreparedQuery<Query>::create(session, std::move(query));
}

// Terminals consume through Done before reporting success. They never finish
// the caller's transaction. A failure leaves recovery/reset to the caller.
template <class Prepared>
auto discard(Prepared &statement) -> Result<empty, ::sqlito::Error> {
  while (rstd_try(statement.step()) == ::sqlito::Step::Row) {
  }
  return Ok(empty{});
}

// Scalar queries require exactly one row. Decode errors retain their original
// kind; zero or multiple rows are a Range error. Extra rows are fully drained.
template <class T, class Prepared>
  requires requires(::sqlito::Statement &statement, usize column) {
    Codec<T>::read(statement, column);
  }
auto scalar(Prepared &statement, usize column = usize())
    -> Result<T, ::sqlito::Error> {
  if (rstd_try(statement.step()) == ::sqlito::Step::Done)
    return Err(::sqlito::Error{::sqlito::ErrorKind::Range, 0,
                               String::make("expected exactly one row"_str)});
  auto reader = statement.reader();
  auto result = rstd_try(reader.template read<T>(column));
  bool extra = false;
  while (rstd_try(statement.step()) == ::sqlito::Step::Row)
    extra = true;
  if (extra)
    return Err(::sqlito::Error{::sqlito::ErrorKind::Range, 0,
                               String::make("expected exactly one row"_str)});
  return Ok(rstd::move(result));
}

// Return the first model, or None, while still consuming subsequent rows and
// propagating later step errors. Projection offsets and factories stay
// explicit.
template <class Prepared, class Model, std::size_t Offset,
          class Factory = liorm::DefaultModelFactory<Model>>
  requires requires(
      query::ProjectionGroup<query::ModelGroup<Model>, Offset> group,
      Reader &reader, Factory factory) {
    liorm::decode_model(group, reader, std::move(factory));
  }
auto one_model(Prepared &statement,
               query::ProjectionGroup<query::ModelGroup<Model>, Offset> group,
               Factory factory = {}) -> Result<Option<Model>, ::sqlito::Error> {
  if (rstd_try(statement.step()) == ::sqlito::Step::Done)
    return Ok<Option<Model>>(None());
  auto reader = statement.reader();
  auto result =
      rstd_try(liorm::decode_model(group, reader, std::move(factory)));
  rstd_try(discard(statement));
  return Ok(Some(rstd::move(result)));
}

class ActiveWriteError {
  RSTD_ENUM(ActiveWriteError, (Storage, (::sqlito::Error error;)),
            (Binding, (liorm::BindError<::sqlito::Error> error;)), (NotFound),
            (NotUpdated), (NotInserted))
};

template <class Model> class PreparedWrite {
  liorm::WriteCommand<Writer, Model> command_;
  ::sqlito::Statement statement_;
  PreparedWrite(liorm::WriteCommand<Writer, Model> command,
                ::sqlito::Statement statement)
      : command_(std::move(command)), statement_(rstd::move(statement)) {}

public:
  PreparedWrite(const PreparedWrite &) = delete;
  auto operator=(const PreparedWrite &) -> PreparedWrite & = delete;
  PreparedWrite(PreparedWrite &&) = default;
  auto operator=(PreparedWrite &&) -> PreparedWrite & = default;

  template <class Session>
  static auto create(Session &session,
                     liorm::WriteCommand<Writer, Model> command)
      -> Result<PreparedWrite, ::sqlito::Error>
    requires(std::is_same_v<Session, ::sqlito::Connection> ||
             std::is_same_v<Session, ::sqlito::Transaction>)
  {
    auto statement = session.prepare(command.query().sql());
    if (statement.is_err())
      return Err(rstd::move(statement).unwrap_err());
    return Ok(
        PreparedWrite(std::move(command), rstd::move(statement).unwrap()));
  }

  auto kind() const -> liorm::WriteKind { return command_.kind(); }
  auto query() const & -> const query::CompiledQuery & {
    return command_.query();
  }
  auto query() const && -> const query::CompiledQuery & = delete;
  auto projection() const { return command_.projection(); }
  auto bind() -> Result<empty, liorm::BindError<::sqlito::Error>> {
    Writer writer(statement_);
    return command_.bind(writer);
  }
  auto step() -> Result<::sqlito::Step, ::sqlito::Error> {
    return statement_.step();
  }
  auto reset() -> Result<empty, ::sqlito::Error> { return statement_.reset(); }
  auto reader() & -> Reader { return Reader(statement_); }
  auto reader() && -> Reader = delete;
};

template <class Session, class Model>
auto prepare(Session &session, liorm::WriteCommand<Writer, Model> command)
    -> Result<PreparedWrite<Model>, ::sqlito::Error>
  requires(std::is_same_v<Session, ::sqlito::Connection> ||
           std::is_same_v<Session, ::sqlito::Transaction>)
{
  return PreparedWrite<Model>::create(session, std::move(command));
}

template <class Session, class Model,
          class Factory = liorm::DefaultModelFactory<Model>>
  requires requires(query::ProjectionGroup<query::ModelGroup<Model>, 0> group,
                    Reader &reader, Factory factory) {
    liorm::decode_model(group, reader, std::move(factory));
  }
auto execute(Session &session, liorm::WriteCommand<Writer, Model> command,
             Factory factory = {}) -> Result<Model, ActiveWriteError> {
  auto prepared = prepare(session, std::move(command));
  if (prepared.is_err())
    return Err(ActiveWriteError::Storage(rstd::move(prepared).unwrap_err()));
  auto statement = rstd::move(prepared).unwrap();
  auto bound = statement.bind();
  if (bound.is_err())
    return Err(ActiveWriteError::Binding(rstd::move(bound).unwrap_err()));
  auto step = statement.step();
  if (step.is_err())
    return Err(ActiveWriteError::Storage(rstd::move(step).unwrap_err()));
  if (step.unwrap() == ::sqlito::Step::Done) {
    if (statement.kind() == liorm::WriteKind::SelectExisting)
      return Err(ActiveWriteError::NotFound());
    if (statement.kind() == liorm::WriteKind::Update)
      return Err(ActiveWriteError::NotUpdated());
    return Err(ActiveWriteError::NotInserted());
  }
  auto reader = statement.reader();
  auto decoded =
      liorm::decode_model(statement.projection(), reader, std::move(factory));
  if (decoded.is_err())
    return Err(ActiveWriteError::Storage(rstd::move(decoded).unwrap_err()));
  auto result = rstd::move(decoded).unwrap();
  while (true) {
    auto next = statement.step();
    if (next.is_err())
      return Err(ActiveWriteError::Storage(rstd::move(next).unwrap_err()));
    if (next.unwrap() == ::sqlito::Step::Done)
      break;
  }
  return Ok(rstd::move(result));
}
} // namespace liorm::sqlito
