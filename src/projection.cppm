module;
#include <rstd/macro.hpp>
export module liorm:projection;
export import :row;
import rstd;
using namespace rstd::prelude;

namespace liorm
{
template<rstd::size_t Index, class Tuple, class Reader, class... Values>
auto decode_tuple(Reader& reader, Values&&... values)
    -> Result<Tuple, typename Reader::error_type> {
    if constexpr (Index == rstd::mtp::tuple_size<Tuple>) {
        return Ok(Tuple(rstd::forward<Values>(values)...));
    } else {
        using Value = rstd::mtp::tuple_element<Index, Tuple>;
        auto value  = rstd_try(reader.template read<Value>(usize(Index)));
        return decode_tuple<Index + 1, Tuple>(
            reader, rstd::forward<Values>(values)..., rstd::move(value));
    }
}

template<rstd::size_t Index,
         class Row,
         auto Member,
         auto... Rest,
         class Reader,
         class Factory,
         class... Fields>
auto decode_record(Reader& reader, Factory& factory, Fields&&... fields)
    -> Result<Row, typename Reader::error_type> {
    using Value = typename schema::MemberPointer<decltype(Member)>::value_type;
    auto value  = rstd_try(reader.template read<Value>(usize(Index)));
    auto field  = DecodedField<Member, Value> { rstd::move(value) };
    if constexpr (sizeof...(Rest) == 0)
        return Ok<Row>(factory(rstd::forward<Fields>(fields)..., rstd::move(field)));
    else
        return decode_record<Index + 1, Row, Rest...>(
            reader, factory, rstd::forward<Fields>(fields)..., rstd::move(field));
}
} // namespace liorm

export namespace liorm
{
// The same ordered shape builds SELECT columns and decodes their owning values.
template<class... Values>
    requires(sizeof...(Values) > 0)
struct TupleProjection {
    using value_type                           = rstd::tuple<Values...>;
    static constexpr rstd::size_t column_count = sizeof...(Values);

    template<class... Expressions>
        requires(sizeof...(Expressions) == column_count) &&
                (rstd::mtp::same_as<Expressions, query::Expr> && ...)
    constexpr auto select(query::QueryBuilder& builder, Expressions... expressions) const {
        return builder.select(expressions...);
    }
    template<class Reader>
    auto decode(Reader& reader) const -> Result<value_type, typename Reader::error_type> {
        return decode_tuple<0, value_type>(reader);
    }
};

template<class Model, rstd::size_t Offset = 0>
struct ModelProjection {
    using value_type = Model;
    template<class Reader>
    auto decode(Reader& reader) const -> Result<Model, typename Reader::error_type> {
        return decode_model(query::ProjectionGroup<query::ModelGroup<Model>, Offset> {}, reader);
    }
};

template<class Row, auto... Members>
    requires(sizeof...(Members) > 0) &&
            (rstd::mtp::same_as<typename schema::MemberPointer<decltype(Members)>::owner_type,
                                Row> &&
             ...)
struct RecordProjection {
    using value_type                           = Row;
    static constexpr rstd::size_t column_count = sizeof...(Members);
    static_assert(
        [] {
            const array<const void*, column_count> ids {
                &query::member_parameter_identity<Members>...
            };
            for (usize i {}; i < ids.len(); ++i)
                for (usize j = i + usize(1); j < ids.len(); ++j)
                    if (ids[i] == ids[j]) return false;
            return true;
        }(),
        "projection members must be distinct");

    template<class... Expressions>
        requires(sizeof...(Expressions) == column_count) &&
                (rstd::mtp::same_as<Expressions, query::Expr> && ...)
    constexpr auto select(query::QueryBuilder& builder, Expressions... expressions) const {
        return builder.select(expressions...);
    }
    template<auto Member, class Reader>
        requires((&query::member_parameter_identity<Member> ==
                  &query::member_parameter_identity<Members>) ||
                 ...)
    auto field(Reader& reader) const
        -> Result<typename schema::MemberPointer<decltype(Member)>::value_type,
                  typename Reader::error_type> {
        constexpr auto index = [] {
            const array<const void*, column_count> ids {
                &query::member_parameter_identity<Members>...
            };
            rstd::size_t result = column_count;
            for (rstd::size_t i = 0; i < column_count; ++i)
                if (ids[usize(i)] == &query::member_parameter_identity<Member>) result = i;
            return result;
        }();
        static_assert(index < column_count, "member is not part of this projection");
        using Value = typename schema::MemberPointer<decltype(Member)>::value_type;
        return reader.template read<Value>(usize(index));
    }
    template<class Reader, class Factory = DefaultModelFactory<Row>>
    auto decode(Reader& reader, Factory factory = {}) const
        -> Result<Row, typename Reader::error_type> {
        return decode_record<0, Row, Members...>(reader, factory);
    }
};
} // namespace liorm
