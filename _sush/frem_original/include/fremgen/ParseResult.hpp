
#ifndef PARSERESULT_HPP
#define PARSERESULT_HPP

#include "TypeRegistry.hpp"

#include <llvm/Support/YAMLTraits.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>


// ----=====================================================================----
//     Parse result
// ----=====================================================================----

struct Annotation
{
    llvm::yaml::Hex32 code{0};
    std::string alias;
    std::vector<std::string> tags;
    std::string returnName;
    std::string via;
};

struct ReturnType
{
    std::string fullyQualifiedType;
    std::string decayedType;

    InterfaceTypeRef interfaceType;
};

struct Parameter
{
    enum Direction
    {
        Input,
        Output
    };

    std::string name;
    std::string fullyQualifiedType;
    std::string decayedType;
    Direction direction;

    InterfaceTypeRef interfaceType;
};

/// Meta-data of a function declared as remote procedure call.
struct RpcFunction
{
    /// Specifies the kind of this function.
    enum Kind
    {
        FreeFunction, //< A free function outside any class/struct scope.
        StaticFunction, //< A static function in a class/struct (no `this`-pointer needed for invocation).
        MemberFunction //< An ordinary member function of a class/struct.
    };

    /// How to invoke a member function. In other words: how to get the this pointer.
    struct Invokee
    {
        /// An expression returning an instance of the class to which the member function belongs.
        std::string expression;
        /// Whether the above expression returns a pointer or a reference.
        bool isPointer;
    };

    // Whether this is a class member function or a free function.
    Kind kind;
    // The name of the function.
    std::string id;
    // The fully qualified name of the function.
    std::string fullyQualifiedName;

    // The file name in which the RPC definition has been found.
    std::string fileName;
    // The line in which the RPC definition has been found.
    unsigned line;

    // The function's return type.
    ReturnType returnType;
    std::vector<Parameter> parameters;
    bool isNoexcept = false;
    std::vector<std::string> qualifiers;

    Invokee invokee;
    // Whether this function is part of a class with the FREM_REGISTERABLE_RPC_SERVICE macro.
    bool registerable = false;

    Annotation annotation;

    std::string docString;
};

struct ReturnValue
{
    int value;
    std::string id;
};

/// A pair of (type, version). This is used to declare configurations, where every version of a configuration
/// has an associated type. This allows to change the type of a configuration when its version is increased.
struct TypeRefWithVersion
{
    InterfaceTypeRef type;
    unsigned version = 0;

    friend bool operator<(const TypeRefWithVersion& lhs, const TypeRefWithVersion& rhs) noexcept
    {
        return std::tie(lhs.type, lhs.version) < std::tie(rhs.type, rhs.version);
    }
};

/// Meta-data about a configuration declaration.
struct Configuration
{
    std::vector<TypeRefWithVersion> versionTypes;
    std::string id;
    llvm::yaml::Hex32 setCode{0};
    llvm::yaml::Hex32 getCode{0};
    llvm::yaml::Hex32 versionCode{0};
    std::vector<std::string> tags;
    std::string fileName;
    unsigned line;

    friend bool operator<(const Configuration& lhs, const Configuration& rhs) noexcept
    {
        return std::tie(lhs.versionTypes,
                        lhs.id,
                        lhs.setCode,
                        lhs.getCode,
                        lhs.versionCode,
                        lhs.tags,
                        lhs.fileName,
                        lhs.line)
               < std::tie(rhs.versionTypes,
                          rhs.id,
                          rhs.setCode,
                          rhs.getCode,
                          rhs.versionCode,
                          rhs.tags,
                          rhs.fileName,
                          rhs.line);
    }
};

struct ErrorDescriptor
{
    std::string id; // id or value has to be set
    llvm::yaml::Hex32 value{0u};

    std::string description;
    std::string serviceText;
    std::string userText;
    std::string comment;

    friend bool operator<(const ErrorDescriptor& lhs, const ErrorDescriptor& rhs) noexcept
    {
        return std::tie(lhs.id,
                        lhs.value,
                        lhs.description,
                        lhs.serviceText,
                        lhs.userText,
                        lhs.comment)
               < std::tie(rhs.id,
                          rhs.value,
                          rhs.description,
                          rhs.serviceText,
                          rhs.userText,
                          rhs.comment);
    }
};

struct Socket
{
    InterfaceTypeRef packetType;
    std::string id;
    std::uint64_t port;
    std::vector<std::string> tags;
    std::string fileName;
    unsigned line;

    friend bool operator<(const Socket& lhs, const Socket& rhs) noexcept
    {
        return std::tie(lhs.packetType, lhs.id, lhs.port, lhs.tags, lhs.fileName, lhs.line)
               < std::tie(rhs.packetType, rhs.id, rhs.port, rhs.tags, rhs.fileName, rhs.line);
    }
};

struct ParseResult
{
    ParseResult() = default;

    ParseResult(const ParseResult&) = delete;
    ParseResult& operator=(const ParseResult&) = delete;

    std::unordered_set<std::string> processedFunctions;
    std::vector<RpcFunction> rpcFunctions;

    std::map<int, std::string> returnValues;

    std::vector<Configuration> configurations;

    std::vector<ErrorDescriptor> errorDescriptors;

    std::vector<Socket> sockets;
};

// ----=====================================================================----
//     YAML bindings
// ----=====================================================================----

namespace llvm::yaml
{
template <>
struct MappingTraits<ReturnType>
{
    static void mapping(IO& io, ReturnType& ret);
};

template <>
struct ScalarEnumerationTraits<Parameter::Direction>
{
    static void enumeration(IO& io, Parameter::Direction& value);
};

template <>
struct MappingTraits<Parameter>
{
    static void mapping(IO& io, Parameter& param);
};

template <>
struct ScalarEnumerationTraits<RpcFunction::Kind>
{
    static void enumeration(IO& io, RpcFunction::Kind& value);
};

template <>
struct MappingTraits<RpcFunction::Invokee>
{
    static void mapping(IO& io, RpcFunction::Invokee& invokee);
};

template <>
struct MappingTraits<RpcFunction>
{
    static void mapping(IO& io, RpcFunction& fun);
};

template <>
struct MappingTraits<ReturnValue>
{
    static void mapping(IO& io, ReturnValue& value);
};

template <>
struct MappingTraits<Configuration>
{
    static void mapping(IO& io, Configuration& cfg);
};

template <>
struct MappingTraits<ErrorDescriptor>
{
    static void mapping(IO& io, ErrorDescriptor& err);
};

template <>
struct MappingTraits<Socket>
{
    static void mapping(IO& io, Socket& socket);
};

template <>
struct MappingTraits<ParseResult>
{
    static void mapping(IO& io, ParseResult& result);
};

template <>
struct MappingTraits<TypeRefWithVersion>
{
    static void mapping(IO& io, TypeRefWithVersion& type);
};

} // namespace llvm::yaml

LLVM_YAML_IS_SEQUENCE_VECTOR(Configuration);
LLVM_YAML_IS_SEQUENCE_VECTOR(ErrorDescriptor);
LLVM_YAML_IS_SEQUENCE_VECTOR(Parameter);
LLVM_YAML_IS_SEQUENCE_VECTOR(ReturnValue);
LLVM_YAML_IS_SEQUENCE_VECTOR(RpcFunction);
LLVM_YAML_IS_SEQUENCE_VECTOR(Socket);
LLVM_YAML_IS_SEQUENCE_VECTOR(TypeRefWithVersion);

#endif // PARSERESULT_HPP
