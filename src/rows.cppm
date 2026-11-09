module;
export module liorm:rows;
import rstd;
export import :bind;
export import :query;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace liorm {
template <auto... Members> consteval auto distinct_row_members() -> bool {
  array<const void *, sizeof...(Members)> ids{
      &query::member_parameter_identity<Members>...};
  for (usize i {}; i < ids.len(); ++i)
    for (usize j = i + usize(1); j < ids.len(); ++j)
      if (ids[i] == ids[j])
        return false;
  return true;
}
template <class Model, auto Member> consteval auto row_parameter_name() {
  constexpr auto table = schema::table_name<Model>();
  constexpr auto column = schema::column_name<Member>();
  constexpr ref<str> prefix = "_rows_"_str;
  array<rstd::byte, prefix.len().to_primitive() + table.len().to_primitive() + 1 +
                        column.len().to_primitive()>
         result{};
  usize i {};
  for (usize k {}; k < prefix.len(); ++k)
    result[i++] = rstd::byte {prefix[k].to_primitive()};
  for (usize k {}; k < table.len(); ++k)
    result[i++] = rstd::byte {table[k].to_primitive()};
  result[i++] = rstd::byte {static_cast<unsigned char>('_')};
  for (usize k {}; k < column.len(); ++k)
    result[i++] = rstd::byte {column[k].to_primitive()};
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
          ((rstd::mtp::same_as<
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
  using Values = rstd::tuple<Value<Members>...>;

public:
  static constexpr rstd::size_t column_count = sizeof...(Members);
  template <auto Member>
  static constexpr auto parameter_name() -> ref<str> {
    const auto &name = row_parameter_storage<Model, Member>;
    return ref<str>::from_raw_parts_unchecked(
        static_cast<rstd::byte const *>(name.data()),
        usize(name.len().to_primitive()));
  }
  constexpr auto insert(query::QueryBuilder &builder,
                        const schema::Schema &schema, rstd::usize count) const
      -> InsertRowsTarget<Model> {
    auto table = builder.use(schema).template table<Model>();
    const array<query::Expr, sizeof...(Members)> columns {
        table.template col<Members>()...};
    const array<query::ParameterGroup, sizeof...(Members)> groups {
        builder.template member_param_group<Members>(
            parameter_name<Members>(), count,
            Policy::template parameter_type<Value<Members>>())...};
    auto statement =
        builder
            .insert_into(table.source(), columns.as_slice())
            .values_groups(groups.as_slice());
    return {table, statement};
  }

  template <class Query, class Writer, class GetRow>
    requires rstd::mtp::same_as<rstd::mtp::rm_cvf<Writer>, Policy>
  auto bind(const Query &query, Writer &writer, rstd::usize count,
            GetRow get_row) const
      -> rstd::Result<rstd::empty, BindError<typename Writer::error_type>> {
    using Error = BindError<typename Writer::error_type>;
    using Row = rstd::mtp::rm_cvf<decltype(get_row(usize{}))>;
    static_assert(rstd::mtp::tuple_size<Row> == column_count,
                  "row accessor must provide exactly the selected columns");
    static_assert(
        []<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
          return (
              rstd::mtp::same_as<rstd::mtp::rm_cvf<rstd::mtp::tuple_element<I, Row>>,
                             rstd::mtp::tuple_element<I, Values>> &&
              ...);
        }(rstd::mtp::make_index_sequence<column_count>{}),
        "row accessor value types must match their selected members");
    array<ValidatedParameterGroup, column_count> groups{};
    rstd::Option<Error> failure{rstd::None()};
    usize column {};
    auto validate = [&]<auto Member>() {
      auto index = column;
      ++column;
      if (failure.is_some())
        return;
      auto result = validate_parameter_group<Value<Member>>(
          query, writer, parameter_name<Member>(), count);
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
      groups[usize(index)] = group;
    };
    (validate.template operator()<Members>(), ...);
    if (failure.is_some())
      return rstd::Err(rstd::move(failure).unwrap());
    for (usize row_index {}; row_index < count; ++row_index) {
      decltype(auto) row = get_row(row_index);
      [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
        auto write = [&]<rstd::size_t Index>() {
          if (failure.is_some())
            return;
          auto result =
              writer.template write<rstd::mtp::tuple_element<Index, Values>>(
                  groups[usize(Index)].first + row_index,
                  rstd::get<Index>(row));
          if (result.is_err())
            failure = rstd::Some(
                Error{BindErrorKind::Write, groups[usize(Index)].parameter_index,
                      rstd::Some(rstd::move(result).unwrap_err())});
        };
        (write.template operator()<I>(), ...);
      }(rstd::mtp::make_index_sequence<column_count>{});
      if (failure.is_some())
        return rstd::Err(rstd::move(failure).unwrap());
    }
    return rstd::Ok(rstd::empty{});
  }
};
} // namespace liorm
