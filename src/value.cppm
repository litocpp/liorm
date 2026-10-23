module;
#include <string_view>
export module liorm:value;
export import rstd.core;

export namespace liorm {
enum class ValueKind { Integer, Real, Boolean, Text, Blob };
struct ValueType {
  ValueKind kind{};
  bool nullable{};
  constexpr bool operator==(const ValueType &) const = default;
};
struct TextParameter {};
struct BlobParameter {};
template <class T> struct Nullable {};
template <class T> struct ValueTraits {};
template <> struct ValueTraits<rstd::i32> {
  static constexpr ValueType type{ValueKind::Integer, false};
};
template <> struct ValueTraits<rstd::i64> : ValueTraits<rstd::i32> {};
template <> struct ValueTraits<rstd::f64> {
  static constexpr ValueType type{ValueKind::Real, false};
};
template <> struct ValueTraits<bool> {
  static constexpr ValueType type{ValueKind::Boolean, false};
};
template <> struct ValueTraits<TextParameter> {
  static constexpr ValueType type{ValueKind::Text, false};
};
template <> struct ValueTraits<BlobParameter> {
  static constexpr ValueType type{ValueKind::Blob, false};
};
template <class T>
  requires requires { ValueTraits<T>::type; }
struct ValueTraits<Nullable<T>> {
  static constexpr ValueType type{ValueTraits<T>::type.kind, true};
};
constexpr auto valid_type(ValueType t) -> bool {
  return unsigned(t.kind) <= unsigned(ValueKind::Blob);
}
constexpr auto numeric(ValueKind k) -> bool {
  return k == ValueKind::Integer || k == ValueKind::Real;
}
constexpr auto comparable(ValueKind a, ValueKind b) -> bool {
  return a == b || (numeric(a) && numeric(b));
}
constexpr auto assignable(ValueKind target, ValueKind value) -> bool {
  return target == value ||
         (target == ValueKind::Real && value == ValueKind::Integer);
}
constexpr auto valid_name(std::string_view value) -> bool {
  if (value.empty())
    return false;
  for (char c : value)
    if (!c)
      return false;
  return true;
}
constexpr auto same_name(std::string_view a, std::string_view b) -> bool {
  if (a.size() != b.size())
    return false;
  auto fold = [](char c) {
    return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c;
  };
  for (unsigned i = 0; i < a.size(); ++i)
    if (fold(a[i]) != fold(b[i]))
      return false;
  return true;
}
} // namespace liorm

export namespace liorm::query {
using ParameterKind = ValueKind;
using ParameterType = ValueType;
using liorm::BlobParameter;
using liorm::Nullable;
using liorm::TextParameter;
template <class T> struct ParameterTraits : ValueTraits<T> {};
template <class T>
  requires requires { ParameterTraits<T>::type; }
struct ParameterTraits<Nullable<T>> {
  static constexpr ParameterType type{ParameterTraits<T>::type.kind, true};
};
} // namespace liorm::query
