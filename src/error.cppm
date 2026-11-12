export module liorm:error;
export import :query;
export import :schema;
export import :write;
import rstd;
using namespace rstd::prelude;

export namespace rstd
{
template<>
struct Impl<fmt::Display, liorm::query::Error> : ImplBase<liorm::query::Error> {
    auto fmt(fmt::Formatter& f) const -> bool {
        const auto& error = this->self();
        return f.write_fmt(
            fmt::Arguments::make("query error {}: {}", static_cast<int>(error.kind), error.name()));
    }
};
template<>
struct Impl<fmt::Debug, liorm::query::Error> : Impl<fmt::Display, liorm::query::Error> {};
template<>
struct Impl<error::Error, liorm::query::Error> : DefaultInImpl<error::Error, liorm::query::Error> {
};
template<>
struct Impl<fmt::Display, liorm::schema::Error> : ImplBase<liorm::schema::Error> {
    auto fmt(fmt::Formatter& f) const -> bool {
        const auto& error = this->self();
        return f.write_fmt(fmt::Arguments::make(
            "schema error {}: {}", static_cast<int>(error.kind), error.name()));
    }
};
template<>
struct Impl<fmt::Debug, liorm::schema::Error> : Impl<fmt::Display, liorm::schema::Error> {};
template<>
struct Impl<error::Error, liorm::schema::Error>
    : DefaultInImpl<error::Error, liorm::schema::Error> {};
template<class E>
struct Impl<fmt::Display, liorm::BindError<E>> : ImplBase<liorm::BindError<E>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        const auto& error = this->self();
        if (! f.write_fmt(fmt::Arguments::make("binding error {} at parameter {}",
                                               static_cast<int>(error.kind),
                                               error.parameter_index)))
            return false;
        if constexpr (Impled<E, fmt::Display>)
            if (error.writer_error.is_some())
                return f.write_fmt(fmt::Arguments::make(": {}", *error.writer_error));
        return true;
    }
};
template<class E>
struct Impl<fmt::Debug, liorm::BindError<E>> : Impl<fmt::Display, liorm::BindError<E>> {};
template<class E>
struct Impl<error::Error, liorm::BindError<E>> : DefaultInImpl<error::Error, liorm::BindError<E>> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        if constexpr (Impled<E, error::Error>) {
            const auto& error = this->self();
            if (error.writer_error.is_some())
                return Some(dyn<error::Error>::from_ref(*error.writer_error));
        }
        return None();
    }
};
template<>
struct Impl<fmt::Display, liorm::WriteError> : ImplBase<liorm::WriteError> {
    auto fmt(fmt::Formatter& f) const -> bool {
        const auto& error = this->self();
        if (error.kind == liorm::WriteErrorKind::Query)
            return f.write_fmt(fmt::Arguments::make("write error: {}", error.query_error));
        return f.write_fmt(
            fmt::Arguments::make("write error {}: {}", static_cast<int>(error.kind), error.field));
    }
};
template<>
struct Impl<fmt::Debug, liorm::WriteError> : Impl<fmt::Display, liorm::WriteError> {};
template<>
struct Impl<error::Error, liorm::WriteError> : DefaultInImpl<error::Error, liorm::WriteError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.kind == liorm::WriteErrorKind::Query)
            return Some(dyn<error::Error>::from_ref(error.query_error));
        return None();
    }
};
} // namespace rstd
