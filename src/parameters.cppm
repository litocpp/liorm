export module liorm:parameters;
export import :bind;
import rstd;
using namespace rstd::prelude;

export namespace liorm
{
template<class T>
struct ParameterValue {
    using type = T;
    template<class Value>
        requires rstd::mtp::init<T, Value>
    static auto own(Value&& value) -> T {
        return T(rstd::forward<Value>(value));
    }
};
template<>
struct ParameterValue<ref<str>> {
    using type = String;
    static auto own(ref<str> value) -> String { return String::make(value); }
};
template<>
struct ParameterValue<slice<u8>> {
    using type = Vec<u8>;
    static auto own(slice<u8> value) -> Vec<u8> {
        Vec<u8> result;
        result.extend_from_slice(value);
        return result;
    }
};
template<class T>
struct ParameterValue<Option<T>> {
    using type = Option<typename ParameterValue<T>::type>;
    template<class Value>
        requires rstd::mtp::init<Option<T>, Value>
    static auto own(Value&& value) -> type {
        auto owned = Option<T>(rstd::forward<Value>(value));
        if (owned.is_none()) return None();
        return Some(ParameterValue<T>::own(rstd::move(owned).unwrap()));
    }
};

template<schema::FixedString Name, class T>
struct NamedParameter {
    using value_type           = T;
    static constexpr auto name = Name;
    T                     value;
};

template<schema::FixedString Name, class Value>
    requires requires(Value&& value) {
        ParameterValue<rstd::mtp::rm_cvf<Value>>::own(rstd::forward<Value>(value));
    }
auto param(Value&& value) {
    using Owner = ParameterValue<rstd::mtp::rm_cvf<Value>>;
    return NamedParameter<Name, typename Owner::type> { Owner::own(rstd::forward<Value>(value)) };
}
} // namespace liorm

namespace liorm
{
template<class T>
inline constexpr bool named_parameter = false;
template<schema::FixedString Name, class T>
inline constexpr bool named_parameter<NamedParameter<Name, T>> =
    rstd::mtp::same_as<T, typename ParameterValue<T>::type>;

template<const query::CompiledQuery& Query, schema::FixedString Name>
consteval auto static_parameter_index() -> usize {
    for (usize i {}; i < Query.parameter_count(); ++i)
        if (Query.parameter_name(i) == Name.view()) return i;
    return Query.parameter_count();
}
} // namespace liorm

export namespace liorm
{
template<class... Fields>
    requires(named_parameter<Fields> && ...)
class Parameters {
    rstd::tuple<Fields...> fields_;

    static consteval auto distinct() -> bool {
        const array<ref<str>, sizeof...(Fields)> names { Fields::name.view()... };
        for (usize i {}; i < names.len(); ++i)
            for (usize j {}; j < i; ++j)
                if (names[i] == names[j]) return false;
        return true;
    }
    template<const query::CompiledQuery& Query>
    static consteval auto complete() -> bool {
        for (usize i {}; i < Query.parameter_count(); ++i)
            if (! ((Query.parameter_name(i) == Fields::name.view()) || ...)) return false;
        return true;
    }
    template<const query::CompiledQuery& Query, class Writer, class Field>
    static consteval auto compatible() -> bool {
        constexpr auto index = static_parameter_index<Query, Field::name>();
        if constexpr (index >= Query.parameter_count())
            return true;
        else if constexpr (! Writer::template supports<typename Field::value_type>())
            return false;
        else
            return Writer::template compatible<typename Field::value_type>(
                Query.parameter(index)->type);
    }

public:
    explicit Parameters(Fields... fields): fields_(rstd::move(fields)...) {}

    template<const query::CompiledQuery& Query, class Writer>
    static consteval void validate() {
        static_assert(distinct(), "duplicate static query parameter");
        static_assert(complete<Query>(), "missing static query parameter");
        static_assert(
            ((static_parameter_index<Query, Fields::name>() < Query.parameter_count()) && ...),
            "extra static query parameter");
        static_assert((compatible<Query, Writer, Fields>() && ...),
                      "incompatible static query parameter type");
    }
    template<const query::CompiledQuery& Query, class Writer>
    auto bind(Writer& writer) const -> Result<empty, BindError<typename Writer::error_type>> {
        validate<Query, Writer>();
        Option<BindError<typename Writer::error_type>> failure;
        rstd::apply(
            [&](const auto&... fields) {
                auto bind = [&]<class Field>(const Field& field) {
                    if (failure.is_some()) return;
                    constexpr auto index = static_parameter_index<Query, Field::name>();
                    if constexpr (index < Query.parameter_count()) {
                        constexpr auto metadata = ValidatedParameterGroup {
                            index, Query.parameter(index)->slot, usize(1)
                        };
                        auto result =
                            bind_indexed_values(metadata, writer, [&](usize) -> const auto& {
                                return field.value;
                            });
                        if (result.is_err()) failure = Some(rstd::move(result).unwrap_err());
                    }
                };
                (bind(fields), ...);
            },
            fields_);
        if (failure.is_some()) return Err(rstd::move(failure).unwrap());
        return Ok(empty {});
    }
};

template<class... Fields>
    requires(named_parameter<Fields> && ...)
auto parameters(Fields... fields) -> Parameters<Fields...> {
    return Parameters<Fields...>(rstd::move(fields)...);
}
} // namespace liorm
