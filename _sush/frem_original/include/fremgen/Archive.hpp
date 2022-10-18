
#ifndef ARCHIVE_HPP
#define ARCHIVE_HPP

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

/// Writes binary data to an output file stream.
class OutArchive
{
public:
    OutArchive(const std::string& filename)
        : m_stream(filename, std::fstream::trunc | std::fstream::binary)
    {
    }

    OutArchive(const OutArchive&) = delete;
    OutArchive& operator=(const OutArchive&) = delete;

    // Prevent implicit type conversions by deleting all operator<< overloads.
    // If this was not present, `ar << 'a'` would trigger an implicit conversion
    // to `uint32_t`.
    template <typename T>
    OutArchive& operator<<(const T&) = delete;

    OutArchive& operator<<(uint32_t value)
    {
        m_stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
        return *this;
    }

    OutArchive& operator<<(const std::string& str)
    {
        *this << uint32_t(str.size());
        m_stream.write(str.data(), str.size());
        return *this;
    }

    OutArchive& operator<<(const std::vector<char>& data)
    {
        *this << uint32_t(data.size());
        m_stream.write(data.data(), data.size());
        return *this;
    }

    template <typename T>
    OutArchive& operator<<(const std::vector<T>& array)
    {
        *this << uint32_t(array.size());
        for (const auto& entry : array)
            *this << entry;
        return *this;
    }

private:
    std::ofstream m_stream;
};

/// Reads binary data from an input file stream.
class InArchive
{
public:
    InArchive(const std::string& filename)
    {
        std::ifstream stream(filename, std::ios::in | std::ios::binary | std::ios::ate);
        auto fileSize = stream.tellg();
        stream.seekg(0, std::ios::beg);
        m_data.resize(fileSize);
        stream.read(m_data.data(), fileSize);
        m_iter = m_data.data();
    }

    InArchive(const InArchive&) = delete;
    InArchive& operator=(const InArchive&) = delete;

    template <typename T>
    InArchive& operator>>(T) = delete;

    InArchive& operator>>(uint32_t& value)
    {
        std::memcpy(reinterpret_cast<char*>(&value), m_iter, sizeof(value));
        m_iter += sizeof(value);
        return *this;
    }

    InArchive& operator>>(std::string& str)
    {
        uint32_t size;
        *this >> size;
        str.resize(size);
        std::memcpy(str.data(), m_iter, size);
        m_iter += size;
        return *this;
    }

    InArchive& operator>>(std::vector<char>& data)
    {
        uint32_t size;
        *this >> size;
        data.resize(size);
        std::memcpy(data.data(), m_iter, size);
        m_iter += size;
        return *this;
    }

    template <typename T>
    InArchive& operator>>(std::vector<T>& array)
    {
        uint32_t size;
        *this >> size;
        array.reserve(size);
        for (uint32_t count = 0; count < size; ++count) {
            T entry;
            *this >> entry;
            array.push_back(entry);
        }
        return *this;
    }

private:
    std::vector<char> m_data;
    const char* m_iter = nullptr;
};

#endif // ARCHIVE_HPP
