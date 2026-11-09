module;
export module liorm:table;
export import :schema;
import rstd;
using namespace rstd::prelude;

namespace liorm::schema {
template <class T> inline constexpr bool member_object_pointer = false;
template <class F, class O>
inline constexpr bool member_object_pointer<F O::*> = !rstd::mtp::is_func<F>;
} // namespace liorm::schema

export namespace liorm::schema {
template <rstd::size_t N> struct FixedString {
  rstd::byte value[N];
  constexpr FixedString(const char (&text)[N]) {
    for (rstd::size_t i = 0; i < N; ++i)
      value[i] = rstd::byte {static_cast<unsigned char>(text[i])};
  }
  constexpr auto view() const -> ref<str> {
    return ref<str>::from_raw_parts_unchecked(value, usize(N - 1));
  }
};

template <class T> struct MemberPointer;
template <class Field, class Owner> struct MemberPointer<Field Owner::*> {
  using owner_type = Owner;
  using value_type = Field;
};

template <auto Left, auto Right> consteval auto same_member() -> bool {
  if constexpr (rstd::mtp::same_as<decltype(Left), decltype(Right)>)
    return Left == Right;
  else
    return false;
}

template <auto Member, FixedString Name, ValueKind Kind, bool Nullable = false,
          bool Primary = false>
struct ColumnSpec {
  static_assert(member_object_pointer<decltype(Member)>);
  using owner_type = typename MemberPointer<decltype(Member)>::owner_type;
  using value_type = typename MemberPointer<decltype(Member)>::value_type;
  static constexpr auto member = Member;
  static constexpr auto name = Name;
  static constexpr ValueType type{Kind, Nullable};
  static constexpr bool primary = Primary;
};

template <class Model, FixedString Name, FixedString PrimaryName,
          class... Fields>
struct TableDefinition {
  static_assert((rstd::mtp::same_as<typename Fields::owner_type, Model> && ...),
                "columns must belong to the table model");
  static constexpr auto name = Name;
  static constexpr auto primary_name = PrimaryName;
  using model_type = Model;
  static constexpr rstd::size_t field_count = sizeof...(Fields);
  template <rstd::size_t Index>
  using field = rstd::mtp::tuple_element<Index, rstd::tuple<Fields...>>;

  template <auto Member>
  static constexpr rstd::size_t member_count =
      (rstd::size_t{0} + ... +
       rstd::size_t(same_member<Member, Fields::member>()));
  static_assert(((member_count<Fields::member> == 1) && ...),
                "duplicate model member");

  template <auto Member> static consteval auto member_index() -> rstd::size_t {
    static_assert(member_count<Member> == 1, "member is not a table column");
    constexpr array<bool, sizeof...(Fields)> matches{
        same_member<Member, Fields::member>()...};
    for (usize i {}; i < matches.len(); ++i)
      if (matches[i])
        return rstd::size_t(i.to_primitive());
    return 0;
  }
  template <auto Member>
    requires(member_count<Member> == 1)
  static constexpr auto column_name() -> ref<str> {
    constexpr array<ref<str>, sizeof...(Fields)> names {Fields::name.view()...};
    return names[usize(member_index<Member>())];
  }

  struct Registration {
    Table table;
    Key primary_key;
    array<Column, sizeof...(Fields)> columns;
    template <auto Member>
      requires(member_count<Member> == 1)
    constexpr auto column() const -> Column {
      return columns[usize(member_index<Member>())];
    }
  };

  static constexpr auto register_in(SchemaBuilder &builder) -> Registration {
    auto table = builder.table(name.view());
    array<Column, sizeof...(Fields)> columns{
        table.column(Fields::name.view(), Fields::type)...};
    constexpr auto primary_count =
        (rstd::size_t{0} + ... + rstd::size_t(Fields::primary));
    array<Column, primary_count> primary{};
    usize index{}, output{};
    ((Fields::primary ? (void)(primary[output++] = columns[index]) : (void)0,
      ++index),
     ...);
    auto key = table.primary_key(primary_name.view(),
                                 primary.as_slice());
    return {table, key, columns};
  }
};

template <class Model> constexpr auto register_table(SchemaBuilder &builder) {
  using Definition =
      decltype(liorm_table_definition(static_cast<Model *>(nullptr)));
  return Definition::register_in(builder);
}

template <class Model> constexpr auto table_name() -> ref<str> {
  using Definition =
      decltype(liorm_table_definition(static_cast<Model *>(nullptr)));
  return Definition::name.view();
}

template <auto Member>
  requires requires {
    decltype(liorm_table_definition(
        static_cast<typename MemberPointer<decltype(Member)>::owner_type *>(
            nullptr)))::template column_name<Member>();
  }
constexpr auto column_name() -> ref<str> {
  using Model = typename MemberPointer<decltype(Member)>::owner_type;
  using Definition =
      decltype(liorm_table_definition(static_cast<Model *>(nullptr)));
  return Definition::template column_name<Member>();
}
} // namespace liorm::schema
