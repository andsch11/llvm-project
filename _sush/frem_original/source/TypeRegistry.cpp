
#include "fremgen/TypeRegistry.hpp"

#include "city/city.h"

#include <unordered_map>


using namespace llvm;
using namespace std;


struct ForwardTypeAlias
{
    std::string alias;
    FileLocation expositionLocation;
};

struct TypeRegistryPrivate
{
    std::vector<std::shared_ptr<InterfaceType>> registeredTypesInOrder;
    std::vector<std::shared_ptr<InterfaceType>> registeredUserDefinedTypesInOrder;
    std::unordered_map<std::string, std::shared_ptr<InterfaceType>> registeredTypeMap;
    std::unordered_map<std::string, ForwardTypeAlias> forwardTypeAliases;
    std::unordered_map<std::string, std::string> aliasToFullyQualifiedNameMap;
    InterfaceTypeRegistrar registrar;
};

// ----=====================================================================----
//     TypeRegistry
// ----=====================================================================----

TypeRegistry::TypeRegistry()
    : m_pimpl(new TypeRegistryPrivate)
{
    // Populate the built-in types.
    // TODO: Remove frem::RpcResult from the built-in types.
    for (std::string name : {"void",
                             "bool",
                             "char",
                             "int8_t",
                             "int16_t",
                             "int32_t",
                             "int64_t",
                             "uint8_t",
                             "uint16_t",
                             "uint32_t",
                             "uint64_t",
                             "float",
                             "double",
                             "frem::RpcResult"}) {
        auto interface = std::make_shared<InterfaceType>();
        interface->kind = InterfaceType::Kind::BuiltIn;
        interface->id = name;
        interface->fullyQualifiedName = name;
        registerType(name, interface);
        // Make std::intX_t a synonym for intX_t (from the RPC interface perspective).
        if (name.find("int") != std::string::npos)
            setInternalAlias(name, "std::" + name);
    }
}

TypeRegistry& TypeRegistry::instance()
{
    static TypeRegistry registry;
    return registry;
}

void TypeRegistry::registerType(std::string fullyQualifiedName, std::shared_ptr<InterfaceType> desc)
{
    m_pimpl->registeredTypesInOrder.push_back(desc);
    if (desc->kind != InterfaceType::Kind::BuiltIn)
        m_pimpl->registeredUserDefinedTypesInOrder.push_back(desc);
    m_pimpl->registeredTypeMap.insert(make_pair(fullyQualifiedName, desc));

    if (desc->kind == InterfaceType::Kind::Enum || desc->kind == InterfaceType::Kind::Struct) {
        // Generate the hash code for enums and structs from their fully-qualified name.
        desc->hash = CityHash32(fullyQualifiedName.c_str(), fullyQualifiedName.size());
        auto iter = m_pimpl->forwardTypeAliases.find(fullyQualifiedName);
        if (iter != m_pimpl->forwardTypeAliases.end()) {
            desc->id = iter->second.alias;
            desc->expositionLocation = iter->second.expositionLocation;
            m_pimpl->aliasToFullyQualifiedNameMap[iter->second.alias] = fullyQualifiedName;
            m_pimpl->forwardTypeAliases.erase(iter);
        }
    }
}

std::shared_ptr<InterfaceType> TypeRegistry::lookup(std::string name) const
{
    auto aliasIter = m_pimpl->aliasToFullyQualifiedNameMap.find(name);
    if (aliasIter != m_pimpl->aliasToFullyQualifiedNameMap.end()) {
        name = aliasIter->second;
    }

    auto iter = m_pimpl->registeredTypeMap.find(name);
    if (iter != m_pimpl->registeredTypeMap.end()) {
        return iter->second;
    }

    return nullptr;
}

std::vector<InterfaceType> TypeRegistry::registeredTypes() const
{
    std::vector<InterfaceType> types;
    for (const auto& type : m_pimpl->registeredTypesInOrder)
        if (type->kind != InterfaceType::Kind::BuiltIn)
            types.push_back(*type);
    return types;
}

void TypeRegistry::setTypeAlias(std::string fullyQualifiedName,
                                std::string alias,
                                FileLocation expositionLocation)
{
    auto iter = m_pimpl->registeredTypeMap.find(fullyQualifiedName);
    if (iter == m_pimpl->registeredTypeMap.end()) {
        // The type has not been defined yet (because the alias has been written before the type definition).
        // Store this alias and apply it later when the type is defined.
        m_pimpl->forwardTypeAliases[fullyQualifiedName] = {alias, expositionLocation};
        return;
    }

    if (iter->second->kind == InterfaceType::Kind::Enum
        || iter->second->kind == InterfaceType::Kind::Struct) {
        iter->second->id = alias;
        iter->second->expositionLocation = expositionLocation;
        m_pimpl->aliasToFullyQualifiedNameMap[alias] = fullyQualifiedName;
    }
}

void TypeRegistry::setInternalAlias(std::string fullyQualifiedName, std::string alias)
{
    m_pimpl->aliasToFullyQualifiedNameMap[alias] = fullyQualifiedName;
}

// ----=====================================================================----
//     YAML bindings
// ----=====================================================================----

namespace llvm::yaml
{
void MappingTraits<FileLocation>::mapping(IO& io, FileLocation& loc)
{
    io.mapRequired("file", loc.fileName);
    io.mapRequired("line", loc.line);
}

void MappingTraits<EnumConstant>::mapping(IO& io, EnumConstant& field)
{
    io.mapRequired("name", field.fieldName);
    io.mapRequired("value", field.value);
}

void MappingTraits<StructFieldData>::mapping(IO& io, StructFieldData& field)
{
    io.mapRequired("name", field.name);
    io.mapRequired("type", field.type);
}

void ScalarEnumerationTraits<InterfaceType::Kind>::enumeration(IO& io, InterfaceType::Kind& value)
{
    io.enumCase(value, "none", InterfaceType::Kind::None);
    io.enumCase(value, "builtin", InterfaceType::Kind::BuiltIn);
    io.enumCase(value, "enum", InterfaceType::Kind::Enum);
    io.enumCase(value, "struct", InterfaceType::Kind::Struct);
    io.enumCase(value, "fixedArray", InterfaceType::Kind::FixedArray);
    io.enumCase(value, "boundedArray", InterfaceType::Kind::BoundedArray);
    io.enumCase(value, "fixedString", InterfaceType::Kind::FixedString);
    io.enumCase(value, "boundedString", InterfaceType::Kind::BoundedString);
    io.enumCase(value, "optional", InterfaceType::Kind::Optional);
    io.enumCase(value, "variant", InterfaceType::Kind::Variant);
    io.enumCase(value, "future", InterfaceType::Kind::Future);
}

void MappingTraits<InterfaceType>::mapping(IO& io, InterfaceType& type)
{
    io.mapRequired("kind", type.kind);
    switch (type.kind) {
    case InterfaceType::Kind::BuiltIn:
        io.mapRequired("id", type.id);
        // ---- C++ stuff
        io.mapRequired("fullyQualifiedName", type.fullyQualifiedName);
        break;

    case InterfaceType::Kind::Enum:
        io.mapRequired("id", type.id);
        io.mapRequired("code", type.hash);
        io.mapRequired("underlyingType", type.underlyingType);
        io.mapRequired("constants", type.enumConstants);
        // ---- C++ stuff
        io.mapRequired("fullyQualifiedName", type.fullyQualifiedName);
        io.mapOptional("declaredAt", type.declarationLocation, FileLocation());
        io.mapOptional("exposedAt", type.expositionLocation, FileLocation());
        break;

    case InterfaceType::Kind::Struct:
        io.mapRequired("id", type.id);
        io.mapRequired("code", type.hash);
        io.mapRequired("fields", type.structFields);
        io.mapOptional("configurationVersion", type.configurationVersion, 0u);
        // ---- C++ stuff
        io.mapRequired("fullyQualifiedName", type.fullyQualifiedName);
        io.mapOptional("declaredAt", type.declarationLocation, FileLocation());
        io.mapOptional("exposedAt", type.expositionLocation, FileLocation());
        break;

    default:
        // Array types, optionals, variants and futures are not exposed in the type registry.
        assert(false);
    }
}

void MappingTraits<InterfaceTypeRef>::mapping(IO& io, InterfaceTypeRef& type)
{
    InterfaceType::Kind kind;
    if (io.outputting())
        kind = type->kind;
    io.mapRequired("kind", kind);
    switch (kind) {
    case InterfaceType::Kind::None:
        break;

    case InterfaceType::Kind::BuiltIn:
    case InterfaceType::Kind::Enum:
    case InterfaceType::Kind::Struct:
        if (io.outputting()) {
            io.mapRequired("id", type->id);
        }
        else {
            std::string id;
            io.mapRequired("id", id);
            type = TypeRegistry::instance().lookup(id);
            if (!type)
                io.setError(llvm::Twine("unknown type '") + id + "'");
        }
        break;

    case InterfaceType::Kind::FixedArray:
        if (!io.outputting()) {
            type = std::make_shared<InterfaceType>();
            type->kind = kind;
        }
        io.mapRequired("elementType", type->elementType);
        io.mapRequired("size", type->minSize);
        break;

    case InterfaceType::Kind::BoundedArray:
        if (!io.outputting()) {
            type = std::make_shared<InterfaceType>();
            type->kind = kind;
        }
        io.mapRequired("elementType", type->elementType);
        io.mapRequired("sizeType", type->sizeType);
        io.mapRequired("minSize", type->minSize);
        io.mapRequired("maxSize", type->maxSize);
        break;

    case InterfaceType::Kind::FixedString:
        if (!io.outputting()) {
            type = std::make_shared<InterfaceType>();
            type->kind = kind;
        }
        io.mapRequired("charType", type->elementType);
        io.mapRequired("size", type->minSize);
        break;

    case InterfaceType::Kind::BoundedString:
        if (!io.outputting()) {
            type = std::make_shared<InterfaceType>();
            type->kind = kind;
        }
        io.mapRequired("charType", type->elementType);
        io.mapRequired("sizeType", type->sizeType);
        io.mapRequired("minSize", type->minSize);
        io.mapRequired("maxSize", type->maxSize);
        break;

    case InterfaceType::Kind::Optional:
        if (!io.outputting()) {
            type = std::make_shared<InterfaceType>();
            type->kind = kind;
        }
        io.mapRequired("underlyingType", type->underlyingType);
        break;

    case InterfaceType::Kind::Variant:
        if (!io.outputting()) {
            type = std::make_shared<InterfaceType>();
            type->kind = kind;
        }
        io.mapRequired("underlyingTypes", type->underlyingTypesList);
        break;

    case InterfaceType::Kind::Future:
        if (!io.outputting()) {
            type = std::make_shared<InterfaceType>();
            type->kind = kind;
        }
        io.mapRequired("underlyingType", type->underlyingType);
        break;
    }
}

void MappingTraits<InterfaceTypeRegistrar>::mapping(IO& io, InterfaceTypeRegistrar& registrar)
{
    if (io.outputting()) {
        MappingTraits<InterfaceType>::mapping(io, *registrar.type);
    }
    else {
        InterfaceType type;
        MappingTraits<InterfaceType>::mapping(io, type);
        if (type.kind != InterfaceType::Kind::BuiltIn) {
            TypeRegistry::instance().registerType(type.fullyQualifiedName,
                                                  std::make_shared<InterfaceType>(type));
            if (type.id != type.fullyQualifiedName)
                TypeRegistry::instance().setTypeAlias(type.fullyQualifiedName,
                                                      type.id,
                                                      type.expositionLocation);
        }
    }
}

size_t SequenceTraits<TypeRegistry>::size(IO&, TypeRegistry& reg)
{
    return reg.m_pimpl->registeredUserDefinedTypesInOrder.size();
}

InterfaceTypeRegistrar& SequenceTraits<TypeRegistry>::element(IO& io,
                                                              TypeRegistry& reg,
                                                              size_t index)
{
    if (io.outputting()) {
        reg.m_pimpl->registrar.type = reg.m_pimpl->registeredUserDefinedTypesInOrder[index];
    }

    return reg.m_pimpl->registrar;
}

} // namespace llvm::yaml
