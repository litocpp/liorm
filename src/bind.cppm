module;
#include <cstddef>
#include <memory>
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
  NullPrimaryKey
};

template <class E> struct BindError {
  BindErrorKind kind;
  unsigned parameter_index;
  rstd::Option<E> writer_error;
};

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
