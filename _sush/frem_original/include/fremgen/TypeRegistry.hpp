
#ifndef TYPEREGISTRY_HPP
#define TYPEREGISTRY_HPP

#include <llvm/ADT/APSInt.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/YAMLTraits.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


class InterfaceType;
class TypeRegistry;
class TypeRegistryPrivate;

using InterfaceTypeRef = std::shared_ptr<InterfaceType>;

namespace llvm::yaml
{
template <>
struct SequenceTraits<TypeRegistry>;
} // namespace llvm::yaml

/// A combination of file and line number.
struct FileLocation
{
    std::string fileName;
    unsigned line = 0;

    bool operator==(const FileLocation& rhs) const
    {
        return fileName == rhs.fileName && line == rhs.line;
    }
};

/// Holds meta-data about a single constant in an enum, which is the constant's
/// name and its associated value. Assumes that all values fit in an int64_t.
struct EnumConstant
{
    std::string fieldName;
    std::int64_t value;
};

/// The meta-data of one struct field entry, which is the field's name and its type.
struct StructFieldData
{
    std::string name;
    InterfaceTypeRef type;
};

/// The interface type is the type specification as it appears in a function or
/// in struct definition, for example.
struct InterfaceType
{
    /// All possible kinds of interface types.
    enum class Kind
    {
        None,          //< An unknown type.
        BuiltIn,       //< A built-in type such as char or int32_t.
        Enum,          //< An enum type.
        Struct,        //< A struct type.
        FixedArray,    //< An array of fixed size.
        BoundedArray,  //< An array with bounded size.
        Optional,      //< An optional type (combination of bool and type).
        Variant,       //< A variant type (combination of integer and list of types).
        Future,        //< A future returned from an asynchronous function.
        BoundedString, //< A string of bounded length.
        FixedString,   //< A string of fixed length.
    };

    Kind kind = Kind::None;

    // Built-in types, enums and structs are identified by their ID. These
    // types also have a fully-qualified name, which is a unique C++ name.
    std::string id;
    std::string fullyQualifiedName;

    // The constants, which make up an enum.
    std::vector<EnumConstant> enumConstants;

    // Enum, optional and future have an underlying type, which they wrap.
    InterfaceTypeRef underlyingType;
    // The possible types which a variant can hold.
    std::vector<InterfaceTypeRef> underlyingTypesList;

    // The fields of a struct.
    std::vector<StructFieldData> structFields;
    unsigned configurationVersion{0}; // TODO: Delete this

    // Arrays and string have a type for their elements, a type for the size and
    // optional bounds for their size.
    InterfaceTypeRef elementType;
    std::int64_t minSize{-1};
    std::int64_t maxSize{-1};
    InterfaceTypeRef sizeType;

    // Enums and structs have a unique hash code, which is intended to be transfered as
    // type specifier over an otherwise untyped interface.
    llvm::yaml::Hex32 hash{0};

    // Where the type was declared.
    FileLocation declarationLocation;

    // Where the `FREM_TYPE_ALIAS` macro was invoked.
    FileLocation expositionLocation;
};

// Helper needed for YAML input of type registry.
struct InterfaceTypeRegistrar
{
    std::shared_ptr<InterfaceType> type;
};

class TypeRegistry
{
public:
    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;

    static TypeRegistry& instance();

    void registerType(std::string fullyQualifiedName, std::shared_ptr<InterfaceType> desc);

    InterfaceTypeRef lookup(std::string name) const;

    std::vector<InterfaceType> registeredTypes() const;

    void setTypeAlias(std::string fullyQualifiedName,
                      std::string alias,
                      FileLocation expositionLocation);

    void setInternalAlias(std::string fullyQualifiedName, std::string alias);

private:
    TypeRegistry();

    TypeRegistryPrivate* m_pimpl;

    friend class llvm::yaml::SequenceTraits<TypeRegistry>;
};

// ----=====================================================================----
//     YAML bindings
// ----=====================================================================----

namespace llvm::yaml
{
template <>
struct MappingTraits<FileLocation>
{
    static void mapping(IO& io, FileLocation& loc);
};

template <>
struct MappingTraits<EnumConstant>
{
    static void mapping(IO& io, EnumConstant& field);
};

template <>
struct MappingTraits<StructFieldData>
{
    static void mapping(IO& io, StructFieldData& field);
};

template <>
struct ScalarEnumerationTraits<InterfaceType::Kind>
{
    static void enumeration(IO& io, InterfaceType::Kind& value);
};

template <>
struct MappingTraits<InterfaceType>
{
    static void mapping(IO& io, InterfaceType& type);
};

template <>
struct MappingTraits<InterfaceTypeRef>
{
    static void mapping(IO& io, InterfaceTypeRef& type);
};

template <>
struct MappingTraits<InterfaceTypeRegistrar>
{
    static void mapping(IO& io, InterfaceTypeRegistrar& type);
};

template <>
struct SequenceTraits<TypeRegistry>
{
    static size_t size(IO& io, TypeRegistry& reg);

    static InterfaceTypeRegistrar& element(IO& io, TypeRegistry& reg, size_t index);
};

} // namespace llvm::yaml

LLVM_YAML_IS_SEQUENCE_VECTOR(StructFieldData);
LLVM_YAML_IS_SEQUENCE_VECTOR(EnumConstant);
LLVM_YAML_IS_SEQUENCE_VECTOR(InterfaceType);
LLVM_YAML_IS_SEQUENCE_VECTOR(InterfaceTypeRef);

#endif // TYPEREGISTRY_HPP
