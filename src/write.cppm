module;
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
export module liorm:write;
export import :bind;

namespace liorm {
template <class Policy, class Model> struct WriteCompiler;
}

export namespace liorm {
enum class WriteKind { Insert, Update, SelectExisting };
enum class WriteErrorKind {
  MissingPrimaryKey,
  NullPrimaryKey,
  DuplicateColumn,
  PrimaryKeyMismatch,
  UnsupportedType,
  Query,
  ShapeMismatch
};
struct WriteError {
  WriteErrorKind kind{};
  std::string_view field{};
  query::Error query_error{};
};

template <class Policy, class Model> class WriteShape;
template <class Policy, class Model> class WriteCommand {
  template <class P, class M> friend class WriteShape;
  query::CompiledQuery query_;
  ActiveModel<Model> model_;
  WriteKind kind_;

  WriteCommand(query::CompiledQuery query, ActiveModel<Model> model,
               WriteKind kind)
      : query_(std::move(query)), model_(std::move(model)), kind_(kind) {}

public:
  WriteCommand(const WriteCommand &) = delete;
  auto operator=(const WriteCommand &) -> WriteCommand & = delete;
  WriteCommand(WriteCommand &&) = default;
  auto operator=(WriteCommand &&) -> WriteCommand & = default;
  auto kind() const -> WriteKind { return kind_; }
  auto query() const & -> const query::CompiledQuery & { return query_; }
  auto query() const && -> const query::CompiledQuery & = delete;
  static constexpr auto projection() {
    return query::ProjectionLayout<query::ModelGroup<Model>>{}
        .template group<0>();
  }
  template <class Writer>
    requires std::is_same_v<std::remove_cvref_t<Writer>, Policy>
  auto bind(Writer &writer) const
      -> rstd::Result<rstd::empty, BindError<typename Writer::error_type>> {
    using Error = BindError<typename Writer::error_type>;
    using Definition = typename ActiveModel<Model>::definition;
    rstd::Option<Error> failure{rstd::None()};
    if (kind_ != WriteKind::Insert) {
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        auto inspect = [&]<std::size_t Index>() {
          using Field = typename Definition::template field<Index>;
          using Value = typename Field::value_type;
          if constexpr (Field::primary) {
            if (failure.is_some())
              return;
            unsigned parameter_index = 0;
            for (; parameter_index < query_.parameter_count();
                 ++parameter_index)
              if (query_.template parameter_is<Field::member>(parameter_index))
                break;
            const auto *value = model_.template field_at<Index>().value();
            if (!value) {
              failure = rstd::Some(Error{BindErrorKind::MissingValue,
                                         parameter_index, rstd::None()});
              return;
            }
            if constexpr (requires {
                            writer.template key_is_null<Value>(*value);
                          }) {
              auto null = writer.template key_is_null<Value>(*value);
              if (null.is_none())
                failure = rstd::Some(Error{BindErrorKind::Unsupported,
                                           parameter_index, rstd::None()});
              else if (*null)
                failure = rstd::Some(Error{BindErrorKind::NullPrimaryKey,
                                           parameter_index, rstd::None()});
            } else {
              failure = rstd::Some(Error{BindErrorKind::Unsupported,
                                         parameter_index, rstd::None()});
            }
          }
        };
        (inspect.template operator()<I>(), ...);
      }(std::make_index_sequence<Definition::field_count>{});
      if (failure.is_some())
        return rstd::Err(rstd::move(failure).unwrap());
    }
    if (query_.parameter_count() == 0)
      return rstd::Ok(rstd::empty{});
    return bind_values<Model>(query_, writer, ActiveValues<Model>{model_});
  }
};

template <class Policy, class Model> class WriteShape {
  template <class P, class M> friend struct WriteCompiler;
  using Definition = typename ActiveModel<Model>::definition;
  static constexpr std::size_t count = Definition::field_count;
  query::CompiledQuery query_;
  std::array<bool, count> selected_{};
  WriteKind kind_;

  WriteShape(query::CompiledQuery query, std::array<bool, count> selected,
             WriteKind kind)
      : query_(std::move(query)), selected_(selected), kind_(kind) {}

public:
  auto kind() const -> WriteKind { return kind_; }
  auto query() const & -> const query::CompiledQuery & { return query_; }
  auto query() const && -> const query::CompiledQuery & = delete;

  auto validate(const ActiveModel<Model> &model) const
      -> rstd::Result<rstd::empty, WriteError> {
    rstd::Option<WriteError> failure{rstd::None()};
    auto fail = [&](WriteErrorKind kind, std::string_view name) {
      if (failure.is_none())
        failure = rstd::Some(WriteError{kind, name, {}});
    };
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      auto inspect = [&]<std::size_t Index>() {
        using Field = typename Definition::template field<Index>;
        using Value = typename Field::value_type;
        const auto &slot = model.template field_at<Index>();
        if (kind_ == WriteKind::Insert) {
          if (slot.has_value() != selected_[Index])
            fail(WriteErrorKind::ShapeMismatch, Field::name.view());
        } else if constexpr (Field::primary) {
          if (!slot.has_value()) {
            fail(WriteErrorKind::MissingPrimaryKey, Field::name.view());
            return;
          }
          if constexpr (Policy::template supports<Value>()) {
            auto null = Policy::template key_is_null<Value>(*slot.value());
            if (null.is_none())
              fail(WriteErrorKind::UnsupportedType, Field::name.view());
            else if (*null)
              fail(WriteErrorKind::NullPrimaryKey, Field::name.view());
          } else {
            fail(WriteErrorKind::UnsupportedType, Field::name.view());
          }
        } else if (slot.is_set() != selected_[Index]) {
          fail(WriteErrorKind::ShapeMismatch, Field::name.view());
        }
      };
      (inspect.template operator()<I>(), ...);
    }(std::make_index_sequence<count>{});
    if (failure.is_some())
      return rstd::Err(rstd::move(failure).unwrap());
    return rstd::Ok(rstd::empty{});
  }

  auto instantiate(ActiveModel<Model> model) const
      -> rstd::Result<WriteCommand<Policy, Model>, WriteError> {
    auto checked = validate(model);
    if (checked.is_err())
      return rstd::Err(rstd::move(checked).unwrap_err());
    return rstd::Ok(
        WriteCommand<Policy, Model>(query_, std::move(model), kind_));
  }
};
} // namespace liorm

namespace liorm {
template <class Policy, class Model> struct WriteCompiler {
  using Definition = typename ActiveModel<Model>::definition;
  using Shape = WriteShape<Policy, Model>;
  using Result = rstd::Result<Shape, WriteError>;
  static constexpr std::size_t count = Definition::field_count;

  static auto selection(const ActiveModel<Model> &model, bool insert)
      -> std::array<bool, count> {
    std::array<bool, count> selected{};
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      ((selected[I] = insert ? model.template field_at<I>().has_value()
                             : model.template field_at<I>().is_set()),
       ...);
    }(std::make_index_sequence<count>{});
    return selected;
  }

  static auto shape(const schema::Schema &schema,
                    std::array<bool, count> selected, bool insert) -> Result {
    constexpr auto names = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::array<std::string_view, count>{
          Definition::template field<I>::name.view()...};
    }(std::make_index_sequence<count>{});
    for (std::size_t i = 0; i < count; ++i)
      for (std::size_t j = 0; j < i; ++j)
        if (same_name(names[i], names[j]))
          return rstd::Err(
              WriteError{WriteErrorKind::DuplicateColumn, names[i], {}});
    constexpr std::size_t model_key_count =
        []<std::size_t... I>(std::index_sequence<I...>) {
          return (std::size_t{0} + ... +
                  std::size_t(Definition::template field<I>::primary));
        }(std::make_index_sequence<count>{});
    if (auto table_id = schema.find_table(Definition::name.view())) {
      std::optional<schema::KeyInfo> primary;
      for (unsigned i = 0; i < schema.key_count(); ++i) {
        auto key = schema.key(i);
        if (key.table == *table_id && key.primary) {
          primary = key;
          break;
        }
      }
      bool key_matches = primary && primary->columns.size() == model_key_count;
      if (key_matches) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          auto inspect = [&]<std::size_t Index>() {
            using Field = typename Definition::template field<Index>;
            if constexpr (Field::primary) {
              bool found = false;
              for (auto column : primary->columns)
                found = found || same_name(schema.column(column).name,
                                           Field::name.view());
              key_matches = key_matches && found;
            }
          };
          (inspect.template operator()<I>(), ...);
        }(std::make_index_sequence<count>{});
      }
      if (!key_matches)
        return rstd::Err(
            WriteError{WriteErrorKind::PrimaryKeyMismatch, {}, {}});
    }
    if (!insert && model_key_count == 0)
      return rstd::Err(WriteError{WriteErrorKind::MissingPrimaryKey, {}, {}});
    rstd::Option<WriteError> failure{rstd::None()};
    unsigned changes = 0;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      auto inspect = [&]<std::size_t Index>() {
        using Field = typename Definition::template field<Index>;
        using Value = typename Field::value_type;
        const bool include =
            insert ? selected[Index] : (Field::primary || selected[Index]);
        if (!include)
          return;
        if constexpr (!Policy::template supports<Value>()) {
          if (failure.is_none())
            failure = rstd::Some(WriteError{
                WriteErrorKind::UnsupportedType, Field::name.view(), {}});
        }
        if constexpr (!Field::primary) {
          if (!insert && selected[Index])
            ++changes;
        }
      };
      (inspect.template operator()<I>(), ...);
    }(std::make_index_sequence<count>{});
    if (failure.is_some())
      return rstd::Err(rstd::move(failure).unwrap());

    query::QueryBuilder builder;
    auto table = builder.use(schema).template table<Model>();
    std::array<query::Expr, count> columns{};
    std::array<query::Expr, count> values{};
    std::array<query::Expr, count> assignments{};
    std::size_t selected_count = 0;
    std::size_t updates = 0;
    std::optional<query::Expr> condition;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      auto append = [&]<std::size_t Index>() {
        using Field = typename Definition::template field<Index>;
        using Value = typename Field::value_type;
        if constexpr (Policy::template supports<Value>()) {
          if (insert ? selected[Index] : (Field::primary || selected[Index])) {
            auto column = table.template col<Field::member>();
            auto param = builder.template member_param<Field::member>(
                Field::name.view(), Policy::template parameter_type<Value>());
            if (insert) {
              columns[selected_count] = column;
              values[selected_count++] = param;
            } else if constexpr (Field::primary) {
              auto key = column == param;
              if (condition)
                condition = *condition && key;
              else
                condition = key;
            } else {
              assignments[updates++] = column.set(param);
            }
          }
        }
      };
      (append.template operator()<I>(), ...);
    }(std::make_index_sequence<count>{});

    constexpr query::ProjectionLayout<query::ModelGroup<Model>> layout;
    auto projection = query::project(layout, table);
    query::Query statement;
    WriteKind kind;
    if (insert) {
      kind = WriteKind::Insert;
      if (selected_count == 0)
        statement =
            builder.insert_default_values(table.source()).returning(projection);
      else
        statement = builder
                        .insert_into(table.source(),
                                     std::span(columns).first(selected_count))
                        .values(std::span(values).first(selected_count))
                        .returning(projection);
    } else if (changes == 0) {
      kind = WriteKind::SelectExisting;
      statement =
          builder.select(projection).from(table.source()).where(*condition);
    } else {
      kind = WriteKind::Update;
      statement = builder.update(table.source())
                      .set(std::span(assignments).first(updates))
                      .where(*condition)
                      .returning(projection);
    }
    auto compiled = query::try_compile(statement);
    if (compiled.is_err())
      return rstd::Err(WriteError{
          WriteErrorKind::Query, {}, rstd::move(compiled).unwrap_err()});
    return rstd::Ok(Shape(rstd::move(compiled).unwrap(), selected, kind));
  }

  static auto build(const schema::Schema &schema, ActiveModel<Model> model,
                    bool insert)
      -> rstd::Result<WriteCommand<Policy, Model>, WriteError> {
    auto compiled = shape(schema, selection(model, insert), insert);
    if (compiled.is_err())
      return rstd::Err(rstd::move(compiled).unwrap_err());
    return rstd::move(compiled).unwrap().instantiate(std::move(model));
  }

  static auto shape_from(const schema::Schema &schema,
                         const ActiveModel<Model> &model, bool insert)
      -> Result {
    auto compiled = shape(schema, selection(model, insert), insert);
    if (compiled.is_err())
      return rstd::Err(rstd::move(compiled).unwrap_err());
    auto checked = compiled->validate(model);
    if (checked.is_err())
      return rstd::Err(rstd::move(checked).unwrap_err());
    return compiled;
  }
};
} // namespace liorm

export namespace liorm {
template <class Policy, class Model>
auto try_insert(const schema::Schema &schema, ActiveModel<Model> model)
    -> rstd::Result<WriteCommand<Policy, Model>, WriteError> {
  return WriteCompiler<Policy, Model>::build(schema, std::move(model), true);
}
template <class Policy, class Model>
auto try_update(const schema::Schema &schema, ActiveModel<Model> model)
    -> rstd::Result<WriteCommand<Policy, Model>, WriteError> {
  return WriteCompiler<Policy, Model>::build(schema, std::move(model), false);
}
template <class Policy, class Model>
auto try_insert_shape(const schema::Schema &schema,
                      const ActiveModel<Model> &model)
    -> rstd::Result<WriteShape<Policy, Model>, WriteError> {
  return WriteCompiler<Policy, Model>::shape_from(schema, model, true);
}
template <class Policy, class Model>
auto try_update_shape(const schema::Schema &schema,
                      const ActiveModel<Model> &model)
    -> rstd::Result<WriteShape<Policy, Model>, WriteError> {
  return WriteCompiler<Policy, Model>::shape_from(schema, model, false);
}
template <class Policy, class Model>
auto try_full_insert_shape(const schema::Schema &schema)
    -> rstd::Result<WriteShape<Policy, Model>, WriteError> {
  std::array<bool, ActiveModel<Model>::field_count> selected{};
  selected.fill(true);
  return WriteCompiler<Policy, Model>::shape(schema, selected, true);
}
template <class Policy, class Model>
auto try_full_update_shape(const schema::Schema &schema)
    -> rstd::Result<WriteShape<Policy, Model>, WriteError> {
  std::array<bool, ActiveModel<Model>::field_count> selected{};
  selected.fill(true);
  return WriteCompiler<Policy, Model>::shape(schema, selected, false);
}
} // namespace liorm
