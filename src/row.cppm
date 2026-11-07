module;
#include <cstddef>
#include <type_traits>
#include <utility>
export module liorm:row;
export import :query;

export namespace liorm {
// Factories receive owning fields in projection order, with their member
// pointers.
template <auto Member, class T> struct DecodedField {
  static constexpr auto member = Member;
  T value;
};

template <class Model> struct DefaultModelFactory {
  template <class... Fields>
    requires(
        std::is_default_constructible_v<Model> &&
        (std::is_assignable_v<decltype(std::declval<Model &>().*Fields::member),
                              decltype(std::declval<Fields &&>().value)> &&
         ...))
  constexpr auto operator()(Fields &&...fields) const -> Model {
    Model result{};
    ((result.*Fields::member = std::move(fields.value)), ...);
    return result;
  }
};
} // namespace liorm

namespace liorm {
template <class Field>
auto decoded_field_type()
    -> DecodedField<Field::member, typename Field::value_type>;

template <class Model, class Factory, std::size_t... Index>
consteval auto model_factory_valid(std::index_sequence<Index...>) -> bool {
  using Definition =
      decltype(liorm_table_definition(static_cast<Model *>(nullptr)));
  return std::is_invocable_r_v<
      Model, Factory &,
      decltype(decoded_field_type<typename Definition::template field<Index>>())
          &&...>;
}

template <std::size_t Index, class Group, class Reader, class Factory,
          class... Values>
auto decode_model_fields(Reader &reader, Factory &factory, Values &&...values)
    -> rstd::Result<typename Group::segment_type::model_type,
                    typename Reader::error_type> {
  using Model = typename Group::segment_type::model_type;
  if constexpr (Index == Group::count) {
    return rstd::Ok<Model>(factory(std::forward<Values>(values)...));
  } else {
    using Field =
        typename Group::segment_type::definition::template field<Index>;
    auto value = reader.template read<typename Field::value_type>(
        rstd::usize(Group::offset + Index));
    if (value.is_err())
      return rstd::Err(rstd::move(value).unwrap_err());
    DecodedField<Field::member, typename Field::value_type> field{
        rstd::move(value).unwrap()};
    return decode_model_fields<Index + 1, Group>(
        reader, factory, std::forward<Values>(values)..., std::move(field));
  }
}
} // namespace liorm

export namespace liorm {
template <class Model, std::size_t Offset, class Reader,
          class Factory = DefaultModelFactory<Model>>
  requires(model_factory_valid<Model, Factory>(
      std::make_index_sequence<query::ModelGroup<Model>::count>{}))
auto decode_model(query::ProjectionGroup<query::ModelGroup<Model>, Offset>,
                  Reader &reader, Factory factory = {})
    -> rstd::Result<Model, typename Reader::error_type> {
  using Group = query::ProjectionGroup<query::ModelGroup<Model>, Offset>;
  return decode_model_fields<0, Group>(reader, factory);
}
} // namespace liorm
