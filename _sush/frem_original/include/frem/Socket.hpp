
#ifndef FREM_SOCKET_HPP
#define FREM_SOCKET_HPP

#include <cstdint>

namespace frem
{
template <typename TPacketType>
struct DatagramSocketDeclarator
{
    template <typename... TArgs>
    constexpr DatagramSocketDeclarator([[maybe_unused]] const char* id,
                                       [[maybe_unused]] std::uint8_t port,
                                       [[maybe_unused]] TArgs&&... args)
    {
    }
};
} // namespace frem

#endif // FREM_SOCKET_HPP
