module;
export module liorm:schema;
export import :value;
import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace liorm::schema {
constexpr auto schema_text(rstd::byte const *data, usize length) -> ref<str> {
  return ref<str>::from_raw_parts_unchecked(data, length);
}
template <class T>
constexpr auto span_of(initializer_list<T> values) -> slice<T> {
  return slice<T>::from_raw_parts(values.begin(), usize(values.size()));
}
} // namespace liorm::schema

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
  array<rstd::byte, 96> context{};
  usize length{};
  bool truncated{};
  constexpr auto name() const -> ref<str> {
    return schema_text(context.data(), length);
  }
};
struct SchemaLimits {
  usize tables {64}, columns {512}, keys {128}, relations {128};
  usize members {1024}, text {8192};
};
constexpr auto valid_limits(SchemaLimits l) -> bool {
  return l.tables <= 64_usize && l.columns <= 512_usize && l.keys <= 128_usize &&
         l.relations <= 128_usize && l.members <= 1024_usize &&
         l.text <= 8192_usize;
}
struct TableInfo {
  ref<str> name;
};
struct ColumnInfo {
  ref<str> name;
  usize table{};
  ValueType type{};
};
struct KeyInfo {
  ref<str> name;
  usize table{};
  slice<usize> columns;
  bool primary{};
};
struct ForeignKeyInfo {
  ref<str> name;
  usize table{}, target{};
  slice<usize> columns;
};
class SchemaBuilder;
class Registry;
} // namespace liorm::schema
namespace liorm::schema {
struct Text {
  usize offset{}, length{};
};
struct TableData {
  Text name;
};
struct ColumnData {
  Text name;
  usize table{};
  ValueType type{};
};
struct KeyData {
  Text name;
  usize table{}, offset{}, count{};
  bool primary{};
};
struct ForeignKeyData {
  Text name;
  usize table{}, target{}, offset{}, count{};
};
} // namespace liorm::schema
export namespace liorm::schema {
class Schema {
  friend class SchemaBuilder;
  friend class Registry;
  array<TableData, 64> tables_{};
  array<ColumnData, 512> columns_{};
  array<KeyData, 128> keys_{};
  array<ForeignKeyData, 128> relations_{};
  array<usize, 1024> members_{};
  array<rstd::byte, 8192> text_{};
  usize tables_size_{}, columns_size_{}, keys_size_{}, relations_size_{},
      members_size_{}, text_size_{};
  constexpr auto text(Text t) const -> ref<str> {
    return schema_text(text_.data() + t.offset.to_primitive(), t.length);
  }

public:
  constexpr auto table_count() const -> usize { return tables_size_; }
  constexpr auto column_count() const -> usize { return columns_size_; }
  constexpr auto key_count() const -> usize { return keys_size_; }
  constexpr auto foreign_key_count() const -> usize {
    return relations_size_;
  }
  constexpr auto table(usize i) const -> TableInfo {
    return {text(tables_.at(i).name)};
  }
  constexpr auto column(usize i) const -> ColumnInfo {
    const auto &c = columns_.at(i);
    return {text(c.name), c.table, c.type};
  }
  constexpr auto key(usize i) const -> KeyInfo {
    const auto &k = keys_.at(i);
    return {text(k.name),
            k.table,
            slice<usize>::from_raw_parts(members_.data() + k.offset.to_primitive(), k.count),
            k.primary};
  }
  constexpr auto foreign_key(usize i) const -> ForeignKeyInfo {
    const auto &f = relations_.at(i);
    return {
        text(f.name), f.table, f.target, slice<usize>::from_raw_parts(members_.data() + f.offset.to_primitive(), f.count)};
  }
  constexpr auto find_table(ref<str> name) const
      -> Option<usize> {
    for (usize i {}; i < tables_size_; ++i)
      if (same_name(table(i).name, name))
        return Some(i);
    return None();
  }
  constexpr auto find_column(usize table, ref<str> name) const
      -> Option<usize> {
    for (usize i {}; i < columns_size_; ++i) {
      auto c = column(i);
      if (c.table == table && same_name(c.name, name))
        return Some(i);
    }
    return {};
  }
};
class Column {
  friend class SchemaBuilder;
  SchemaBuilder *owner_{};
  usize id_{};
  constexpr Column(SchemaBuilder *b, usize id) : owner_(b), id_(id) {}

public:
  constexpr Column() = default;
};
class Key {
  friend class SchemaBuilder;
  SchemaBuilder *owner_{};
  usize id_{};
  constexpr Key(SchemaBuilder *b, usize id) : owner_(b), id_(id) {}

public:
  constexpr Key() = default;
};
class Table {
  friend class SchemaBuilder;
  SchemaBuilder *owner_{};
  usize id_{};
  constexpr Table(SchemaBuilder *b, usize id) : owner_(b), id_(id) {}

public:
  constexpr Table() = default;
  constexpr auto column(ref<str> name, ValueType type) const -> Column;
  template <class T>
  constexpr auto column(ref<str> name) const -> Column;
  constexpr auto primary_key(ref<str> name,
                             slice<Column> columns) const -> Key;
  constexpr auto unique_key(ref<str> name,
                            slice<Column> columns) const -> Key;
  constexpr auto primary_key(ref<str> name,
                             initializer_list<Column> columns) const
      -> Key {
    return primary_key(name, span_of(columns));
  }
  constexpr auto unique_key(ref<str> name,
                            initializer_list<Column> columns) const
      -> Key {
    return unique_key(name, span_of(columns));
  }
};
class SchemaBuilder {
  friend class Table;
  Schema data_{};
  SchemaLimits limits_{};
  Error error_{};
  constexpr void fail(ErrorKind kind, ref<str> name = {}) {
    if (error_.kind != ErrorKind::None)
      return;
    error_.kind = kind;
    error_.length = name.len() < 96_usize ? name.len() : 96_usize;
    error_.truncated = name.len() > 96_usize;
    for (usize i {}; i < error_.length; ++i)
      error_.context[i] = rstd::byte {name[i].to_primitive()};
  }
  constexpr auto store(ref<str> name) -> Text {
    if (!valid_name(name)) {
      fail(ErrorKind::InvalidName, name);
      return {};
    }
    if (name.len() > limits_.text - data_.text_size_) {
      fail(ErrorKind::Capacity, name);
      return {};
    }
    array<rstd::byte, 8192> bytes{};
    for (usize i {}; i < name.len(); ++i)
      bytes[i] = rstd::byte {static_cast<unsigned char>(name[i].to_primitive())};
    if (rstd::str_::from_utf8(rstd::slice<rstd::u8>::from_raw_parts(
                                  bytes.data(), rstd::usize(name.len())))
            .is_err()) {
      fail(ErrorKind::InvalidName, name);
      return {};
    }
    Text t{data_.text_size_, usize(name.len())};
    for (usize i {}; i < name.len(); ++i)
      data_.text_[data_.text_size_++] = rstd::byte {name[i].to_primitive()};
    return t;
  }
  template <class T>
  constexpr auto check(T h, usize count, ref<str> name) -> bool {
    if (! h.owner_ || h.id_ == 0_usize) {
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
  constexpr auto add_column(Table t, ref<str> name, ValueType type)
      -> Column {
    if (!check(t, data_.tables_size_, name))
      return {};
    if (!valid_type(type)) {
      fail(ErrorKind::UnsupportedType, name);
      return {};
    }
    if (data_.find_column(t.id_ - 1_usize, name)) {
      fail(ErrorKind::DuplicateName, name);
      return {};
    }
    if (data_.columns_size_ >= limits_.columns) {
      fail(ErrorKind::Capacity, name);
      return {};
    }
    auto text = store(name);
    usize id = data_.columns_size_++;
    data_.columns_[id] = {text, t.id_ - 1_usize, type};
    return Column(this, id + 1_usize);
  }
  constexpr auto members(slice<Column> columns, usize table,
                         ref<str> name) -> bool {
    if (columns.is_empty()) {
      fail(ErrorKind::EmptyKey, name);
      return false;
    }
    if (columns.len() > limits_.members - data_.members_size_) {
      fail(ErrorKind::Capacity, name);
      return false;
    }
    for (usize i {}; i < columns.len(); ++i) {
      if (!check(columns[i], data_.columns_size_, name))
        return false;
      if (data_.column(columns[i].id_ - 1_usize).table != table) {
        fail(ErrorKind::CrossTable, name);
        return false;
      }
      for (usize j {}; j < i; ++j)
        if (columns[j].id_ == columns[i].id_) {
          fail(ErrorKind::DuplicateColumn, name);
          return false;
        }
    }
    return true;
  }
  constexpr auto add_key(Table t, ref<str> name,
                         slice<Column> columns, bool primary) -> Key {
    if (!check(t, data_.tables_size_, name) ||
        !members(columns, t.id_ - 1_usize, name))
      return {};
    for (usize i {}; i < data_.keys_size_; ++i) {
      auto k = data_.key(i);
      if (k.table != t.id_ - 1_usize)
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
        if (data_.column(c.id_ - 1_usize).type.nullable) {
          fail(ErrorKind::NullablePrimaryKey, name);
          return {};
        }
    if (data_.keys_size_ >= limits_.keys) {
      fail(ErrorKind::Capacity, name);
      return {};
    }
    auto text = store(name);
    usize id = data_.keys_size_++;
    data_.keys_[id] = {text, t.id_ - 1_usize, data_.members_size_,
                       usize(columns.len()), primary};
    for (auto c : columns)
      data_.members_[data_.members_size_++] = c.id_ - 1_usize;
    return Key(this, id + 1_usize);
  }

public:
  constexpr explicit SchemaBuilder(SchemaLimits limits = {}) : limits_(limits) {
    if (!valid_limits(limits)) {
      fail(ErrorKind::Capacity, "limits"_str);
      limits_ = {};
    }
  }
  SchemaBuilder(const SchemaBuilder &) = delete;
  SchemaBuilder(SchemaBuilder &&) = delete;
  constexpr auto table(ref<str> name) -> Table {
    if (name.contains("."_str)) {
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
    usize id = data_.tables_size_++;
    data_.tables_[id] = {text};
    return Table(this, id + 1_usize);
  }
  constexpr void foreign_key(ref<str> name,
                             slice<Column> columns, Key target) {
    if (!check(target, data_.keys_size_, name))
      return;
    if (columns.is_empty()) {
      fail(ErrorKind::EmptyKey, name);
      return;
    }
    if (!check(columns[0_usize], data_.columns_size_, name))
      return;
    usize table = data_.column(columns[0_usize].id_ - 1_usize).table;
    if (!members(columns, table, name))
      return;
    for (usize i {}; i < data_.relations_size_; ++i)
      if (same_name(data_.foreign_key(i).name, name)) {
        fail(ErrorKind::DuplicateName, name);
        return;
      }
    auto key = data_.key(target.id_ - 1_usize);
    if (columns.len() != key.columns.len()) {
      fail(ErrorKind::ColumnCount, name);
      return;
    }
    for (usize i {}; i < columns.len(); ++i) {
      auto a = data_.column(columns[i].id_ - 1_usize).type,
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
    data_.relations_[data_.relations_size_++] = {text, table, target.id_ - 1_usize,
                                                 data_.members_size_,
                                                 usize(columns.len())};
    for (auto c : columns)
      data_.members_[data_.members_size_++] = c.id_ - 1_usize;
  }
  constexpr void foreign_key(ref<str> name,
                             initializer_list<Column> columns,
                             Key target) {
    foreign_key(name, span_of(columns), target);
  }
  constexpr auto finish() const -> rstd::Result<Schema, Error> {
    if (error_.kind != ErrorKind::None)
      return rstd::Err(Error(error_));
    return rstd::Ok(Schema(data_));
  }
};
constexpr auto Table::column(ref<str> name, ValueType type) const
    -> Column {
  return owner_ ? owner_->add_column(*this, name, type) : Column{};
}
template <class T>
constexpr auto Table::column(ref<str> name) const -> Column {
  if constexpr (requires { ValueTraits<T>::type; })
    return column(name, ValueTraits<T>::type);
  else {
    if (owner_)
      owner_->fail(ErrorKind::UnsupportedType, name);
    return {};
  }
}
constexpr auto Table::primary_key(ref<str> name,
                                  slice<Column> columns) const
    -> Key {
  return owner_ ? owner_->add_key(*this, name, columns, true) : Key{};
}
constexpr auto Table::unique_key(ref<str> name,
                                 slice<Column> columns) const -> Key {
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
  usize tables{}, columns{}, keys{}, relations{}, members{}, text{};
  usize table_count{}, column_count{}, key_count{}, relation_count{},
      member_count{}, text_count{};
};
} // namespace liorm::schema
export namespace liorm::schema {
// Registrations share one bounded store; binding a table never copies a schema.
class Registry {
  Schema data_{};
  SchemaLimits limits_{};
  array<Range, 16> ranges_{};
  usize count_{}, capacity_ {16};
  constexpr auto append(const Schema &source, Range r)
      -> rstd::Result<usize, Error> {
    if (!valid_limits(limits_) || capacity_ > 16_usize || count_ >= capacity_ ||
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
    for (usize i {}; i < r.table_count; ++i) {
      auto v = source.tables_[r.tables + i];
      v.name = remap_text(v.name);
      data_.tables_[data_.tables_size_++] = v;
    }
    for (usize i {}; i < r.column_count; ++i) {
      auto v = source.columns_[r.columns + i];
      v.name = remap_text(v.name);
      v.table = dest.tables + (v.table - r.tables);
      data_.columns_[data_.columns_size_++] = v;
    }
    for (usize i {}; i < r.key_count; ++i) {
      auto v = source.keys_[r.keys + i];
      v.name = remap_text(v.name);
      v.table = dest.tables + (v.table - r.tables);
      v.offset = dest.members + (v.offset - r.members);
      data_.keys_[data_.keys_size_++] = v;
    }
    for (usize i {}; i < r.relation_count; ++i) {
      auto v = source.relations_[r.relations + i];
      v.name = remap_text(v.name);
      v.table = dest.tables + (v.table - r.tables);
      v.target = dest.keys + (v.target - r.keys);
      v.offset = dest.members + (v.offset - r.members);
      data_.relations_[data_.relations_size_++] = v;
    }
    for (usize i {}; i < r.member_count; ++i)
      data_.members_[data_.members_size_++] =
          dest.columns + (source.members_[r.members + i] - r.columns);
    for (usize i {}; i < r.text_count; ++i)
      data_.text_[data_.text_size_++] = source.text_[r.text + i];
    ranges_[count_] = dest;
    return rstd::Ok(usize(count_++));
  }

public:
  constexpr explicit Registry(SchemaLimits limits = {}, usize capacity = 16_usize)
      : limits_(limits), capacity_(capacity) {}
  constexpr auto use(const Schema &schema) -> rstd::Result<usize, Error> {
    return append(schema, Range{.table_count = schema.tables_size_,
                                .column_count = schema.columns_size_,
                                .key_count = schema.keys_size_,
                                .relation_count = schema.relations_size_,
                                .member_count = schema.members_size_,
                                .text_count = schema.text_size_});
  }
  constexpr auto import(const Registry &source, usize registration)
      -> rstd::Result<usize, Error> {
    return append(source.data_, source.ranges_.at(registration));
  }
  constexpr auto remap_table(const Registry &source, usize old,
                             usize from, usize to) const -> usize {
    return ranges_.at(to).tables + (old - source.ranges_.at(from).tables);
  }
  constexpr auto remap_column(const Registry &source, usize old,
                              usize from, usize to) const -> usize {
    return ranges_.at(to).columns + (old - source.ranges_.at(from).columns);
  }
  constexpr auto find_table(usize registration, ref<str> name) const
      -> Option<usize> {
    auto r = ranges_.at(registration);
    for (usize i {}; i < r.table_count; ++i)
      if (same_name(data_.table(r.tables + i).name, name))
        return Some(r.tables + i);
    return None();
  }
  constexpr auto find_foreign_key(usize registration,
                                  ref<str> name) const
      -> Option<usize> {
    auto r = ranges_.at(registration);
    for (usize i {}; i < r.relation_count; ++i)
      if (same_name(data_.foreign_key(r.relations + i).name, name))
        return Some(r.relations + i);
    return None();
  }
  constexpr auto table(usize i) const -> TableInfo { return data_.table(i); }
  constexpr auto column(usize i) const -> ColumnInfo {
    return data_.column(i);
  }
  constexpr auto key(usize i) const -> KeyInfo { return data_.key(i); }
  constexpr auto foreign_key(usize i) const -> ForeignKeyInfo {
    return data_.foreign_key(i);
  }
  constexpr auto find_column(usize table, ref<str> name) const
      -> Option<usize> {
    return data_.find_column(table, name);
  }
};
} // namespace liorm::schema
