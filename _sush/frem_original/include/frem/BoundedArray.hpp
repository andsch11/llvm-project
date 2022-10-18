
#ifndef FREM_BOUNDEDARRAY_HPP
#define FREM_BOUNDEDARRAY_HPP

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace frem
{
// ----=====================================================================----
//     BoundedArray
// ----=====================================================================----

/// \brief A bounded array.
///
/// The BoundedArray is an array with an upper and a lower size bound.
/// At all times the size is constrained to be within those limits.
///
/// Note that if the lower bounds is different from zero, the array
/// will not be empty after construction but will contain \p TMinCount
/// default-constructed elements instead.
template <typename TType, std::uint16_t TMinCount, std::uint16_t TMaxCount>
class BoundedArray
{
    static_assert(TMinCount <= TMaxCount, "Wrong size bounds");
    static_assert(std::is_trivially_destructible_v<TType>,
                  "Invoking the destructor is not implemented");

public:
    using value_type = TType;
    using pointer = TType*;
    using const_pointer = const TType*;
    using size_type = std::uint16_t;

    /// \brief Constructs a bounded array.
    ///
    /// Constructs a bounded array with \p min_size() default-constructed
    /// elements.
    constexpr BoundedArray() noexcept
        : m_size(TMinCount)
    {
        std::fill_n(m_data, m_size, TType());
    }

    BoundedArray(const BoundedArray&) = delete;
    BoundedArray& operator=(const BoundedArray&) = delete;

    /// \brief Whether the array is empty.
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    /// \brief The current size of the array.
    [[nodiscard]] constexpr size_type size() const noexcept { return m_size; }
    /// \brief The minimum size of the array.
    [[nodiscard]] constexpr size_type min_size() const noexcept { return TMinCount; }
    /// \brief The maximum size of the array.
    [[nodiscard]] constexpr size_type max_size() const noexcept { return TMaxCount; }

    /// \brief The maximum size of the array.
    [[nodiscard]] constexpr size_type capacity() const noexcept { return TMaxCount; }

    value_type& at(size_type index) noexcept
    {
        if (index >= m_size)
            throw std::out_of_range("BoundedArray<>::at() out of range");
        return m_data[index];
    }

    const value_type& at(size_type index) const noexcept
    {
        if (index >= m_size)
            throw std::out_of_range("BoundedArray<>::at() out of range");
        return m_data[index];
    }

    value_type& operator[](size_type index) noexcept { return m_data[index]; }
    const value_type& operator[](size_type index) const noexcept { return m_data[index]; }

    /// \brief A pointer to the data array.
    constexpr pointer data() noexcept { return m_data; }
    /// \brief A pointer to the data array.
    constexpr const_pointer data() const noexcept { return m_data; }

    /// \brief Changes the size of the array.
    ///
    /// If \p count is less than the current array size, the array is reduced to the
    /// first \p count elements and the rest is discarded.
    /// If \p count is greater than the current size, new default-constructed elements
    /// are appended.
    void resize(size_type count) noexcept
    {
        assert(count >= min_size() && count <= max_size());
        if (count > m_size)
            std::fill_n(m_data + m_size, count - m_size, TType());
        m_size = count;
    }

    /// \brief Changes the size of the array without initializing the elements.
    ///
    /// If \p count is less than the current array size, the array is reduced to the
    /// first \p count elements and the rest is discarded.
    /// If \p count is greater than the current size, the array size is increased.
    /// The new elements at the end are not initialized but left as is. It is the
    /// users responsibility to fill them with meaningful values.
    void uninitialized_resize(size_type count) noexcept
    {
        assert(count >= min_size() && count <= max_size());
        m_size = count;
    }

    /// \brief Adds an element.
    ///
    /// Appends \p value at the end of the array.
    void push_back(const value_type& value)
    {
        assert(m_size < max_size());
        m_data[m_size] = value;
        ++m_size;
    }

    /// \brief Assigns from an iterator range.
    ///
    /// Assigns the elements <tt>[first, last)</tt> to this vector.
    template <typename TInputIterator>
    constexpr void assign(TInputIterator first, TInputIterator last)
    {
        m_size = std::distance(first, last);
        assert(m_size >= min_size() && m_size <= max_size());
        std::copy(first, last, m_data);
    }

private:
    size_type m_size;
    value_type m_data[TMaxCount];
};

} // namespace frem

#endif // FREM_BOUNDEDARRAY_HPP
