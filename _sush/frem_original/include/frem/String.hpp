
#ifndef FREM_STRING_HPP
#define FREM_STRING_HPP

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace frem
{
// ----=====================================================================----
//     BoundedBasicString
// ----=====================================================================----

/// \brief A bounded string.
///
/// The BoundedString is a string array with a lower and an upper size bound.
/// The size bound must be valid at all times.
///
/// Note that if the lower bounds is different from zero, the string
/// will not be empty after construction but will contain \p TMinCount
/// NUL characters instead.
template <typename TChar, std::uint16_t TMinCount, std::uint16_t TMaxCount>
class BoundedBasicString
{
    static_assert(TMinCount <= TMaxCount, "Wrong size bounds");

public:
    using value_type = TChar;
    using pointer = TChar*;
    using const_pointer = const TChar*;
    using size_type = std::uint16_t;

    /// \brief Constructs a bounded string.
    ///
    /// Constructs a bounded string with \p min_size() NUL characters.
    constexpr BoundedBasicString() noexcept
        : m_size(TMinCount)
    {
        std::fill_n(m_data, m_size, TChar(0));
    }

    /// \brief Constructs a bounded string from a string view.
    ///
    /// Constructs a bounded string by copying the contents of the
    /// string view \p str. The size of \p str has to be within the bounds
    /// of this string.
    constexpr BoundedBasicString(std::basic_string_view<TChar> str) noexcept
        : m_size(str.size())
    {
        assert(m_size >= min_size() && m_size <= max_size());
        std::copy(str.begin(), str.end(), m_data);
    }

    BoundedBasicString(const BoundedBasicString&) = delete;
    BoundedBasicString& operator=(const BoundedBasicString&) = delete;

    /// \brief Assigns from a string view.
    ///
    /// Assigns the contents of the string view \p str to this string. The
    /// size of \p str has to be within the bounds of this string.
    BoundedBasicString& operator=(std::basic_string_view<TChar> str) noexcept
    {
        assert(str.size() >= min_size() && str.size() <= max_size());
        m_size = str.size();
        std::copy(str.begin(), str.end(), m_data);
        return *this;
    }

    /// \brief Whether the string is empty.
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    /// \brief The current size of the string.
    [[nodiscard]] constexpr size_type size() const noexcept { return m_size; }
    /// \brief The minimum string size.
    [[nodiscard]] constexpr size_type min_size() const noexcept { return TMinCount; }
    /// \brief The maximum string size.
    [[nodiscard]] constexpr size_type max_size() const noexcept { return TMaxCount; }

    /// \brief The maximum string size.
    [[nodiscard]] constexpr size_type capacity() const noexcept { return TMaxCount; }

    value_type& at(size_type index) noexcept
    {
        if (index >= m_size)
            throw std::out_of_range("BoundedBasicString<>::at() out of range");
        return m_data[index];
    }

    const value_type& at(size_type index) const noexcept
    {
        if (index >= m_size)
            throw std::out_of_range("BoundedBasicString<>::at() out of range");
        return m_data[index];
    }

    value_type& operator[](size_type index) noexcept { return m_data[index]; }
    const value_type& operator[](size_type index) const noexcept { return m_data[index]; }

    /// \brief A pointer to the data array.
    constexpr pointer data() noexcept { return m_data; }
    /// \brief A pointer to the data array.
    constexpr const_pointer data() const noexcept { return m_data; }

    /// \brief Changes the size of the string.
    ///
    /// If \p count is less than the current string size, the string is truncated.
    /// If \p count is greater than the current size, NUL characters are appended.
    void resize(size_type count) noexcept
    {
        assert(count >= min_size() && count <= max_size());
        if (count > m_size)
            std::fill_n(m_data + m_size, count - m_size, TChar(0));
        m_size = count;
    }

    /// \brief Adds a char at the end.
    ///
    /// Appends the character \p value at the end of this string.
    void push_back(const value_type& value)
    {
        assert(m_size < capacity());
        m_data[m_size] = value;
        ++m_size;
    }

    /// \brief Conversion to a string view.
    constexpr operator std::basic_string_view<TChar>() const noexcept
    {
        return std::basic_string_view<TChar>(m_data, m_size);
    }

private:
    size_type m_size;
    value_type m_data[TMaxCount];
};

template <std::uint16_t TMin, std::uint16_t TMax>
using BoundedAsciiString = BoundedBasicString<char, TMin, TMax>;

// ----=====================================================================----
//     FixedBasicString
// ----=====================================================================----

template <typename TChar, std::uint16_t TSize>
class FixedBasicString
{
    static_assert(TSize > 0);

public:
    using value_type = TChar;
    using pointer = TChar*;
    using const_pointer = const TChar*;
    using size_type = std::uint16_t;

    /// \brief Construct a fixed string.
    ///
    /// Constructs a fixed string consisting of a sequence of NUL characters.
    constexpr FixedBasicString() noexcept { std::fill_n(m_data, TSize, TChar(0)); }

    /// \brief Constructs a fixed string from a string view.
    ///
    /// Constructs a fixed string by copying the contents of the string
    /// view \p str. The size of \p str has to be the same as this \p size().
    constexpr FixedBasicString(std::basic_string_view<TChar> str) noexcept
    {
        assert(str.size() == capacity());
        std::copy(str.begin(), str.end(), m_data);
    }

    FixedBasicString(const FixedBasicString&) = delete;
    FixedBasicString& operator=(const FixedBasicString&) = delete;

    /// \brief Assigns from a string view.
    ///
    /// Assigns the contents of the string view \p str to this string. The
    /// size of \p str has to be equal to this \p size().
    FixedBasicString& operator=(std::basic_string_view<TChar> str) noexcept
    {
        assert(str.size() == capacity());
        std::copy(str.begin(), str.end(), m_data);
        return *this;
    }

    /// \brief The static size of this string.
    constexpr size_type size() const noexcept { return TSize; }

    /// \brief The static size of this string.
    constexpr size_type capacity() const noexcept { return TSize; }

    value_type& at(size_type index) noexcept
    {
        if (index >= TSize)
            throw std::out_of_range("FixedBasicString<>::at() out of range");
        return m_data[index];
    }

    const value_type& at(size_type index) const noexcept
    {
        if (index >= TSize)
            throw std::out_of_range("FixedBasicString<>::at() out of range");
        return m_data[index];
    }

    value_type& operator[](size_type index) noexcept { return m_data[index]; }

    const value_type& operator[](size_type index) const noexcept { return m_data[index]; }

    /// \brief A pointer to the data array.
    constexpr pointer data() noexcept { return m_data; }
    /// \brief A pointer to the data array.
    constexpr const_pointer data() const noexcept { return m_data; }

    /// \brief Conversion to a string view.
    constexpr operator std::basic_string_view<TChar>() const noexcept
    {
        return std::basic_string_view<TChar>(m_data, TSize);
    }

private:
    value_type m_data[TSize];
};

template <std::uint16_t TSize>
using FixedAsciiString = FixedBasicString<char, TSize>;

} // namespace frem

#endif // FREM_STRING_HPP
