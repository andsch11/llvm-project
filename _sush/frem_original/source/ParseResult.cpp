
#include "fremgen/ParseResult.hpp"

#include <algorithm>
#include <set>

// ----=====================================================================----
//     YAML bindings
// ----=====================================================================----

namespace llvm::yaml
{
void MappingTraits<ReturnType>::mapping(IO& io, ReturnType& ret)
{
    io.mapRequired("type", ret.interfaceType);

    // ---- C++ stuff
    io.mapRequired("fullyQualifiedType", ret.fullyQualifiedType);
    io.mapRequired("decayedType", ret.decayedType);
}

void ScalarEnumerationTraits<Parameter::Direction>::enumeration(IO& io, Parameter::Direction& value)
{
    io.enumCase(value, "in", Parameter::Input);
    io.enumCase(value, "out", Parameter::Output);
}

void MappingTraits<Parameter>::mapping(IO& io, Parameter& param)
{
    io.mapRequired("name", param.name);
    io.mapRequired("direction", param.direction);
    io.mapRequired("type", param.interfaceType);

    // ---- C++ stuff
    io.mapRequired("fullyQualifiedType", param.fullyQualifiedType);
    io.mapRequired("decayedType", param.decayedType);
}

void ScalarEnumerationTraits<RpcFunction::Kind>::enumeration(IO& io, RpcFunction::Kind& value)
{
    io.enumCase(value, "free", RpcFunction::Kind::FreeFunction);
    io.enumCase(value, "static", RpcFunction::Kind::StaticFunction);
    io.enumCase(value, "member", RpcFunction::Kind::MemberFunction);
}

void MappingTraits<RpcFunction::Invokee>::mapping(IO& io, RpcFunction::Invokee& invokee)
{
    io.mapRequired("getter", invokee.expression);
    io.mapRequired("pointer", invokee.isPointer);
}

void MappingTraits<RpcFunction>::mapping(IO& io, RpcFunction& fun)
{
    auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {
        if (from.empty())
            return;
        size_t startPos = 0;
        while ((startPos = str.find(from, startPos)) != std::string::npos) {
            str.replace(startPos, from.length(), to);
            startPos += to.length();
        }
    };

    io.mapRequired("id", fun.id);
    io.mapRequired("code", fun.annotation.code);
    io.mapOptional("via", fun.annotation.via, std::string());
    io.mapRequired("return", fun.returnType);
    io.mapRequired("parameters", fun.parameters);

    io.mapRequired("doc", fun.docString);
    io.mapRequired("tags", fun.annotation.tags);

    io.mapOptional("returnName", fun.annotation.returnName, std::string());

    // ---- C++ stuff
    io.mapRequired("kind", fun.kind);
    io.mapRequired("fullyQualifiedName", fun.fullyQualifiedName);
    io.mapRequired("file", fun.fileName);
    io.mapRequired("line", fun.line);
    io.mapRequired("noexcept", fun.isNoexcept);
    io.mapOptional("qualifiers", fun.qualifiers);

    if (fun.kind == RpcFunction::Kind::MemberFunction)
        io.mapRequired("instance", fun.invokee);
    io.mapOptional("registerable", fun.registerable, false);

    if (io.outputting()) {
        std::string signature = fun.returnType.fullyQualifiedType + " " + fun.fullyQualifiedName
                                + "(";
        bool first = true;
        for (const auto& p : fun.parameters) {
            if (!first)
                signature += ", ";
            signature += p.fullyQualifiedType;
            first = false;
        }
        signature += ")";
        io.mapRequired("signature", signature);
    }
}

void MappingTraits<ReturnValue>::mapping(IO& io, ReturnValue& value)
{
    io.mapRequired("name", value.id);
    io.mapRequired("value", value.value);
}

void MappingTraits<Configuration>::mapping(IO& io, Configuration& cfg)
{
    // If one of the set/get/version codes is missing, use the following
    // code.
    unsigned maxCode = std::max(std::max(cfg.setCode, cfg.getCode), cfg.versionCode);
    if (maxCode != 0) {
        if (cfg.setCode == 0u)
            cfg.setCode = ++maxCode;
        if (cfg.getCode == 0u)
            cfg.getCode = ++maxCode;
        if (cfg.versionCode == 0u)
            cfg.versionCode = ++maxCode;
    }

    io.mapRequired("id", cfg.id);
    io.mapRequired("versionTypes", cfg.versionTypes);
    io.mapOptional("setCode", cfg.setCode, llvm::yaml::Hex32(0u));
    io.mapOptional("getCode", cfg.getCode, llvm::yaml::Hex32(0u));
    io.mapOptional("versionCode", cfg.versionCode, llvm::yaml::Hex32(0u));
    io.mapRequired("tags", cfg.tags);
    io.mapRequired("file", cfg.fileName);
    io.mapRequired("line", cfg.line);
}

void MappingTraits<ErrorDescriptor>::mapping(IO& io, ErrorDescriptor& err)
{
    io.mapOptional("id", err.id, std::string());
    if (err.id.empty())
        io.mapRequired("value", err.value);
    io.mapRequired("description", err.description);
    io.mapOptional("serviceText", err.serviceText, std::string());
    io.mapOptional("userText", err.userText, std::string());
    io.mapOptional("comment", err.comment, std::string());
}

void MappingTraits<Socket>::mapping(IO& io, Socket& socket)
{
    io.mapRequired("id", socket.id);
    io.mapRequired("port", socket.port);
    io.mapRequired("packetType", socket.packetType);
    io.mapOptional("tags", socket.tags);
}

template <typename T>
static void unique(std::vector<T>& vec)
{
    std::set<T> set(vec.begin(), vec.end());
    vec.assign(set.begin(), set.end());
}

void MappingTraits<ParseResult>::mapping(IO& io, ParseResult& result)
{
    std::vector<ReturnValue> returnValues;
    if (io.outputting()) {
        for (const auto& rv : result.returnValues)
            returnValues.push_back(ReturnValue{rv.first, rv.second});
    }
    if (io.outputting()) {
        unique(result.configurations);
        unique(result.errorDescriptors);
        unique(result.sockets);
    }

    io.mapRequired("returnValues", returnValues);
    io.mapRequired("types", TypeRegistry::instance());
    io.mapRequired("functions", result.rpcFunctions);
    io.mapRequired("sockets", result.sockets);
    io.mapRequired("configurations", result.configurations);
    io.mapRequired("errors", result.errorDescriptors);

    if (!io.outputting()) {
        unique(result.configurations);
        unique(result.errorDescriptors);
        unique(result.sockets);
    }
    if (!io.outputting()) {
        for (const auto& rv : returnValues)
            result.returnValues[rv.value] = rv.id;
    }
}

void MappingTraits<TypeRefWithVersion>::mapping(IO& io, TypeRefWithVersion& type)
{
    io.mapRequired("version", type.version);
    io.mapRequired("type", type.type);
}

} // namespace llvm::yaml
