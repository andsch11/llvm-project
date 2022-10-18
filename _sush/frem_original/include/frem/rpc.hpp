
#ifndef FREM_RPC_HPP
#define FREM_RPC_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>


/// An annotation for remote procedure calls.
#define FREM_RPC(...)

/// Defines an alias for a type.
#define FREM_TYPE_ALIAS(type, aliasName)


namespace frem
{
// ----=====================================================================----
//     StringLiteral
// ----=====================================================================----

template <std::size_t N>
class StringLiteral
{
public:
    template <std::size_t... TIndices>
    constexpr StringLiteral(const char (&str)[N + 1], std::index_sequence<TIndices...>) noexcept
        : data{str[TIndices]..., '\0'}
    {
    }

    template <std::size_t... TIndices>
    constexpr StringLiteral(const std::array<char, N + 1>& str,
                            std::index_sequence<TIndices...>) noexcept
        : data{str[TIndices]..., '\0'}
    {
    }

    template <std::size_t M>
    constexpr StringLiteral<N + M> append(const StringLiteral<M>& str) const noexcept
    {
        return StringLiteral<N + M>(doAppend(str,
                                             std::make_index_sequence<N>(),
                                             std::make_index_sequence<M>()),
                                    std::make_index_sequence<N + M>());
    }

private:
    char data[N + 1];

    template <std::size_t M, std::size_t... TThis, std::size_t... TThat>
    constexpr std::array<char, N + M + 1> doAppend(const StringLiteral<M>& str,
                                                   std::index_sequence<TThis...>,
                                                   std::index_sequence<TThat...>) const noexcept
    {
        return std::array<char, N + M + 1>{data[TThis]..., str.data[TThat]..., '\0'};
    }

    template <std::size_t M>
    friend class StringLiteral;
};

template <std::size_t N>
constexpr StringLiteral<N - 1> toStringLiteral(const char (&str)[N]) noexcept
{
    return StringLiteral<N - 1>(str, std::make_index_sequence<N - 1>());
}

template <std::size_t M, std::size_t N>
constexpr StringLiteral<M + N> operator+(const StringLiteral<M>& a,
                                         const StringLiteral<N>& b) noexcept
{
    return a.append(b);
}

template <std::size_t M, std::size_t N>
constexpr StringLiteral<M + N - 1> operator+(const StringLiteral<M>& a, const char (&b)[N]) noexcept
{
    return a.append(toStringLiteral(b));
}

template <std::size_t M, std::size_t N>
constexpr StringLiteral<M + N - 1> operator+(const char (&a)[M], const StringLiteral<N>& b) noexcept
{
    return toStringLiteral(a).append(b);
}

// ----=====================================================================----
//     Annotations
// ----=====================================================================----

/// An alias name for the RPC.
struct Alias
{
    constexpr Alias(const char*) {}

    template <std::size_t N>
    constexpr Alias(const StringLiteral<N>&)
    {
    }
};

/// The RPC code.
struct Code
{
    constexpr Code(unsigned) {}
};

/// Marks an RPC as registerable in an RPC registry.
struct Registerable
{
    constexpr Registerable(bool = true) {}
};

/// Specifies the name of the return variable.
struct ReturnName
{
    template <typename... TTags>
    constexpr ReturnName(const char*, TTags...)
    {
    }

    template <std::size_t N, typename... TTags>
    constexpr ReturnName(const StringLiteral<N>&, TTags...)
    {
    }
};

/// Additional tags for an RPC.
struct Tags
{
    template <typename... TTags>
    constexpr Tags(const char*, TTags...)
    {
    }

    template <std::size_t N, typename... TTags>
    constexpr Tags(const StringLiteral<N>&, TTags...)
    {
    }
};

/// Specifies the transport channel.
struct Via
{
    constexpr Via(const char*) {}

    template <std::size_t N>
    constexpr Via(const StringLiteral<N>&)
    {
    }
};

// ----=====================================================================----
//     TypeAlias
// ----=====================================================================----

template <typename T>
struct TypeAlias
{
    constexpr TypeAlias(const char*) {}

    template <std::size_t N>
    constexpr TypeAlias(const StringLiteral<N>&)
    {
    }
};

// ----=====================================================================----
//     RpcService
// ----=====================================================================----

/// A remote procedure call service.
template <typename T>
class RpcService
{
public:
    RpcService() { m_fremSelf = static_cast<T*>(this); }

    RpcService(const RpcService&) = delete;
    RpcService& operator=(const RpcService&) = delete;

    ~RpcService() { m_fremSelf = nullptr; }

    static std::atomic<T*> m_fremSelf;
};

template <typename T>
std::atomic<T*> RpcService<T>::m_fremSelf{nullptr};

// ----=====================================================================----
//     RpcResultDecl
// ----=====================================================================----

class RpcResult;

//! Declares the result of an RPC invocation.
//! This class is needed to make all possible values of \p RpcResult known
//! to frem-gen.
class RpcResultDecl
{
public:
    constexpr RpcResultDecl(std::int16_t value) noexcept
        : m_value(value)
    {
    }

private:
    std::int16_t m_value;

    friend class RpcResult;
};

// ----=====================================================================----
//     RpcResult
// ----=====================================================================----

//! RpcResult is the result of an RPC invocation.
class RpcResult
{
public:
    //! Creates an RPC result from the declarator \p decl.
    constexpr RpcResult(RpcResultDecl decl) noexcept
        : m_value(decl.m_value)
    {
    }

    constexpr explicit RpcResult(std::int16_t value) noexcept
        : m_value(value)
    {
    }

    RpcResult(const RpcResult&) = default;
    RpcResult& operator=(const RpcResult&) = default;

    //! Returns \p true, if the results \p r1 and \p r2 are equal.
    friend bool operator==(RpcResult r1, RpcResult r2) noexcept { return r1.m_value == r2.m_value; }

    //! Returns \p true, if the results \p r1 and \p r2 are not equal.
    friend bool operator!=(RpcResult r1, RpcResult r2) noexcept { return r1.m_value != r2.m_value; }

    //! Returns \p true, if the result is success (0).
    explicit operator bool() const noexcept { return m_value == 0; }

    //! Returns the value of the RPC result.
    operator std::int16_t() const noexcept { return m_value; }


    static constexpr RpcResultDecl Success{0};

    static constexpr RpcResultDecl ServiceNotAvailable{-256};

    static constexpr RpcResultDecl NoSuchCommand{-257};

private:
    std::int16_t m_value;
};

// ----=====================================================================----
//     Array
// ----=====================================================================----

//! A policy for fixed-size arrays.
template <std::size_t TSize>
struct Fixed
{
    static constexpr std::size_t size = TSize;
    using size_type = std::size_t;
};

//! A policy for arrays with bounded size.
template <std::size_t TMinSize, std::size_t TMaxSize, typename TSizeType = std::uint16_t>
struct Bounded
{
    static constexpr std::size_t min_size = TMinSize;
    static constexpr std::size_t max_size = TMaxSize;
    using size_type = TSizeType;
};


namespace frem_detail
{
template <typename T>
struct is_size_policy : std::false_type
{
};

template <std::size_t TSize>
struct is_size_policy<Fixed<TSize>> : std::true_type
{
};

template <std::size_t TMinSize, std::size_t TMaxSize>
struct is_size_policy<Bounded<TMinSize, TMaxSize>> : std::true_type
{
};


template <typename T>
struct is_fixed_policy : std::false_type
{
};

template <std::size_t TSize>
struct is_fixed_policy<Fixed<TSize>> : std::true_type
{
};


template <typename T, typename TPolicy>
struct FixedArrayStorage
{
    using size_type = typename TPolicy::size_type;

    T* data() const noexcept { return m_data; }

    constexpr size_type capacity() const noexcept { return TPolicy::size; }

    T m_data[TPolicy::size];
};

template <typename T, typename TPolicy>
struct BoundedArrayStorage
{
    using size_type = typename TPolicy::size_type;

    T* data() const noexcept { return const_cast<T*>(m_data); }

    constexpr size_type capacity() const noexcept { return TPolicy::max_size; }

    T m_data[TPolicy::max_size];
};

template <typename T, typename TPolicy>
using ArrayStorage = typename std::conditional<is_fixed_policy<TPolicy>::value,
                                               FixedArrayStorage<T, TPolicy>,
                                               BoundedArrayStorage<T, TPolicy>>::type;

} // namespace frem_detail


template <typename TType, typename TSizePolicy>
class Array
{
    static_assert(frem_detail::is_size_policy<TSizePolicy>::value, "Expected a size policy.");

    using storage_type = frem_detail::ArrayStorage<TType, TSizePolicy>;

public:
    using value_type = TType;
    using size_type = typename storage_type::size_type;

    constexpr Array()
        : m_size(0)
    {
    }

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    constexpr size_type size() const noexcept { return m_size; }

    constexpr size_type capacity() const noexcept { return m_storage.maxSize(); }

    value_type& at(size_type index) noexcept
    {
        // TODO: assert that there is enough space left
        //if (m_size >= capacity())
        //    throw something;
        return m_storage.data()[index];
    }

    const value_type& at(size_type index) const noexcept
    {
        // TODO: assert that there is enough space left
        //if (m_size >= capacity())
        //    throw something;
        return m_storage.data()[index];
    }

    void push_back(const value_type& value)
    {
        // TODO: assert that there is enough space left
        //if (m_size >= capacity())
        //    throw something;
        m_storage.data()[m_size] = value;
        ++m_size;
    }

    value_type& operator[](size_type index) noexcept { return m_storage.data()[index]; }

    const value_type& operator[](size_type index) const noexcept { return m_storage.data()[index]; }

private:
    storage_type m_storage;
    size_type m_size;
};

} // namespace frem

#endif // FREM_RPC_HPP
