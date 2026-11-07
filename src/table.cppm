module;
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
export module liorm:table;
export import :schema;

export namespace liorm::schema {
template <std::size_t N> struct FixedString {
  char value[N];
  constexpr FixedString(const char (&text)[N]) {
    for (std::size_t i = 0; i < N; ++i)
      value[i] = text[i];
  }
  constexpr auto view() const -> std::string_view { return {value, N - 1}; }
};

template <class T> struct MemberPointer;
template <class Field, class Owner> struct MemberPointer<Field Owner::*> {
  using owner_type = Owner;
  using value_type = Field;
};

template <auto Left, auto Right> consteval auto same_member() -> bool {
  if constexpr (std::is_same_v<decltype(Left), decltype(Right)>)
    return Left == Right;
  else
    return false;
}

template <auto Member, FixedString Name, ValueKind Kind, bool Nullable = false,
          bool Primary = false>
struct ColumnSpec {
  static_assert(std::is_member_object_pointer_v<decltype(Member)>);
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
  static_assert((std::is_same_v<typename Fields::owner_type, Model> && ...),
                "columns must belong to the table model");
  static constexpr auto name = Name;
  static constexpr auto primary_name = PrimaryName;
  using model_type = Model;
  static constexpr std::size_t field_count = sizeof...(Fields);
  template <std::size_t Index>
  using field = std::tuple_element_t<Index, std::tuple<Fields...>>;

  template <auto Member>
  static constexpr std::size_t member_count =
      (std::size_t{0} + ... +
       std::size_t(same_member<Member, Fields::member>()));
  static_assert(((member_count<Fields::member> == 1) && ...),
                "duplicate model member");

  template <auto Member> static consteval auto member_index() -> std::size_t {
    static_assert(member_count<Member> == 1, "member is not a table column");
    constexpr std::array<bool, sizeof...(Fields)> matches{
        same_member<Member, Fields::member>()...};
    for (std::size_t i = 0; i < matches.size(); ++i)
      if (matches[i])
        return i;
    return 0;
  }
  template <auto Member>
    requires(member_count<Member> == 1)
  static constexpr auto column_name() -> std::string_view {
    constexpr std::array<std::string_view, sizeof...(Fields)> names{
        Fields::name.view()...};
    return names[member_index<Member>()];
  }

  struct Registration {
    Table table;
    Key primary_key;
    std::array<Column, sizeof...(Fields)> columns;
    template <auto Member>
      requires(member_count<Member> == 1)
    constexpr auto column() const -> Column {
      return columns[member_index<Member>()];
    }
  };

  static constexpr auto register_in(SchemaBuilder &builder) -> Registration {
    auto table = builder.table(name.view());
    std::array<Column, sizeof...(Fields)> columns{
        table.column(Fields::name.view(), Fields::type)...};
    constexpr auto primary_count =
        (std::size_t{0} + ... + std::size_t(Fields::primary));
    std::array<Column, primary_count> primary{};
    std::size_t index = 0, output = 0;
    ((Fields::primary ? (void)(primary[output++] = columns[index]) : (void)0,
      ++index),
     ...);
    auto key = table.primary_key(primary_name.view(),
                                 std::span<const Column>(primary));
    return {table, key, columns};
  }
};

template <class Model> constexpr auto register_table(SchemaBuilder &builder) {
  using Definition =
      decltype(liorm_table_definition(static_cast<Model *>(nullptr)));
  return Definition::register_in(builder);
}

template <class Model> constexpr auto table_name() -> std::string_view {
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
constexpr auto column_name() -> std::string_view {
  using Model = typename MemberPointer<decltype(Member)>::owner_type;
  using Definition =
      decltype(liorm_table_definition(static_cast<Model *>(nullptr)));
  return Definition::template column_name<Member>();
}
} // namespace liorm::schema
