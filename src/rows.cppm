module;
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
export module liorm:rows;
export import :bind;

namespace liorm {
template <auto... Members> consteval auto distinct_row_members() -> bool {
  std::array<const void *, sizeof...(Members)> ids{
      &query::member_parameter_identity<Members>...};
  for (std::size_t i = 0; i < ids.size(); ++i)
    for (std::size_t j = i + 1; j < ids.size(); ++j)
      if (ids[i] == ids[j])
        return false;
  return true;
}
template <class Model, auto Member> consteval auto row_parameter_name() {
  constexpr auto table = schema::table_name<Model>();
  constexpr auto column = schema::column_name<Member>();
  constexpr std::string_view prefix = "_rows_";
  std::array<char, prefix.size() + table.size() + 1 + column.size()> result{};
  std::size_t i = 0;
  for (char c : prefix)
    result[i++] = c;
  for (char c : table)
    result[i++] = c;
  result[i++] = '_';
  for (char c : column)
    result[i++] = c;
  return result;
}
template <class Model, auto Member>
inline constexpr auto row_parameter_storage =
    row_parameter_name<Model, Member>();
} // namespace liorm

export namespace liorm {
// The handles borrow their QueryBuilder, just like Query and TypedBoundTable.
template <class Model> struct InsertRowsTarget {
  query::TypedBoundTable<Model> target;
  query::Query statement;
  constexpr auto table() const { return target; }
  constexpr auto query() const { return statement; }
};

// A fixed participating-column shape. Omitted fields are not bound, so database
// defaults remain the owner. A row accessor supplies precisely these values.
template <class Policy, class Model, auto... Members>
  requires(sizeof...(Members) > 0) &&
          ((std::is_same_v<
               typename schema::MemberPointer<decltype(Members)>::owner_type,
               Model>) &&
           ...) &&
          ((Policy::template supports<typename schema::MemberPointer<
                decltype(Members)>::value_type>()) &&
           ...) &&
          (distinct_row_members<Members...>())
class InsertRowsShape {
  template <auto Member>
  using Value = typename schema::MemberPointer<decltype(Member)>::value_type;
  using Values = std::tuple<Value<Members>...>;

public:
  static constexpr std::size_t column_count = sizeof...(Members);
  template <auto Member>
  static constexpr auto parameter_name() -> std::string_view {
    const auto &name = row_parameter_storage<Model, Member>;
    return {name.data(), name.size()};
  }
  constexpr auto insert(query::QueryBuilder &builder,
                        const schema::Schema &schema, rstd::usize count) const
      -> InsertRowsTarget<Model> {
    auto table = builder.use(schema).template table<Model>();
    const std::array columns{table.template col<Members>()...};
    const std::array groups{builder.template member_param_group<Members>(
        parameter_name<Members>(), count,
        Policy::template parameter_type<Value<Members>>())...};
    auto statement =
        builder
            .insert_into(table.source(), std::span<const query::Expr>(columns))
            .values_groups(std::span<const query::ParameterGroup>(groups));
    return {table, statement};
  }

  template <class Query, class Writer, class GetRow>
    requires std::is_same_v<std::remove_cvref_t<Writer>, Policy>
  auto bind(const Query &query, Writer &writer, rstd::usize count,
            GetRow get_row) const
      -> rstd::Result<rstd::empty, BindError<typename Writer::error_type>> {
    using Error = BindError<typename Writer::error_type>;
    using Row = std::remove_cvref_t<decltype(get_row(std::size_t{}))>;
    static_assert(std::tuple_size_v<Row> == column_count,
                  "row accessor must provide exactly the selected columns");
    static_assert(
        []<std::size_t... I>(std::index_sequence<I...>) {
          return (
              std::is_same_v<std::remove_cvref_t<std::tuple_element_t<I, Row>>,
                             std::tuple_element_t<I, Values>> &&
              ...);
        }(std::make_index_sequence<column_count>{}),
        "row accessor value types must match their selected members");
    std::array<ValidatedParameterGroup, column_count> groups{};
    rstd::Option<Error> failure{rstd::None()};
    std::size_t column = 0;
    auto validate = [&]<auto Member>() {
      auto index = column++;
      if (failure.is_some())
        return;
      auto result = validate_parameter_group<Value<Member>>(
          query, writer, parameter_name<Member>(), count.to_primitive());
      if (result.is_err()) {
        failure = rstd::Some(rstd::move(result).unwrap_err());
        return;
      }
      auto group = rstd::move(result).unwrap();
      if (!query.template parameter_is<Member>(group.parameter_index)) {
        failure = rstd::Some(Error{BindErrorKind::ForeignMember,
                                   group.parameter_index, rstd::None()});
        return;
      }
      groups[index] = group;
    };
    (validate.template operator()<Members>(), ...);
    if (failure.is_some())
      return rstd::Err(rstd::move(failure).unwrap());
    for (std::size_t row_index = 0; row_index < count.to_primitive();
         ++row_index) {
      decltype(auto) row = get_row(row_index);
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        auto write = [&]<std::size_t Index>() {
          if (failure.is_some())
            return;
          auto result =
              writer.template write<std::tuple_element_t<Index, Values>>(
                  groups[Index].first + rstd::usize(row_index),
                  std::get<Index>(row));
          if (result.is_err())
            failure = rstd::Some(
                Error{BindErrorKind::Write, groups[Index].parameter_index,
                      rstd::Some(rstd::move(result).unwrap_err())});
        };
        (write.template operator()<I>(), ...);
      }(std::make_index_sequence<column_count>{});
      if (failure.is_some())
        return rstd::Err(rstd::move(failure).unwrap());
    }
    return rstd::Ok(rstd::empty{});
  }
};
} // namespace liorm
