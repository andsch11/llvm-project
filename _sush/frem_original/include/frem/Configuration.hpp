
#ifndef FREM_CONFIGURATION_HPP
#define FREM_CONFIGURATION_HPP

#include <type_traits>

namespace frem
{
struct GetCode
{
    constexpr GetCode([[maybe_unused]] unsigned code) {}
};

struct SetCode
{
    constexpr SetCode([[maybe_unused]] unsigned code) {}
};

struct VersionCode
{
    constexpr VersionCode([[maybe_unused]] unsigned code) {}
};

template <unsigned TVersion>
struct ConfigurationVersion
{
};

template <typename... TTypes>
struct ConfigurationDeclarator
{
    static_assert(sizeof...(TTypes) > 0, "Need at least one type");
    static_assert(std::conjunction_v<std::is_trivially_copyable<TTypes>...>,
                  "Not all types are trivially copyable");

    template <typename... TArgs>
    constexpr ConfigurationDeclarator([[maybe_unused]] const char* id,
                                      [[maybe_unused]] TArgs&&... args)
    {
    }
};

} // namespace frem

#endif // FREM_CONFIGURATION_HPP
