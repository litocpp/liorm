module;
#include <cstddef>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
export module liorm:bind;
export import :sqlite;
export import :active;

export namespace liorm {
enum class BindErrorKind {
  NoMembers,
  ForeignMember,
  Group,
  InvalidSlot,
  TypeMismatch,
  Unsupported,
  Write,
  MissingValue,
  NullPrimaryKey,
  UnknownParameter,
  ExpectedScalar,
  ExpectedGroup,
  CountMismatch
};

template <class E> struct BindError {
  BindErrorKind kind;
  unsigned parameter_index;
  rstd::Option<E> writer_error;
};

struct ValidatedParameterGroup {
  unsigned parameter_index;
  rstd::usize first;
  rstd::usize count;
};

} // namespace liorm

namespace liorm {
struct BindingMetadata {
  unsigned index;
  query::ParameterType type;
  std::size_t first;
  std::size_t count;
};

template <class Query, class Writer>
auto checked_binding_metadata(const Query &query, std::string_view name,
                              bool group)
    -> rstd::Result<BindingMetadata, BindError<typename Writer::error_type>> {
  using Error = BindError<typename Writer::error_type>;
  unsigned index = 0;
  for (; index < query.parameter_count(); ++index)
    if (query.parameter_name(index) == name)
      break;
  if (index == query.parameter_count())
    return rstd::Err(
        Error{BindErrorKind::UnknownParameter, index, rstd::None()});
  if (query.parameter_is_group(index) != group)
    return rstd::Err(Error{group ? BindErrorKind::ExpectedGroup
                                 : BindErrorKind::ExpectedScalar,
                           index, rstd::None()});
  auto parameter = query.parameter(index);
  if (parameter.is_none())
    return rstd::Err(Error{BindErrorKind::InvalidSlot, index, rstd::None()});
  if (!group) {
    if (parameter->slot == 0)
      return rstd::Err(Error{BindErrorKind::InvalidSlot, index, rstd::None()});
    return rstd::Ok(
        BindingMetadata{index, parameter->type, parameter->slot, 1});
  }
  // CompiledQuery deliberately has no parameter_group() interface.
  if constexpr (requires { query.parameter_group(name); }) {
    auto slots = query.parameter_group(name);
    if (slots.is_none())
      return rstd::Err(Error{BindErrorKind::InvalidSlot, index, rstd::None()});
    const auto first = slots->first.to_primitive();
    const auto count = slots->count.to_primitive();
    constexpr auto last_slot = std::numeric_limits<unsigned>::max();
    if (first != parameter->slot || first > last_slot ||
        (count == 0 ? first != 0 : first == 0 || count - 1 > last_slot - first))
      return rstd::Err(Error{BindErrorKind::InvalidSlot, index, rstd::None()});
    return rstd::Ok(BindingMetadata{index, parameter->type, first, count});
  } else {
    return rstd::Err(Error{BindErrorKind::ExpectedGroup, index, rstd::None()});
  }
}

template <class T, class Writer>
auto checked_binding_values(const BindingMetadata &metadata, Writer &writer,
                            std::size_t count)
    -> rstd::Result<ValidatedParameterGroup,
                    BindError<typename Writer::error_type>> {
  using Error = BindError<typename Writer::error_type>;
  using Value = std::remove_cvref_t<T>;
  if (count != metadata.count)
    return rstd::Err(
        Error{BindErrorKind::CountMismatch, metadata.index, rstd::None()});
  if constexpr (std::is_void_v<Value>) {
    return rstd::Err(
        Error{BindErrorKind::Unsupported, metadata.index, rstd::None()});
  } else if constexpr (!Writer::template supports<Value>()) {
    return rstd::Err(
        Error{BindErrorKind::Unsupported, metadata.index, rstd::None()});
  } else {
    if (!writer.template compatible<Value>(metadata.type))
      return rstd::Err(
          Error{BindErrorKind::TypeMismatch, metadata.index, rstd::None()});
    return rstd::Ok(ValidatedParameterGroup{
        metadata.index, rstd::usize(metadata.first), rstd::usize(count)});
  }
}

template <class Writer, class Getter>
auto bind_indexed_values(const ValidatedParameterGroup &metadata,
                         Writer &writer, Getter getter)
    -> rstd::Result<rstd::empty, BindError<typename Writer::error_type>> {
  using Error = BindError<typename Writer::error_type>;
  using Value = std::remove_cvref_t<decltype(getter(std::size_t{}))>;
  if constexpr (std::is_void_v<Value>) {
    return rstd::Err(Error{BindErrorKind::Unsupported, metadata.parameter_index,
                           rstd::None()});
  } else if constexpr (!Writer::template supports<Value>() ||
                       !std::is_convertible_v<decltype(getter(std::size_t{})),
                                              const Value &>) {
    return rstd::Err(Error{BindErrorKind::Unsupported, metadata.parameter_index,
                           rstd::None()});
  } else {
    for (std::size_t index = 0; index < metadata.count.to_primitive();
         ++index) {
      // Retain a returned temporary through write(), without copying a
      // reference result. Writers must encode/copy it before write() returns.
      const Value &value = getter(index);
      auto result = writer.template write<Value>(
          metadata.first + rstd::usize(index), value);
      if (result.is_err())
        return rstd::Err(Error{BindErrorKind::Write, metadata.parameter_index,
                               rstd::Some(rstd::move(result).unwrap_err())});
    }
    return rstd::Ok(rstd::empty{});
  }
}
} // namespace liorm

export namespace liorm {
// Validates one complete group without obtaining values or writing any slot.
// The returned layout belongs to this query; it is not binding-state tracking.
template <class T, class Query, class Writer>
auto validate_parameter_group(const Query &query, Writer &writer,
                              std::string_view name, std::size_t count)
    -> rstd::Result<ValidatedParameterGroup,
                    BindError<typename Writer::error_type>> {
  auto metadata = checked_binding_metadata<Query, Writer>(query, name, true);
  if (metadata.is_err())
    return rstd::Err(rstd::move(metadata).unwrap_err());
  return checked_binding_values<T>(metadata.unwrap(), writer, count);
}

template <class Query, class Writer, class T>
auto bind_parameter(const Query &query, Writer &writer, std::string_view name,
                    const T &value)
    -> rstd::Result<rstd::empty, BindError<typename Writer::error_type>> {
  auto metadata = checked_binding_metadata<Query, Writer>(query, name, false);
  if (metadata.is_err())
    return rstd::Err(rstd::move(metadata).unwrap_err());
  auto validated = checked_binding_values<T>(metadata.unwrap(), writer, 1);
  if (validated.is_err())
    return rstd::Err(rstd::move(validated).unwrap_err());
  return bind_indexed_values(
      validated.unwrap(), writer,
      [&value](std::size_t) -> const T & { return value; });
}

template <class Query, class Writer, class Getter>
auto bind_group_values(const Query &query, Writer &writer,
                       std::string_view name, std::size_t count, Getter getter)
    -> rstd::Result<rstd::empty, BindError<typename Writer::error_type>> {
  using Value = std::remove_cvref_t<decltype(getter(std::size_t{}))>;
  auto validated = validate_parameter_group<Value>(query, writer, name, count);
  if (validated.is_err())
    return rstd::Err(rstd::move(validated).unwrap_err());
  return bind_indexed_values(validated.unwrap(), writer, std::move(getter));
}

template <class Query, class Writer, class T>
auto bind_repeated(const Query &query, Writer &writer, std::string_view name,
                   const T &value)
    -> rstd::Result<rstd::empty, BindError<typename Writer::error_type>> {
  auto metadata = checked_binding_metadata<Query, Writer>(query, name, true);
  if (metadata.is_err())
    return rstd::Err(rstd::move(metadata).unwrap_err());
  auto validated =
      checked_binding_values<T>(metadata.unwrap(), writer, metadata->count);
  if (validated.is_err())
    return rstd::Err(rstd::move(validated).unwrap_err());
  return bind_indexed_values(
      validated.unwrap(), writer,
      [&value](std::size_t) -> const T & { return value; });
}

// Values supplies a pointer for each mapped member, or null if it is absent.
template <class Model, class Query, class Writer, class Values>
auto bind_values(const Query &query, Writer &writer, const Values &values)
    -> rstd::Result<rstd::empty, BindError<typename Writer::error_type>> {
  using Error = BindError<typename Writer::error_type>;
  using Definition =
      decltype(liorm_table_definition(static_cast<Model *>(nullptr)));
  bool any = false;
  for (unsigned index = 0; index < query.parameter_count(); ++index) {
    if (!query.parameter_has_member(index))
      continue;
    any = true;
    if (query.parameter_is_group(index))
      return rstd::Err(Error{BindErrorKind::Group, index, rstd::None()});
    auto parameter = query.parameter(index);
    if (parameter.is_none() || parameter->slot == 0)
      return rstd::Err(Error{BindErrorKind::InvalidSlot, index, rstd::None()});
    bool matched = false;
    bool supported = false;
    bool compatible = false;
    bool present = false;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      auto inspect = [&]<class Field>() {
        if (!query.template parameter_is<Field::member>(index))
          return;
        matched = true;
        present = values.template get<Field::member>() != nullptr;
        using Value = typename Field::value_type;
        if constexpr (Writer::template supports<Value>()) {
          supported = true;
          compatible = writer.template compatible<Value>(parameter->type);
        }
      };
      (inspect.template operator()<typename Definition::template field<I>>(),
       ...);
    }(std::make_index_sequence<Definition::field_count>{});
    if (!matched)
      return rstd::Err(
          Error{BindErrorKind::ForeignMember, index, rstd::None()});
    if (!present)
      return rstd::Err(Error{BindErrorKind::MissingValue, index, rstd::None()});
    if (!supported)
      return rstd::Err(Error{BindErrorKind::Unsupported, index, rstd::None()});
    if (!compatible)
      return rstd::Err(Error{BindErrorKind::TypeMismatch, index, rstd::None()});
  }
  if (!any)
    return rstd::Err(Error{BindErrorKind::NoMembers, 0, rstd::None()});

  for (unsigned index = 0; index < query.parameter_count(); ++index) {
    if (!query.parameter_has_member(index))
      continue;
    auto parameter = query.parameter(index).unwrap();
    rstd::Option<typename Writer::error_type> failure{rstd::None()};
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      auto write = [&]<class Field>() {
        using Value = typename Field::value_type;
        if constexpr (Writer::template supports<Value>()) {
          if (query.template parameter_is<Field::member>(index)) {
            auto result = writer.template write<Value>(
                rstd::usize(parameter.slot),
                *values.template get<Field::member>());
            if (result.is_err())
              failure = rstd::Some(rstd::move(result).unwrap_err());
          }
        }
      };
      (write.template operator()<typename Definition::template field<I>>(),
       ...);
    }(std::make_index_sequence<Definition::field_count>{});
    if (failure.is_some())
      return rstd::Err(Error{BindErrorKind::Write, index, rstd::move(failure)});
  }
  return rstd::Ok(rstd::empty{});
}

template <class Model> struct ModelValues {
  const Model &model;
  template <auto Member> auto get() const {
    return std::addressof(model.*Member);
  }
};

template <class Model> struct ActiveValues {
  const ActiveModel<Model> &model;
  template <auto Member> auto get() const {
    return model.template field<Member>().value();
  }
};

template <class Query, class Writer, class Model>
auto bind_model(const Query &query, Writer &writer, const Model &model)
    -> rstd::Result<rstd::empty, BindError<typename Writer::error_type>> {
  return bind_values<Model>(query, writer, ModelValues<Model>{model});
}
} // namespace liorm
