
#ifndef ASTVISITOR_HPP
#define ASTVISITOR_HPP

#include "ParseResult.hpp"
#include "TypeRegistry.hpp"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceLocation.h>
#include <llvm/ADT/Optional.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>


/// Runs over the nodes of an abstract syntax tree (AST) and extracts the interface definitions
/// from it.
class AstVisitor : public clang::RecursiveASTVisitor<AstVisitor>
{
public:
    explicit AstVisitor(clang::ASTContext& ctxt,
                        clang::DiagnosticsEngine& diagEngine,
                        bool writeRpcs,
                        ParseResult& result);

    bool shouldVisitTemplateInstantiations() const;
    bool VisitVarDecl(clang::VarDecl* decl);
    bool VisitFunctionDecl(clang::FunctionDecl* fun);
    bool VisitCXXRecordDecl(clang::CXXRecordDecl* decl);

private:
    clang::ASTContext& m_context;
    clang::DiagnosticsEngine& m_diagEngine;
    ParseResult& m_parseResult;
    std::map<int, Annotation> m_annotationMap;
    TypeRegistry& m_typeRegistry;
    bool m_writeRpcs;

    void registerReturnValue(int value, const std::string& identifier);

    std::shared_ptr<InterfaceType> registerType(clang::SourceLocation diagLoc,
                                                clang::QualType type,
                                                std::set<std::string> typeStack);
    std::shared_ptr<InterfaceType> registerRecord(clang::QualType type,
                                                  std::set<std::string> typeStack);

    // A helper function to convert a template argument to an integer.
    std::optional<llvm::APSInt> toInteger(const clang::TemplateArgument& arg);
    // A helper to get an integer from the first argument of a constructor expression.
    std::optional<llvm::APSInt> toInteger(const clang::CXXConstructExpr* expr);

    std::optional<Parameter> getParameter(clang::SourceLocation diagLoc, clang::QualType type);
    std::optional<ReturnType> getReturnType(clang::SourceLocation diagLoc, clang::QualType type);

    llvm::Optional<RpcFunction::Invokee> findClassInstanceGetter(clang::CXXRecordDecl* record);

    void handleFremAnnotationVariable(clang::VarDecl* varDecl, const clang::CXXConstructExpr* expr);

    llvm::Optional<std::vector<std::string>> getTags(clang::SourceLocation annotationLocation,
                                                     const clang::CXXConstructExpr* expr);

    llvm::Optional<std::string> evaluateStringArgument(const clang::Expr* arg,
                                                       clang::SourceLocation annotationLocation);
};


#endif // ASTVISITOR_HPP
