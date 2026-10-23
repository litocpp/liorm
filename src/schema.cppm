module;
#include <array>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
export module liorm:schema;
export import :value;

export namespace liorm::schema {
enum class ErrorKind {
  None,
  Capacity,
  InvalidHandle,
  ForeignOwner,
  InvalidName,
  DuplicateName,
  UnsupportedType,
  EmptyKey,
  DuplicateColumn,
  CrossTable,
  NullablePrimaryKey,
  DuplicatePrimaryKey,
  InvalidTarget,
  ColumnCount,
  TypeMismatch
};
struct Error {
  ErrorKind kind{};
  std::array<char, 96> context{};
  unsigned length{};
  bool truncated{};
  constexpr auto name() const -> std::string_view {
    return {context.data(), length};
  }
};
struct SchemaLimits {
  unsigned tables = 64, columns = 512, keys = 128, relations = 128;
  unsigned members = 1024, text = 8192;
};
constexpr auto valid_limits(SchemaLimits l) -> bool {
  return l.tables <= 64 && l.columns <= 512 && l.keys <= 128 &&
         l.relations <= 128 && l.members <= 1024 && l.text <= 8192;
}
struct TableInfo {
  std::string_view name;
};
struct ColumnInfo {
  std::string_view name;
  unsigned table{};
  ValueType type{};
};
struct KeyInfo {
  std::string_view name;
  unsigned table{};
  std::span<const unsigned> columns;
  bool primary{};
};
struct ForeignKeyInfo {
  std::string_view name;
  unsigned table{}, target{};
  std::span<const unsigned> columns;
};
class SchemaBuilder;
class Registry;
} // namespace liorm::schema
namespace liorm::schema {
struct Text {
  unsigned offset{}, length{};
};
struct TableData {
  Text name;
};
struct ColumnData {
  Text name;
  unsigned table{};
  ValueType type{};
};
struct KeyData {
  Text name;
  unsigned table{}, offset{}, count{};
  bool primary{};
};
struct ForeignKeyData {
  Text name;
  unsigned table{}, target{}, offset{}, count{};
};
} // namespace liorm::schema
export namespace liorm::schema {
class Schema {
  friend class SchemaBuilder;
  friend class Registry;
  std::array<TableData, 64> tables_{};
  std::array<ColumnData, 512> columns_{};
  std::array<KeyData, 128> keys_{};
  std::array<ForeignKeyData, 128> relations_{};
  std::array<unsigned, 1024> members_{};
  std::array<char, 8192> text_{};
  unsigned tables_size_{}, columns_size_{}, keys_size_{}, relations_size_{},
      members_size_{}, text_size_{};
  constexpr auto text(Text t) const -> std::string_view {
    return {text_.data() + t.offset, t.length};
  }

public:
  constexpr auto table_count() const -> unsigned { return tables_size_; }
  constexpr auto column_count() const -> unsigned { return columns_size_; }
  constexpr auto key_count() const -> unsigned { return keys_size_; }
  constexpr auto foreign_key_count() const -> unsigned {
    return relations_size_;
  }
  constexpr auto table(unsigned i) const -> TableInfo {
    return {text(tables_.at(i).name)};
  }
  constexpr auto column(unsigned i) const -> ColumnInfo {
    const auto &c = columns_.at(i);
    return {text(c.name), c.table, c.type};
  }
  constexpr auto key(unsigned i) const -> KeyInfo {
    const auto &k = keys_.at(i);
    return {text(k.name),
            k.table,
            {members_.data() + k.offset, k.count},
            k.primary};
  }
  constexpr auto foreign_key(unsigned i) const -> ForeignKeyInfo {
    const auto &f = relations_.at(i);
    return {
        text(f.name), f.table, f.target, {members_.data() + f.offset, f.count}};
  }
  constexpr auto find_table(std::string_view name) const
      -> std::optional<unsigned> {
    for (unsigned i = 0; i < tables_size_; ++i)
      if (same_name(table(i).name, name))
        return i;
    return {};
  }
  constexpr auto find_column(unsigned table, std::string_view name) const
      -> std::optional<unsigned> {
    for (unsigned i = 0; i < columns_size_; ++i) {
      auto c = column(i);
      if (c.table == table && same_name(c.name, name))
        return i;
    }
    return {};
  }
};
class Column {
  friend class SchemaBuilder;
  SchemaBuilder *owner_{};
  unsigned id_{};
  constexpr Column(SchemaBuilder *b, unsigned id) : owner_(b), id_(id) {}

public:
  constexpr Column() = default;
};
class Key {
  friend class SchemaBuilder;
  SchemaBuilder *owner_{};
  unsigned id_{};
  constexpr Key(SchemaBuilder *b, unsigned id) : owner_(b), id_(id) {}

public:
  constexpr Key() = default;
};
class Table {
  friend class SchemaBuilder;
  SchemaBuilder *owner_{};
  unsigned id_{};
  constexpr Table(SchemaBuilder *b, unsigned id) : owner_(b), id_(id) {}

public:
  constexpr Table() = default;
  constexpr auto column(std::string_view name, ValueType type) const -> Column;
  template <class T>
  constexpr auto column(std::string_view name) const -> Column;
  constexpr auto primary_key(std::string_view name,
                             std::span<const Column> columns) const -> Key;
  constexpr auto unique_key(std::string_view name,
                            std::span<const Column> columns) const -> Key;
  constexpr auto primary_key(std::string_view name,
                             std::initializer_list<Column> columns) const
      -> Key {
    return primary_key(name, std::span(columns.begin(), columns.size()));
  }
  constexpr auto unique_key(std::string_view name,
                            std::initializer_list<Column> columns) const
      -> Key {
    return unique_key(name, std::span(columns.begin(), columns.size()));
  }
};
class SchemaBuilder {
  friend class Table;
  Schema data_{};
  SchemaLimits limits_{};
  Error error_{};
  constexpr void fail(ErrorKind kind, std::string_view name = {}) {
    if (error_.kind != ErrorKind::None)
      return;
    error_.kind = kind;
    error_.length = unsigned(name.size() < 96 ? name.size() : 96);
    error_.truncated = name.size() > 96;
    for (unsigned i = 0; i < error_.length; ++i)
      error_.context[i] = name[i];
  }
  constexpr auto store(std::string_view name) -> Text {
    if (!valid_name(name)) {
      fail(ErrorKind::InvalidName, name);
      return {};
    }
    if (name.size() > limits_.text - data_.text_size_) {
      fail(ErrorKind::Capacity, name);
      return {};
    }
    std::array<rstd::byte, 8192> bytes{};
    for (unsigned i = 0; i < name.size(); ++i)
      bytes[i] = rstd::byte{static_cast<unsigned char>(name[i])};
    if (rstd::str_::from_utf8(rstd::slice<rstd::u8>::from_raw_parts(
                                  bytes.data(), rstd::usize(name.size())))
            .is_err()) {
      fail(ErrorKind::InvalidName, name);
      return {};
    }
    Text t{data_.text_size_, unsigned(name.size())};
    for (char c : name)
      data_.text_[data_.text_size_++] = c;
    return t;
  }
  template <class T>
  constexpr auto check(T h, unsigned count, std::string_view name) -> bool {
    if (!h.owner_ || !h.id_) {
      fail(ErrorKind::InvalidHandle, name);
      return false;
    }
    if (h.owner_ != this) {
      fail(ErrorKind::ForeignOwner, name);
      return false;
    }
    if (h.id_ > count) {
      fail(ErrorKind::InvalidHandle, name);
      return false;
    }
    return true;
  }
  constexpr auto add_column(Table t, std::string_view name, ValueType type)
      -> Column {
    if (!check(t, data_.tables_size_, name))
      return {};
    if (!valid_type(type)) {
      fail(ErrorKind::UnsupportedType, name);
      return {};
    }
    if (data_.find_column(t.id_ - 1, name)) {
      fail(ErrorKind::DuplicateName, name);
      return {};
    }
    if (data_.columns_size_ >= limits_.columns) {
      fail(ErrorKind::Capacity, name);
      return {};
    }
    auto text = store(name);
    unsigned id = data_.columns_size_++;
    data_.columns_[id] = {text, t.id_ - 1, type};
    return Column(this, id + 1);
  }
  constexpr auto members(std::span<const Column> columns, unsigned table,
                         std::string_view name) -> bool {
    if (columns.empty()) {
      fail(ErrorKind::EmptyKey, name);
      return false;
    }
    if (columns.size() > limits_.members - data_.members_size_) {
      fail(ErrorKind::Capacity, name);
      return false;
    }
    for (unsigned i = 0; i < columns.size(); ++i) {
      if (!check(columns[i], data_.columns_size_, name))
        return false;
      if (data_.column(columns[i].id_ - 1).table != table) {
        fail(ErrorKind::CrossTable, name);
        return false;
      }
      for (unsigned j = 0; j < i; ++j)
        if (columns[j].id_ == columns[i].id_) {
          fail(ErrorKind::DuplicateColumn, name);
          return false;
        }
    }
    return true;
  }
  constexpr auto add_key(Table t, std::string_view name,
                         std::span<const Column> columns, bool primary) -> Key {
    if (!check(t, data_.tables_size_, name) ||
        !members(columns, t.id_ - 1, name))
      return {};
    for (unsigned i = 0; i < data_.keys_size_; ++i) {
      auto k = data_.key(i);
      if (k.table != t.id_ - 1)
        continue;
      if (same_name(k.name, name)) {
        fail(ErrorKind::DuplicateName, name);
        return {};
      }
      if (primary && k.primary) {
        fail(ErrorKind::DuplicatePrimaryKey, name);
        return {};
      }
    }
    if (primary)
      for (auto c : columns)
        if (data_.column(c.id_ - 1).type.nullable) {
          fail(ErrorKind::NullablePrimaryKey, name);
          return {};
        }
    if (data_.keys_size_ >= limits_.keys) {
      fail(ErrorKind::Capacity, name);
      return {};
    }
    auto text = store(name);
    unsigned id = data_.keys_size_++;
    data_.keys_[id] = {text, t.id_ - 1, data_.members_size_,
                       unsigned(columns.size()), primary};
    for (auto c : columns)
      data_.members_[data_.members_size_++] = c.id_ - 1;
    return Key(this, id + 1);
  }

public:
  constexpr explicit SchemaBuilder(SchemaLimits limits = {}) : limits_(limits) {
    if (!valid_limits(limits)) {
      fail(ErrorKind::Capacity, "limits");
      limits_ = {};
    }
  }
  SchemaBuilder(const SchemaBuilder &) = delete;
  SchemaBuilder(SchemaBuilder &&) = delete;
  constexpr auto table(std::string_view name) -> Table {
    if (name.find('.') != std::string_view::npos) {
      fail(ErrorKind::InvalidName, name);
      return {};
    }
    if (data_.find_table(name)) {
      fail(ErrorKind::DuplicateName, name);
      return {};
    }
    if (data_.tables_size_ >= limits_.tables) {
      fail(ErrorKind::Capacity, name);
      return {};
    }
    auto text = store(name);
    unsigned id = data_.tables_size_++;
    data_.tables_[id] = {text};
    return Table(this, id + 1);
  }
  constexpr void foreign_key(std::string_view name,
                             std::span<const Column> columns, Key target) {
    if (!check(target, data_.keys_size_, name))
      return;
    if (columns.empty()) {
      fail(ErrorKind::EmptyKey, name);
      return;
    }
    if (!check(columns[0], data_.columns_size_, name))
      return;
    unsigned table = data_.column(columns[0].id_ - 1).table;
    if (!members(columns, table, name))
      return;
    for (unsigned i = 0; i < data_.relations_size_; ++i)
      if (same_name(data_.foreign_key(i).name, name)) {
        fail(ErrorKind::DuplicateName, name);
        return;
      }
    auto key = data_.key(target.id_ - 1);
    if (columns.size() != key.columns.size()) {
      fail(ErrorKind::ColumnCount, name);
      return;
    }
    for (unsigned i = 0; i < columns.size(); ++i) {
      auto a = data_.column(columns[i].id_ - 1).type,
           b = data_.column(key.columns[i]).type;
      if (b.nullable) {
        fail(ErrorKind::InvalidTarget, name);
        return;
      }
      if (!comparable(a.kind, b.kind)) {
        fail(ErrorKind::TypeMismatch, name);
        return;
      }
    }
    if (data_.relations_size_ >= limits_.relations) {
      fail(ErrorKind::Capacity, name);
      return;
    }
    auto text = store(name);
    data_.relations_[data_.relations_size_++] = {text, table, target.id_ - 1,
                                                 data_.members_size_,
                                                 unsigned(columns.size())};
    for (auto c : columns)
      data_.members_[data_.members_size_++] = c.id_ - 1;
  }
  constexpr void foreign_key(std::string_view name,
                             std::initializer_list<Column> columns,
                             Key target) {
    foreign_key(name, std::span(columns.begin(), columns.size()), target);
  }
  constexpr auto finish() const -> rstd::Result<Schema, Error> {
    if (error_.kind != ErrorKind::None)
      return rstd::Err(Error(error_));
    return rstd::Ok(Schema(data_));
  }
};
constexpr auto Table::column(std::string_view name, ValueType type) const
    -> Column {
  return owner_ ? owner_->add_column(*this, name, type) : Column{};
}
template <class T>
constexpr auto Table::column(std::string_view name) const -> Column {
  if constexpr (requires { ValueTraits<T>::type; })
    return column(name, ValueTraits<T>::type);
  else {
    if (owner_)
      owner_->fail(ErrorKind::UnsupportedType, name);
    return {};
  }
}
constexpr auto Table::primary_key(std::string_view name,
                                  std::span<const Column> columns) const
    -> Key {
  return owner_ ? owner_->add_key(*this, name, columns, true) : Key{};
}
constexpr auto Table::unique_key(std::string_view name,
                                 std::span<const Column> columns) const -> Key {
  return owner_ ? owner_->add_key(*this, name, columns, false) : Key{};
}
inline void invalid_schema(Error) { __builtin_trap(); }
template <class Build>
constexpr auto try_define(Build build, SchemaLimits limits = {})
    -> rstd::Result<Schema, Error> {
  SchemaBuilder b(limits);
  build(b);
  return b.finish();
}
template <class Build>
consteval auto define(Build build, SchemaLimits limits = {}) -> Schema {
  auto result = try_define(build, limits);
  if (result.is_err())
    invalid_schema(result.unwrap_err());
  return result.unwrap();
}
} // namespace liorm::schema

namespace liorm::schema {
struct Range {
  unsigned tables{}, columns{}, keys{}, relations{}, members{}, text{};
  unsigned table_count{}, column_count{}, key_count{}, relation_count{},
      member_count{}, text_count{};
};
} // namespace liorm::schema
export namespace liorm::schema {
// Registrations share one bounded store; binding a table never copies a schema.
class Registry {
  Schema data_{};
  SchemaLimits limits_{};
  std::array<Range, 16> ranges_{};
  unsigned count_{}, capacity_{16};
  constexpr auto append(const Schema &source, Range r)
      -> rstd::Result<unsigned, Error> {
    if (!valid_limits(limits_) || capacity_ > 16 || count_ >= capacity_ ||
        r.table_count > limits_.tables - data_.tables_size_ ||
        r.column_count > limits_.columns - data_.columns_size_ ||
        r.key_count > limits_.keys - data_.keys_size_ ||
        r.relation_count > limits_.relations - data_.relations_size_ ||
        r.member_count > limits_.members - data_.members_size_ ||
        r.text_count > limits_.text - data_.text_size_)
      return rstd::Err(Error{.kind = ErrorKind::Capacity});
    Range dest{data_.tables_size_,    data_.columns_size_, data_.keys_size_,
               data_.relations_size_, data_.members_size_, data_.text_size_,
               r.table_count,         r.column_count,      r.key_count,
               r.relation_count,      r.member_count,      r.text_count};
    auto remap_text = [&](Text t) {
      return Text{dest.text + (t.offset - r.text), t.length};
    };
    for (unsigned i = 0; i < r.table_count; ++i) {
      auto v = source.tables_[r.tables + i];
      v.name = remap_text(v.name);
      data_.tables_[data_.tables_size_++] = v;
    }
    for (unsigned i = 0; i < r.column_count; ++i) {
      auto v = source.columns_[r.columns + i];
      v.name = remap_text(v.name);
      v.table = dest.tables + (v.table - r.tables);
      data_.columns_[data_.columns_size_++] = v;
    }
    for (unsigned i = 0; i < r.key_count; ++i) {
      auto v = source.keys_[r.keys + i];
      v.name = remap_text(v.name);
      v.table = dest.tables + (v.table - r.tables);
      v.offset = dest.members + (v.offset - r.members);
      data_.keys_[data_.keys_size_++] = v;
    }
    for (unsigned i = 0; i < r.relation_count; ++i) {
      auto v = source.relations_[r.relations + i];
      v.name = remap_text(v.name);
      v.table = dest.tables + (v.table - r.tables);
      v.target = dest.keys + (v.target - r.keys);
      v.offset = dest.members + (v.offset - r.members);
      data_.relations_[data_.relations_size_++] = v;
    }
    for (unsigned i = 0; i < r.member_count; ++i)
      data_.members_[data_.members_size_++] =
          dest.columns + (source.members_[r.members + i] - r.columns);
    for (unsigned i = 0; i < r.text_count; ++i)
      data_.text_[data_.text_size_++] = source.text_[r.text + i];
    ranges_[count_] = dest;
    return rstd::Ok(unsigned(count_++));
  }

public:
  constexpr explicit Registry(SchemaLimits limits = {}, unsigned capacity = 16)
      : limits_(limits), capacity_(capacity) {}
  constexpr auto use(const Schema &schema) -> rstd::Result<unsigned, Error> {
    return append(schema, Range{.table_count = schema.tables_size_,
                                .column_count = schema.columns_size_,
                                .key_count = schema.keys_size_,
                                .relation_count = schema.relations_size_,
                                .member_count = schema.members_size_,
                                .text_count = schema.text_size_});
  }
  constexpr auto import(const Registry &source, unsigned registration)
      -> rstd::Result<unsigned, Error> {
    return append(source.data_, source.ranges_.at(registration));
  }
  constexpr auto remap_table(const Registry &source, unsigned old,
                             unsigned from, unsigned to) const -> unsigned {
    return ranges_.at(to).tables + (old - source.ranges_.at(from).tables);
  }
  constexpr auto remap_column(const Registry &source, unsigned old,
                              unsigned from, unsigned to) const -> unsigned {
    return ranges_.at(to).columns + (old - source.ranges_.at(from).columns);
  }
  constexpr auto find_table(unsigned registration, std::string_view name) const
      -> std::optional<unsigned> {
    auto r = ranges_.at(registration);
    for (unsigned i = 0; i < r.table_count; ++i)
      if (same_name(data_.table(r.tables + i).name, name))
        return r.tables + i;
    return {};
  }
  constexpr auto find_foreign_key(unsigned registration,
                                  std::string_view name) const
      -> std::optional<unsigned> {
    auto r = ranges_.at(registration);
    for (unsigned i = 0; i < r.relation_count; ++i)
      if (same_name(data_.foreign_key(r.relations + i).name, name))
        return r.relations + i;
    return {};
  }
  constexpr auto table(unsigned i) const -> TableInfo { return data_.table(i); }
  constexpr auto column(unsigned i) const -> ColumnInfo {
    return data_.column(i);
  }
  constexpr auto key(unsigned i) const -> KeyInfo { return data_.key(i); }
  constexpr auto foreign_key(unsigned i) const -> ForeignKeyInfo {
    return data_.foreign_key(i);
  }
  constexpr auto find_column(unsigned table, std::string_view name) const
      -> std::optional<unsigned> {
    return data_.find_column(table, name);
  }
};
} // namespace liorm::schema
