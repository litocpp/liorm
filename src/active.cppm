module;
#include <rstd/enum.hpp>
export module liorm:active;
export import :table;
import rstd;

export namespace liorm
{
template<class T>
class ActiveValue {
    class State {
        RSTD_ENUM_DEFAULT(State, (NotSet), (NotSet), (Unchanged, (T value;)), (Set, (T value;)))
    } state_;

public:
    constexpr ActiveValue() = default;
    constexpr auto is_not_set() const -> bool { return state_.is_NotSet(); }
    constexpr auto is_unchanged() const -> bool { return state_.is_Unchanged(); }
    constexpr auto is_set() const -> bool { return state_.is_Set(); }
    constexpr auto has_value() const -> bool { return ! is_not_set(); }
    constexpr auto value() const& -> const T* {
        if (state_.is_Set()) return &state_.as_Set().value;
        if (state_.is_Unchanged()) return &state_.as_Unchanged().value;
        return nullptr;
    }
    auto           value() const&& -> const T* = delete;
    constexpr void set(T value) { state_.replace_Set(rstd::move(value)); }
    constexpr void unchanged(T value) { state_.replace_Unchanged(rstd::move(value)); }
    constexpr void unset() { state_.replace_NotSet(); }
    constexpr void reset() {
        if (state_.is_Unchanged()) {
            T value(rstd::move(state_.as_Unchanged().value));
            state_.replace_Set(rstd::move(value));
        }
    }
};

template<class Definition, class Sequence>
struct ActiveSlots;
template<class Definition, rstd::size_t... I>
struct ActiveSlots<Definition, rstd::mtp::index_sequence<I...>> {
    using type = rstd::tuple<ActiveValue<typename Definition::template field<I>::value_type>...>;
};

template<class Model>
class ActiveModel {
public:
    using definition = decltype(liorm_table_definition(static_cast<Model*>(nullptr)));
    static constexpr rstd::size_t field_count = definition::field_count;
    template<rstd::size_t I>
    using field_type = typename definition::template field<I>;

private:
    typename ActiveSlots<definition, rstd::mtp::make_index_sequence<field_count>>::type fields_;

public:
    constexpr ActiveModel() = default;
    template<auto Member>
        requires(definition::template member_count<Member> == 1)
    constexpr auto field() & -> auto& {
        return rstd::get<definition::template member_index<Member>()>(fields_);
    }
    template<auto Member>
        requires(definition::template member_count<Member> == 1)
    constexpr auto field() const& -> const auto& {
        return rstd::get<definition::template member_index<Member>()>(fields_);
    }
    template<auto Member>
    auto field() && -> void = delete;
    template<auto Member>
    auto field() const&& -> void = delete;
    template<rstd::size_t I>
    constexpr auto field_at() const& -> const auto& {
        return rstd::get<I>(fields_);
    }
    template<rstd::size_t I>
    auto                  field_at() const&& -> void = delete;
    static constexpr auto from_model(Model&& model) -> ActiveModel {
        ActiveModel result;
        [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
            (result.template field<field_type<I>::member>().unchanged(
                 rstd::move(model.*field_type<I>::member)),
             ...);
        }(rstd::mtp::make_index_sequence<field_count> {});
        return result;
    }
    constexpr void reset_all() {
        rstd::apply(
            [](auto&... slot) {
                (slot.reset(), ...);
            },
            fields_);
    }
};
} // namespace liorm
