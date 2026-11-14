export module liorm:changeset;
export import :active;
import rstd;

export namespace liorm
{
template<auto Member>
    requires schema::member_object_pointer<decltype(Member)>
struct Assignment {
    using model_type             = typename schema::MemberPointer<decltype(Member)>::owner_type;
    using value_type             = typename schema::MemberPointer<decltype(Member)>::value_type;
    static constexpr auto member = Member;
    value_type            value;
};

template<auto Member, class Value>
    requires schema::member_object_pointer<decltype(Member)> &&
             rstd::mtp::init<typename schema::MemberPointer<decltype(Member)>::value_type, Value>
constexpr auto set(Value&& value) -> Assignment<Member> {
    using T = typename Assignment<Member>::value_type;
    return { T(rstd::forward<Value>(value)) };
}

template<auto Member>
    requires schema::member_object_pointer<decltype(Member)>
struct Column {
    using model_type             = typename schema::MemberPointer<decltype(Member)>::owner_type;
    using value_type             = typename schema::MemberPointer<decltype(Member)>::value_type;
    static constexpr auto member = Member;

    template<class Value>
        requires rstd::mtp::init<value_type, Value>
    constexpr auto operator=(Value&& value) const -> Assignment<Member> {
        return liorm::set<Member>(rstd::forward<Value>(value));
    }
};

template<class Model>
using Columns = decltype(liorm_columns_definition(static_cast<Model*>(nullptr)));
} // namespace liorm

namespace liorm
{
template<class T>
struct IsAssignment {
    static constexpr bool value = false;
};
template<auto Member>
struct IsAssignment<Assignment<Member>> {
    static constexpr bool value = true;
};
template<class T>
concept FieldAssignment = IsAssignment<rstd::mtp::rm_cvf<T>>::value;

enum class WriteFields
{
    Insert,
    Changes,
    Key
};

template<class Model>
using ModelDefinition = decltype(liorm_table_definition(static_cast<Model*>(nullptr)));

template<class Model, WriteFields Mode, auto Member>
consteval auto accepts_write_member() -> bool {
    using Definition = ModelDefinition<Model>;
    if constexpr (Definition::template member_count<Member> != 1)
        return false;
    else {
        constexpr bool primary =
            Definition::template field<Definition::template member_index<Member>()>::primary;
        return Mode == WriteFields::Insert || (Mode == WriteFields::Key ? primary : ! primary);
    }
}

template<class Model, WriteFields Mode, class T>
consteval auto accepts_assignment() -> bool {
    if constexpr (! FieldAssignment<T>)
        return false;
    else
        return accepts_write_member<Model, Mode, rstd::mtp::rm_cvf<T>::member>();
}

template<class... Fields>
struct UniqueAssignments {
    static constexpr bool value = true;
};
template<class First, class... Rest>
struct UniqueAssignments<First, Rest...> {
    static constexpr bool value = ((! schema::same_member<First::member, Rest::member>()) && ...) &&
                                  UniqueAssignments<Rest...>::value;
};

template<class Model, WriteFields Mode>
class WriteValues {
    ActiveModel<Model> values_;

public:
    constexpr WriteValues() = default;
    template<class... Fields>
        requires(sizeof...(Fields) > 0 && (accepts_assignment<Model, Mode, Fields>() && ...))
    constexpr explicit WriteValues(Fields... fields) {
        static_assert(UniqueAssignments<Fields...>::value, "duplicate write assignment");
        (set<Fields::member>(rstd::move(fields.value)), ...);
    }

    template<auto Member, class Value>
        requires(accepts_write_member<Model, Mode, Member>()) &&
                rstd::mtp::init<typename schema::MemberPointer<decltype(Member)>::value_type, Value>
    constexpr void set(Value&& value) {
        using T = typename schema::MemberPointer<decltype(Member)>::value_type;
        values_.template field<Member>().set(T(rstd::forward<Value>(value)));
    }
    template<auto Member>
        requires(accepts_write_member<Model, Mode, Member>())
    constexpr auto value() const& {
        return values_.template field<Member>().value();
    }
    template<auto Member>
    auto value() const&& = delete;

    constexpr auto empty() const -> bool {
        return [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
            return ((! values_.template field_at<I>().has_value()) && ...);
        }(rstd::mtp::make_index_sequence<ActiveModel<Model>::field_count> {});
    }
    constexpr auto into_active() && -> ActiveModel<Model> { return rstd::move(values_); }
};

template<class Model>
consteval auto primary_field_count() -> rstd::size_t {
    using Definition = ModelDefinition<Model>;
    return []<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
        return (rstd::size_t(0) + ... + rstd::size_t(Definition::template field<I>::primary));
    }(rstd::mtp::make_index_sequence<Definition::field_count> {});
}
template<class Model>
consteval auto first_primary_field() -> rstd::size_t {
    using Definition      = ModelDefinition<Model>;
    constexpr auto fields = []<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
        return rstd::array<bool, sizeof...(I)> { Definition::template field<I>::primary... };
    }(rstd::mtp::make_index_sequence<Definition::field_count> {});
    for (rstd::usize i {}; i < fields.len(); ++i)
        if (fields[i]) return i.to_primitive();
    return 0;
}
} // namespace liorm

export namespace liorm
{
template<class Model>
class InsertValues : public WriteValues<Model, WriteFields::Insert> {
    using Base = WriteValues<Model, WriteFields::Insert>;

public:
    using Base::Base;
};

template<class Model>
class Changeset : public WriteValues<Model, WriteFields::Changes> {
    using Base = WriteValues<Model, WriteFields::Changes>;

public:
    using Base::Base;
};

template<class Model>
class Key : public WriteValues<Model, WriteFields::Key> {
    using Base    = WriteValues<Model, WriteFields::Key>;
    using Primary = typename ModelDefinition<Model>::template field<first_primary_field<Model>()>;

public:
    static constexpr auto field_count = primary_field_count<Model>();
    static_assert(field_count > 0, "update key requires a primary field");

    template<class... Fields>
        requires(sizeof...(Fields) > 0 &&
                 (accepts_assignment<Model, WriteFields::Key, Fields>() && ...))
    constexpr explicit Key(Fields... fields): Base(rstd::move(fields)...) {
        static_assert(sizeof...(Fields) == field_count, "incomplete primary key");
    }
    template<class Value>
        requires(field_count == 1 && ! FieldAssignment<Value> &&
                 rstd::mtp::init<typename Primary::value_type, Value>)
    constexpr explicit Key(Value&& value)
        : Base(liorm::set<Primary::member>(rstd::forward<Value>(value))) {}
};

template<class Model>
auto update_values(Key<Model> key, Changeset<Model> changes) -> ActiveModel<Model> {
    auto values      = rstd::move(changes).into_active();
    auto keys        = rstd::move(key).into_active();
    using Definition = ModelDefinition<Model>;
    [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
        auto assign = [&]<rstd::size_t Index>() {
            using Field = typename Definition::template field<Index>;
            if constexpr (Field::primary)
                values.template field<Field::member>().unchanged(
                    keys.template field<Field::member>().take().unwrap());
        };
        (assign.template operator()<I>(), ...);
    }(rstd::mtp::make_index_sequence<Definition::field_count> {});
    return values;
}
} // namespace liorm
