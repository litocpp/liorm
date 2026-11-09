module;
export module liorm:row;
import rstd;
export import :query;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace liorm
{
// Factories receive owning fields in projection order, with their member
// pointers.
template<auto Member, class T>
struct DecodedField {
    static constexpr auto member = Member;
    T                     value;
};

template<class Model>
struct DefaultModelFactory {
    template<class... Fields>
        requires(requires(Model model, Fields&&... fields) {
            Model {};
            ((model.*Fields::member = rstd::move(fields).value), ...);
        })
    constexpr auto operator()(Fields&&... fields) const -> Model {
        Model result {};
        ((result.*Fields::member = rstd::move(fields.value)), ...);
        return result;
    }
};
} // namespace liorm

namespace liorm
{
template<class Field>
auto decoded_field_type() -> DecodedField<Field::member, typename Field::value_type>;

template<class R, class F, class... Args>
concept invocable_r =
    rstd::mtp::is_invocable_v<F, Args...> &&
    rstd::mtp::convertible_to<decltype(rstd::mtp::declval<F>()(rstd::mtp::declval<Args>()...)), R>;

template<class Model, class Factory, rstd::size_t... Index>
consteval auto model_factory_valid(rstd::mtp::index_sequence<Index...>) -> bool {
    using Definition = decltype(liorm_table_definition(static_cast<Model*>(nullptr)));
    return invocable_r<
        Model,
        Factory&,
        decltype(decoded_field_type<typename Definition::template field<Index>>())&&...>;
}

template<rstd::size_t Index, class Group, class Reader, class Factory, class... Values>
auto decode_model_fields(Reader& reader, Factory& factory, Values&&... values)
    -> rstd::Result<typename Group::segment_type::model_type, typename Reader::error_type> {
    using Model = typename Group::segment_type::model_type;
    if constexpr (Index == Group::count) {
        return rstd::Ok<Model>(factory(rstd::forward<Values>(values)...));
    } else {
        using Field = typename Group::segment_type::definition::template field<Index>;
        auto value =
            reader.template read<typename Field::value_type>(rstd::usize(Group::offset + Index));
        if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err());
        DecodedField<Field::member, typename Field::value_type> field {
            rstd::move(value).unwrap()
        };
        return decode_model_fields<Index + 1, Group>(
            reader, factory, rstd::forward<Values>(values)..., rstd::move(field));
    }
}
} // namespace liorm

export namespace liorm
{
template<class Model, rstd::size_t Offset, class Reader, class Factory = DefaultModelFactory<Model>>
    requires(model_factory_valid<Model, Factory>(
        rstd::mtp::make_index_sequence<query::ModelGroup<Model>::count> {}))
auto decode_model(query::ProjectionGroup<query::ModelGroup<Model>, Offset>,
                  Reader& reader,
                  Factory factory = {}) -> rstd::Result<Model, typename Reader::error_type> {
    using Group = query::ProjectionGroup<query::ModelGroup<Model>, Offset>;
    return decode_model_fields<0, Group>(reader, factory);
}
} // namespace liorm
