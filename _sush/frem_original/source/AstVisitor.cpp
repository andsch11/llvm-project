
#include "fremgen/AstVisitor.hpp"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <clang/AST/APValue.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/QualTypeNames.h>
#include <clang/AST/Type.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/Tooling.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <iostream>
#include <set>
#include <sstream>


using namespace clang;
using namespace llvm;
using namespace std;


//! Check if the \p decl is in the top-level \p nameSpace.
static bool isInToplevelNamespace(const clang::Decl* decl, llvm::StringRef nameSpace)
{
    // Check if the decl is embedded in a namespace decl.
    const NamespaceDecl* nsDecl = dyn_cast<NamespaceDecl>(decl->getDeclContext());
    if (!nsDecl)
        return false;
    // Check if the name matches.
    const IdentifierInfo* info = nsDecl->getIdentifier();
    if (!info || !info->getName().equals(nameSpace))
        return false;
    // Check if the namespace is a top-level namespace.
    return isa<TranslationUnitDecl>(nsDecl->getDeclContext());
}

/// Extracts the location from a variable name `var`.
/// The expected variable name format is (see AstConsumer.cpp):
/// prefix '_' counter '_' annotationGroup '_' location
static void extractionAnnotationLocation(std::string s,
                                         int& annotationGroup,
                                         SourceLocation& annotationLocation)
{
    auto locationEnd = s.length();
    auto preLocationStart = s.rfind('_', locationEnd - 1);
    auto preIndexStart = s.rfind('_', preLocationStart - 1);

    std::istringstream ss1(s.substr(preIndexStart + 1, preLocationStart - preIndexStart - 1));
    ss1 >> annotationGroup;

    unsigned raw;
    std::istringstream ss2(s.substr(preLocationStart + 1, locationEnd - preLocationStart - 1));
    ss2 >> raw;
    annotationLocation = SourceLocation::getFromRawEncoding(raw);
}

static std::string getFullyQualifiedNameOfType(QualType type, const ASTContext& ctxt)
{
    return TypeName::getFullyQualifiedName(
        TypeName::getFullyQualifiedType(type, ctxt, /*WithGlobalNsPrefix =*/false),
        ctxt,
        ctxt.getPrintingPolicy(),
        /*WithGlobalNsPrefix =*/false);
}

static const clang::Expr* stripExpr(const clang::Expr* expr)
{
    while (expr) {
        if (auto* ewc = llvm::dyn_cast<ExprWithCleanups>(expr))
            expr = ewc->getSubExpr();
        else if (auto* mte = dyn_cast<MaterializeTemporaryExpr>(expr))
            expr = mte->getSubExpr();
        else if (auto* ice = dyn_cast<ImplicitCastExpr>(expr))
            expr = ice->getSubExpr();
        else if (auto* fce = dyn_cast<CXXFunctionalCastExpr>(expr))
            expr = fce->getSubExpr();
        else
            break;
    }

    return expr;
}

static std::string getNamespaces(clang::DeclContext* ctxt)
{
    std::string namespaces;
    while (ctxt) {
        if (const auto* ns = dyn_cast<NamespaceDecl>(ctxt)) {
            if (!ns->isInline())
                namespaces = ns->getNameAsString() + "::" + namespaces;
        }
        ctxt = ctxt->getParent();
    }
    return namespaces;
}

// ----=====================================================================----
//     AstVisitor
// ----=====================================================================----

AstVisitor::AstVisitor(clang::ASTContext& ctxt,
                       clang::DiagnosticsEngine& diagEngine,
                       bool writeRpcs,
                       ParseResult& result)
    : m_context(ctxt)
    , m_diagEngine(diagEngine)
    , m_parseResult(result)
    , m_typeRegistry(TypeRegistry::instance())
    , m_writeRpcs(writeRpcs)
{
}

bool AstVisitor::shouldVisitTemplateInstantiations() const
{
    return true;
}

llvm::Optional<std::vector<std::string>> AstVisitor::getTags(
    clang::SourceLocation annotationLocation, const clang::CXXConstructExpr* expr)
{
    // cout << "getTags" << endl;
    // expr->dump();
    // cout << expr->getStmtClassName() << endl;
    expr = dyn_cast_or_null<CXXConstructExpr>(stripExpr(expr));

    if (!expr || expr->getNumArgs() == 0) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error, "expected an argument");
        m_diagEngine.Report(annotationLocation, id);
        return llvm::Optional<std::vector<std::string>>();
    }

    std::vector<std::string> tags;
    for (unsigned argument = 0; argument < expr->getNumArgs(); ++argument) {
        auto tag = evaluateStringArgument(expr->getArg(argument), annotationLocation);
        if (!tag) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected a string literal");
            m_diagEngine.Report(annotationLocation, id);
            return llvm::Optional<std::vector<std::string>>();
        }
        tags.push_back(*tag);
    }
    return tags;
}

llvm::Optional<std::string> AstVisitor::evaluateStringArgument(
    const clang::Expr* arg, clang::SourceLocation annotationLocation)
{
    Expr::EvalResult result;
    bool worked = arg->EvaluateAsRValue(result, m_context);
    if (!worked) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                   "expected a string argument");
        m_diagEngine.Report(annotationLocation, id);
        return llvm::Optional<std::string>();
    }

    if (result.Val.isLValue()) {
        auto base = result.Val.getLValueBase();
        auto literal = dyn_cast<const clang::StringLiteral>(base.get<const Expr*>());
        if (!literal) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected a string literal");
            m_diagEngine.Report(annotationLocation, id);
            return llvm::Optional<std::string>();
        }
        return literal->getString().str();
    }

    if (result.Val.isStruct()) {
        if (result.Val.getStructNumFields() != 1 || !result.Val.getStructField(0).isArray()) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected a StringLiteral");
            m_diagEngine.Report(annotationLocation, id);
            return llvm::Optional<std::string>();
        }

        std::string strResult;
        auto field = result.Val.getStructField(0);
        for (unsigned i = 0; i < field.getArrayInitializedElts(); ++i) {
            const APValue& element = field.getArrayInitializedElt(i);
            if (!element.isInt())
                return llvm::Optional<std::string>();
            auto value = element.getInt().getSExtValue();
            if (value == 0 || static_cast<char>(value) != value)
                break;
            strResult += static_cast<char>(value);
        }
        return strResult;
    }

    return llvm::Optional<std::string>();
}

void AstVisitor::handleFremAnnotationVariable(clang::VarDecl* varDecl,
                                              const clang::CXXConstructExpr* constructExpr)
{
    // cout << "handleFremAnnotationVariable: " << varDecl->getNameAsString() << endl;

    // Extract the index and the location of the annotation from the variable
    // name.
    int annotationGroup;
    SourceLocation annotationLocation;
    extractionAnnotationLocation(varDecl->getNameAsString(), annotationGroup, annotationLocation);

    // If the annotation is already available in parts, load it.
    Annotation annotation;
    auto iter = m_annotationMap.find(annotationGroup);
    if (iter != m_annotationMap.end())
        annotation = iter->second;

    auto name = constructExpr->getConstructor()->getNameAsString();
    if (name == "Code") {
        if (constructExpr->getNumArgs() != 1) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected an argument");
            m_diagEngine.Report(annotationLocation, id);
            return;
        }
        Expr::EvalResult code;
        bool worked = constructExpr->getArg(0)->EvaluateAsInt(code, m_context);
        if (!worked || !code.Val.isInt()) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected an integer");
            m_diagEngine.Report(annotationLocation, id);
            return;
        }

        annotation.code = code.Val.getInt().getExtValue();
        m_annotationMap[annotationGroup] = annotation;
        return;
    }

    if (name == "Alias") {
        if (constructExpr->getNumArgs() != 1) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected an argument");
            m_diagEngine.Report(annotationLocation, id);
            return;
        }
        auto alias = evaluateStringArgument(constructExpr->getArg(0), annotationLocation);
        if (!alias) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected a string literal");
            m_diagEngine.Report(annotationLocation, id);
            return;
        }

        annotation.alias = *alias;
        m_annotationMap[annotationGroup] = annotation;
        return;
    }

    if (name == "Via") {
        if (constructExpr->getNumArgs() != 1) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected an argument");
            m_diagEngine.Report(annotationLocation, id);
            return;
        }
        auto via = evaluateStringArgument(constructExpr->getArg(0), annotationLocation);
        if (!via) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected a string literal");
            m_diagEngine.Report(annotationLocation, id);
            return;
        }

        annotation.via = *via;
        m_annotationMap[annotationGroup] = annotation;
        return;
    }

    if (name == "Tags") {
        auto tags = getTags(annotationLocation, constructExpr);
        if (!tags)
            return;

        annotation.tags = *tags;
        m_annotationMap[annotationGroup] = annotation;
        return;
    }

    if (name == "ReturnName") {
        if (constructExpr->getNumArgs() != 1) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected an argument");
            m_diagEngine.Report(annotationLocation, id);
            return;
        }
        auto name = evaluateStringArgument(constructExpr->getArg(0), annotationLocation);
        if (!name) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                       "expected a string literal");
            m_diagEngine.Report(annotationLocation, id);
            return;
        }

        annotation.returnName = *name;
        m_annotationMap[annotationGroup] = annotation;
        return;
    }
}

bool AstVisitor::VisitVarDecl(clang::VarDecl* decl)
{
    auto* initializer = stripExpr(decl->getAnyInitializer());
    const CXXConstructExpr* expr = dyn_cast_or_null<CXXConstructExpr>(initializer);

    if (expr && decl->getName().startswith("_frem_rpc_arg_")) {
        handleFremAnnotationVariable(decl, expr);
        return true;
    }

    if (!expr)
        return true;

    if (expr->getNumArgs() == 1) {
        // Handle frem::RpcResultDecl instantiation.
        auto typeName = getFullyQualifiedNameOfType(decl->getType().getUnqualifiedType(), m_context);
        if (typeName == "frem::RpcResultDecl") {
            Expr::EvalResult value;
            if (!expr->getArg(0)->EvaluateAsInt(value, m_context) || !value.Val.isInt())
                return true;
            auto intValue = value.Val.getInt().getExtValue();
            if (intValue != int(intValue))
                return true;
            registerReturnValue(int(intValue), decl->getNameAsString());

            return true;
        }

        // Handle frem::TypeAlias<> instantiation.
        if (auto* templ = decl->getType()->getAs<TemplateSpecializationType>())
            if (TemplateDecl* typeTemplateDecl = templ->getTemplateName().getAsTemplateDecl())
                if (typeTemplateDecl->getName() == "TypeAlias"
                    && isInToplevelNamespace(typeTemplateDecl, "frem")) {
                    auto aliasLoc = m_context.getSourceManager().getFileLoc(decl->getLocation());
                    auto originalType = templ->getArg(0).getAsType().getUnqualifiedType();
                    auto aliasName = evaluateStringArgument(expr->getArg(0),
                                                            expr->getArg(0)->getExprLoc());
                    if (!aliasName) {
                        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                                   "alias name must be a string");
                        m_diagEngine.Report(aliasLoc, id);
                        return true;
                    }

                    auto registeredType = registerType(aliasLoc,
                                                       originalType,
                                                       std::set<std::string>());
                    if (!registeredType)
                        return true;
                    m_typeRegistry.setTypeAlias(
                        getFullyQualifiedNameOfType(originalType, m_context),
                        *aliasName,
                        FileLocation{m_context.getSourceManager().getFilename(aliasLoc).str(),
                                     m_context.getSourceManager().getSpellingLineNumber(aliasLoc)});
                    return true;
                }
    }

    if (expr->getNumArgs() >= 2) {
        // Handle nsp::DiagnosticDescriptor and nsp::ErrorDescriptor instances.
        auto typeName = getFullyQualifiedNameOfType(decl->getType().getUnqualifiedType(), m_context);
        if (typeName == "nsp::DiagnosticDescriptor" || typeName == "nsp::ErrorDescriptor") {
            ErrorDescriptor desc;

            auto id = evaluateStringArgument(expr->getArg(0), expr->getArg(0)->getExprLoc());
            if (id) {
                desc.id = *id;
            }
            else {
                Expr::EvalResult result;
                bool worked = expr->getArg(0)->EvaluateAsRValue(result, m_context);
                if (worked && result.Val.isInt()) {
                    desc.value = result.Val.getInt().getExtValue();
                }
                else {
                    unsigned diagId = m_diagEngine
                                          .getCustomDiagID(DiagnosticsEngine::Error,
                                                           "id must be integer or string literal");
                    m_diagEngine.Report(expr->getArg(0)->getExprLoc(), diagId);
                    return true;
                }
            }

            auto description = evaluateStringArgument(expr->getArg(1),
                                                      expr->getArg(1)->getExprLoc());
            if (!description) {
                unsigned diagId = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                               "description must be a string");
                m_diagEngine.Report(expr->getArg(1)->getExprLoc(), diagId);
                return true;
            }
            desc.description = *description;

            if (expr->getNumArgs() >= 3) {
                auto service = evaluateStringArgument(expr->getArg(2),
                                                      expr->getArg(2)->getExprLoc());
                if (service)
                    desc.serviceText = *service;
            }

            if (expr->getNumArgs() >= 4) {
                auto user = evaluateStringArgument(expr->getArg(3), expr->getArg(3)->getExprLoc());
                if (user)
                    desc.userText = *user;
            }

            if (expr->getNumArgs() >= 5) {
                auto comment = evaluateStringArgument(expr->getArg(4),
                                                      expr->getArg(4)->getExprLoc());
                if (comment)
                    desc.comment = *comment;
            }

            m_parseResult.errorDescriptors.push_back(desc);
            return true;
        }
    }

    // Handle frem::ConfigurationDeclarator<> instantiation.
    if (auto* templ = decl->getType()->getAs<TemplateSpecializationType>()) {
        if (TemplateDecl* typeTemplateDecl = templ->getTemplateName().getAsTemplateDecl()) {
            if (typeTemplateDecl->getName() == "ConfigurationDeclarator"
                && isInToplevelNamespace(typeTemplateDecl, "frem")) {
                Configuration config;

                for (auto templateArgument : templ->template_arguments()) {
                    auto templateArgType = templateArgument.getAsType().getUnqualifiedType();
                    auto type = registerType(decl->getBeginLoc(),
                                             templateArgType,
                                             std::set<std::string>());
                    if (type)
                        config.versionTypes.push_back(
                            TypeRefWithVersion{type, type->configurationVersion});
                }

                auto name = evaluateStringArgument(expr->getArg(0), expr->getArg(0)->getExprLoc());
                if (!name) {
                    unsigned diagId = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                                   "alias must be a string");
                    m_diagEngine.Report(expr->getArg(0)->getExprLoc(), diagId);
                    return true;
                }
                config.id = *name;

                auto cfgLocation = expr->getLocation();
                config.fileName = m_context.getSourceManager().getFilename(cfgLocation).str();
                config.line = m_context.getSourceManager().getSpellingLineNumber(cfgLocation);

                // If the configuration is already registered, do not register it again.
                for (const auto& existing : m_parseResult.configurations)
                    if (existing.id == config.id)
                        return true;

                for (unsigned argument = 1; argument < expr->getNumArgs(); ++argument) {
                    auto arg = dyn_cast_or_null<CXXConstructExpr>(stripExpr(expr->getArg(argument)));
                    if (!arg)
                        continue;

                    // cout << "arg " << argument << "  " << arg->getConstructor()->getNameAsString() << endl;
                    // cout << "arg " << argument << "  " << "#args: " << arg->getNumArgs() << endl;
                    if (arg->getConstructor()->getNameAsString() == "SetCode") {
                        if (auto val = toInteger(arg); val)
                            config.setCode = val->getExtValue();
                    }
                    else if (arg->getConstructor()->getNameAsString() == "GetCode") {
                        if (auto val = toInteger(arg); val)
                            config.getCode = val->getExtValue();
                    }
                    else if (arg->getConstructor()->getNameAsString() == "VersionCode") {
                        if (auto val = toInteger(arg); val)
                            config.versionCode = val->getExtValue();
                    }
                    else if (arg->getConstructor()->getNameAsString() == "Tags"
                             && arg->getNumArgs() > 0) {
                        auto tags = getTags(m_context.getSourceManager().getFileLoc(
                                                arg->getLocation()),
                                            arg);
                        if (tags)
                            config.tags = *tags;
                    }
                }

                m_parseResult.configurations.push_back(config);
                return true;
            }
        }
    }

    // Handle frem::DatagramSocketDeclarator<> instances.
    if (auto* templ = decl->getType()->getAs<TemplateSpecializationType>()) {
        if (TemplateDecl* typeTemplateDecl = templ->getTemplateName().getAsTemplateDecl()) {
            if (typeTemplateDecl->getName() == "DatagramSocketDeclarator"
                && isInToplevelNamespace(typeTemplateDecl, "frem")) {
                Socket socket;

                for (auto templateArgument : templ->template_arguments()) {
                    auto templateArgType = templateArgument.getAsType().getUnqualifiedType();
                    auto type = registerType(decl->getBeginLoc(),
                                             templateArgType,
                                             std::set<std::string>());
                    if (type)
                        socket.packetType = type;
                    // TODO: add a diagnostic if type registration failed
                }

                Expr::EvalResult result;
                bool worked = expr->getArg(1)->EvaluateAsRValue(result, m_context);
                if (worked && result.Val.isInt()) {
                    socket.port = result.Val.getInt().getExtValue();
                }
                else {
                    unsigned diagId = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                                   "port must be an integer literal");
                    m_diagEngine.Report(expr->getArg(1)->getExprLoc(), diagId);
                    return true;
                }

                auto name = evaluateStringArgument(expr->getArg(0), expr->getArg(0)->getExprLoc());
                if (!name) {
                    unsigned diagId = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                                   "id must be a string");
                    m_diagEngine.Report(expr->getArg(0)->getExprLoc(), diagId);
                    return true;
                }
                socket.id = *name;

                auto location = expr->getLocation();
                socket.fileName = m_context.getSourceManager().getFilename(location).str();
                socket.line = m_context.getSourceManager().getSpellingLineNumber(location);

                for (unsigned argument = 2; argument < expr->getNumArgs(); ++argument) {
                    auto arg = dyn_cast_or_null<CXXConstructExpr>(stripExpr(expr->getArg(argument)));
                    if (!arg)
                        continue;

                    if (arg->getConstructor()->getNameAsString() == "Tags"
                        && arg->getNumArgs() > 0) {
                        auto tags = getTags(m_context.getSourceManager().getFileLoc(
                                                arg->getLocation()),
                                            arg);
                        if (tags)
                            socket.tags = *tags;
                    }
                }

                m_parseResult.sockets.push_back(socket);
                return true;
            }
        }
    }

    return true;
}

static bool matchesAPUserDefinedClassInStd(const std::string& name)
{
    return name == "threadex" || name == "thread_accessor";
}

static bool matchesAPUserDefinedFunctionInStd(const std::string& name)
{
    static const std::set<std::string> excludeSet({"clear_signals",
                                                   "current_stack_usage",
                                                   "get_priority",
                                                   "max_stack_usage",
                                                   "set_priority",
                                                   "sleep_for_busy",
                                                   "try_wait_for_all_signals",
                                                   "try_wait_for_all_signals_for",
                                                   "try_wait_for_all_signals_until",
                                                   "try_wait_for_any_signal",
                                                   "try_wait_for_any_signal_for",
                                                   "try_wait_for_any_signal_until",
                                                   "wait_for_all_signals"});
    return excludeSet.find(name) != excludeSet.end();
}

bool AstVisitor::VisitFunctionDecl(clang::FunctionDecl* fun)
{
    if (isInToplevelNamespace(fun, "std")) {
        if (matchesAPUserDefinedFunctionInStd(fun->getDeclName().getAsString())) {
            unsigned id = m_diagEngine.getCustomDiagID(
                DiagnosticsEngine::Warning, "declaration of user-defined function in std namespace");
            m_diagEngine.Report(fun->getLocation(), id);
        }
    }

    // Skip methods which are part of a class template declaration. We do
    // not skip methods belonging to a class template specialization.
    // [http://clang-developers.42468.n3.nabble.com/Questions-about-CXXRecordDecl-td2678651.html]
    auto* method = dyn_cast<CXXMethodDecl>(fun);
    if (method) {
        CXXRecordDecl* classDecl = method->getParent();

        // Try to get the class template to which this CXX record decl belongs.
        // If this is non-null, the method is part of a template.
        if (classDecl->getDescribedClassTemplate()) {
            return true;
        }

        if (const ClassTemplatePartialSpecializationDecl* partialSpecializationDecl
            = dyn_cast<ClassTemplatePartialSpecializationDecl>(classDecl)) {
            // If this is a partial specialization, try to get a pointer to
            // the partially-specialized class template. If the pointer is
            // null, we are in a template.
            if (!partialSpecializationDecl->getInstantiatedFrom()) {
                return true;
            }
        }
    }

    bool isRpcFunction = false;
    SourceLocation functionLocation;
    RpcFunction function;
    for (auto attrIter = fun->specific_attr_begin<AnnotateAttr>();
         attrIter != fun->specific_attr_end<AnnotateAttr>();
         ++attrIter) {
        const AnnotateAttr* attr = *attrIter;
        if (attr->getAnnotation().startswith("_frem_rpc:")) {
            // Only consider a function if we are in the same file as the annotation has been placed.
            auto annotationLocation = m_context.getSourceManager().getFileLoc(attr->getLocation());
            functionLocation = m_context.getSourceManager().getFileLoc(fun->getLocation());
            if (m_context.getSourceManager().getFileID(annotationLocation)
                != m_context.getSourceManager().getFileID(functionLocation))
                continue;

            std::istringstream ss(attr->getAnnotation().substr(10).str());
            int annotationGroup;
            ss >> annotationGroup;
            auto iter = m_annotationMap.find(annotationGroup);
            if (iter == m_annotationMap.end()) {
                unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                           "missing annotation");
                m_diagEngine.Report(functionLocation, id);
                return true;
            }
            function.annotation = iter->second;

            isRpcFunction = true;
            break;
        }
    }

    if (!isRpcFunction) {
        //unsigned id = m_diagEngine.getCustomDiagID(
        //    DiagnosticsEngine::Error,
        //    "function " << fun->getDeclName().getAsString() << " has been skipped");
        //m_diagEngine.Report(method->getParent()->getBeginLoc(), id);
        return true;
    }

    function.fileName = m_context.getSourceManager().getFilename(functionLocation).str();
    function.line = m_context.getSourceManager().getSpellingLineNumber(functionLocation);

    // Determine the kind of the function (free, static, member).
    if (method) {
        function.fullyQualifiedName = getFullyQualifiedNameOfType(m_context.getRecordType(
                                                                      method->getParent()),
                                                                  m_context)
                                      + "::" + fun->getDeclName().getAsString();

        if (method->isStatic()) {
            function.kind = RpcFunction::StaticFunction;
        }
        else {
            function.kind = RpcFunction::MemberFunction;
            // Determine how to get a pointer/reference to an instance of the class to
            // which this member function belongs to.
            auto invokee = findClassInstanceGetter(method->getParent());
            if (invokee) {
                function.invokee = *invokee;
                if (method->isConst())
                    function.qualifiers.push_back("const");
            }
            else {
                unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                           "class has no instance getter");
                m_diagEngine.Report(method->getParent()->getBeginLoc(), id);
                return true;
            }
        }
    }
    else {
        function.kind = RpcFunction::FreeFunction;
        function.fullyQualifiedName = getNamespaces(fun->getDeclContext())
                                      + fun->getDeclName().getAsString();
    }

    if (m_parseResult.processedFunctions.find(function.fullyQualifiedName)
        != m_parseResult.processedFunctions.end()) {
        return true;
    }
    m_parseResult.processedFunctions.insert(function.fullyQualifiedName);

    auto returnType = getReturnType(fun->getReturnTypeSourceRange().getBegin(),
                                    fun->getReturnType());
    if (!returnType)
        return true;
    function.returnType = *returnType;

    for (const auto* paramIter : fun->parameters()) {
        auto parameter = getParameter(paramIter->getBeginLoc(), paramIter->getType());
        if (!parameter)
            return true;

        parameter->name = paramIter->getNameAsString();
        function.parameters.push_back(*parameter);
    }

    function.isNoexcept = fun->getType()->getAs<FunctionProtoType>()->canThrow()
                          == CanThrowResult::CT_Cannot;

    const auto* comment = m_context.getRawCommentForDeclNoCache(fun);
    if (comment)
        function.docString = comment->getRawText(m_context.getSourceManager()).str();

    // The ID of a function is either its fully qualified name or the alias.
    function.id = function.fullyQualifiedName;
    if (!function.annotation.alias.empty())
        function.id = function.annotation.alias;

    if (m_writeRpcs)
        m_parseResult.rpcFunctions.push_back(function);

    return true;
}

bool AstVisitor::VisitCXXRecordDecl(CXXRecordDecl* decl)
{
    if (isInToplevelNamespace(decl, "std")) {
        if (matchesAPUserDefinedClassInStd(decl->getDeclName().getAsString())) {
            unsigned id = m_diagEngine.getCustomDiagID(
                DiagnosticsEngine::Warning, "declaration of user-defined record in std namespace");
            m_diagEngine.Report(decl->getLocation(), id);
        }
    }
    return true;
}

void AstVisitor::registerReturnValue(int value, const std::string& identifier)
{
    // TODO: Check for duplicates
    m_parseResult.returnValues[value] = identifier;
}

std::shared_ptr<InterfaceType> AstVisitor::registerType(clang::SourceLocation diagLoc,
                                                        clang::QualType type,
                                                        std::set<string> typeStack)
{
    // If the type is already registered, there is nothing to do.
    auto fullyQualifiedName = getFullyQualifiedNameOfType(type, m_context);
    if (auto registeredType = m_typeRegistry.lookup(fullyQualifiedName))
        return registeredType;

    // Push the current type on the stack of handled types. This way, we can
    // detect cycles and abort instead of running into an endless loop.
    auto result = typeStack.insert(fullyQualifiedName);
    if (!result.second) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                   "detected a cycle in the type system");
        m_diagEngine.Report(diagLoc, id);
        return nullptr;
    }

    // Handle special template instances, e.g. std::array<>, frem::Array<>, std::future<>.
    if (auto* templ = type->getAs<TemplateSpecializationType>())
        if (TemplateDecl* decl = templ->getTemplateName().getAsTemplateDecl()) {
            // Handle std::array<T, N>
            if (decl->getName() == "array" && isInToplevelNamespace(decl, "std")) {
                if (templ->getNumArgs() != 2) {
                    unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                               "expected 2 template arguments");
                    m_diagEngine.Report(diagLoc, id);
                    return nullptr;
                }

                auto interfaceType = make_shared<InterfaceType>();
                interfaceType->kind = InterfaceType::Kind::FixedArray;

                // Register the element type first.
                auto elementType = templ->getArg(0).getAsType().getUnqualifiedType();
                interfaceType->elementType = registerType(decl->getBeginLoc(),
                                                          elementType,
                                                          typeStack);
                if (!interfaceType->elementType)
                    return nullptr;

                std::optional<llvm::APSInt> size = toInteger(templ->getArg(1));
                if (!size)
                    return nullptr;

                interfaceType->minSize = interfaceType->maxSize = size->getExtValue();

                return interfaceType;
            }

            // Handle frem::BoundedArray<T, MIN, MAX>
            if (decl->getName() == "BoundedArray" && isInToplevelNamespace(decl, "frem")) {
                if (templ->getNumArgs() != 3) {
                    unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                               "expected 3 template arguments");
                    m_diagEngine.Report(diagLoc, id);

                    return nullptr;
                }

                auto interfaceType = make_shared<InterfaceType>();

                // Register the element type first.
                auto elementType = templ->getArg(0).getAsType().getUnqualifiedType();
                interfaceType->elementType = registerType(decl->getBeginLoc(),
                                                          elementType,
                                                          typeStack);
                if (!interfaceType->elementType)
                    return nullptr;

                interfaceType->kind = InterfaceType::Kind::BoundedArray;

                std::optional<llvm::APSInt> minSize = toInteger(templ->getArg(1));
                std::optional<llvm::APSInt> maxSize = toInteger(templ->getArg(2));
                if (!minSize || !maxSize) {
                    unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                               "could not determine array bounds");
                    m_diagEngine.Report(diagLoc, id);
                    return nullptr;
                }

                interfaceType->minSize = minSize->getExtValue();
                interfaceType->maxSize = maxSize->getExtValue();

                // For frem::BoundedArray the size-type is fixed to uint16_t
                interfaceType->sizeType = make_shared<InterfaceType>();
                interfaceType->sizeType->kind = InterfaceType::Kind::BuiltIn;
                interfaceType->sizeType->id = "uint16_t";
                interfaceType->sizeType->fullyQualifiedName = "uint16_t";

                return interfaceType;
            }

            // Handle frem::FixedBasicString<T, N>
            if (decl->getName() == "FixedBasicString" && isInToplevelNamespace(decl, "frem")) {
                if (templ->getNumArgs() != 2) {
                    unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                               "expected 2 template arguments");
                    m_diagEngine.Report(diagLoc, id);
                    return nullptr;
                }

                auto interfaceType = make_shared<InterfaceType>();
                interfaceType->kind = InterfaceType::Kind::FixedString;

                // Register the element type first.
                auto elementType = templ->getArg(0).getAsType().getUnqualifiedType();
                interfaceType->elementType = registerType(decl->getBeginLoc(),
                                                          elementType,
                                                          typeStack);
                if (!interfaceType->elementType)
                    return nullptr;

                std::optional<llvm::APSInt> size = toInteger(templ->getArg(1));
                if (!size)
                    return nullptr;

                interfaceType->minSize = interfaceType->maxSize = size->getExtValue();

                return interfaceType;
            }

#if 0
            // Handle frem::FixedAsciiString<N>
            if (decl->getName() == "FixedAsciiString" && isInToplevelNamespace(decl, "frem")) {
                if (templ->getNumArgs() != 1) {
                    unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                               "expected 1 template argument");
                    m_diagEngine.Report(diagLoc, id);
                    return nullptr;
                }

                std::cout << "Juhu" << std::endl;

                return nullptr;
            }
#endif

            // Handle frem::BoundedBasicString<T, MIN, MAX>
            if (decl->getName() == "BoundedBasicString" && isInToplevelNamespace(decl, "frem")) {
                if (templ->getNumArgs() != 3) {
                    unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                               "expected 3 template arguments");
                    m_diagEngine.Report(diagLoc, id);
                    return nullptr;
                }

                auto interfaceType = make_shared<InterfaceType>();

                // Register the element type first.
                auto elementType = templ->getArg(0).getAsType().getUnqualifiedType();
                interfaceType->elementType = registerType(decl->getBeginLoc(),
                                                          elementType,
                                                          typeStack);
                if (!interfaceType->elementType)
                    return nullptr;

                interfaceType->kind = InterfaceType::Kind::BoundedString;

                std::optional<llvm::APSInt> minSize = toInteger(templ->getArg(1));
                std::optional<llvm::APSInt> maxSize = toInteger(templ->getArg(2));
                if (!minSize || !maxSize) {
                    unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                               "could not determine string bounds");
                    m_diagEngine.Report(diagLoc, id);
                    return nullptr;
                }

                interfaceType->minSize = minSize->getExtValue();
                interfaceType->maxSize = maxSize->getExtValue();

                // For frem::BoundedString the size-type is fixed to uint16_t
                interfaceType->sizeType = make_shared<InterfaceType>();
                interfaceType->sizeType->kind = InterfaceType::Kind::BuiltIn;
                interfaceType->sizeType->id = "uint16_t";
                interfaceType->sizeType->fullyQualifiedName = "uint16_t";

                return interfaceType;
            }

            // Handle frem::Array<T, Fixed<N>> and frem::Array<T, Bounded<U, MIN, MAX>>
            if (decl->getName() == "Array" && isInToplevelNamespace(decl, "frem")) {
                if (templ->getNumArgs() != 2)
                    return nullptr;

                auto interfaceType = make_shared<InterfaceType>();

                // Register the element type first.
                auto elementType = templ->getArg(0).getAsType().getUnqualifiedType();
                interfaceType->elementType = registerType(decl->getBeginLoc(),
                                                          elementType,
                                                          typeStack);
                if (!interfaceType->elementType)
                    return nullptr;

                auto sizePolicyArg = templ->getArg(1).getAsType().getUnqualifiedType();
                auto* policyTemplate = sizePolicyArg->getAs<TemplateSpecializationType>();
                if (!policyTemplate)
                    return nullptr;
                TemplateDecl* policyDecl = policyTemplate->getTemplateName().getAsTemplateDecl();
                if (!policyDecl && !isInToplevelNamespace(decl, "frem"))
                    return nullptr;

                if (policyDecl->getName() == "Fixed") {
                    interfaceType->kind = InterfaceType::Kind::FixedArray;
                    if (policyTemplate->getNumArgs() != 1)
                        return nullptr;

                    std::optional<llvm::APSInt> size = toInteger(policyTemplate->getArg(0));
                    if (!size)
                        return nullptr;

                    interfaceType->minSize = interfaceType->maxSize = size->getExtValue();
                }
                else if (policyDecl->getName() == "Bounded") {
                    interfaceType->kind = InterfaceType::Kind::BoundedArray;
                    if (policyTemplate->getNumArgs() != 2)
                        return nullptr;

                    std::optional<llvm::APSInt> minSize = toInteger(policyTemplate->getArg(0));
                    std::optional<llvm::APSInt> maxSize = toInteger(policyTemplate->getArg(1));
                    if (!minSize || !maxSize)
                        return nullptr;

                    interfaceType->minSize = minSize->getExtValue();
                    interfaceType->maxSize = maxSize->getExtValue();

                    // TODO: fix this; it does not work for default template arguments...
                    interfaceType->sizeType = make_shared<InterfaceType>();
                    interfaceType->sizeType->kind = InterfaceType::Kind::BuiltIn;
                    interfaceType->sizeType->id = "uint16_t";
                    interfaceType->sizeType->fullyQualifiedName = "uint16_t";
                    //interfaceType->sizeType = registerType(
                    //                              policyTemplate->getArg(2).getAsType().getUnqualifiedType(),
                    //                              std::set<std::string>());
                    //if (!interfaceType->sizeType)
                    //    return nullptr;
                }

                return interfaceType;
            }

            // Handle std::future<T>, std::shared_future<T>, nsp::Future<T>, nsp::SharedFuture<T>
            if ((decl->getName() == "future" && isInToplevelNamespace(decl, "std"))
                || (decl->getName() == "shared_future" && isInToplevelNamespace(decl, "std"))
                || (decl->getName() == "Future" && isInToplevelNamespace(decl, "nsp"))
                || (decl->getName() == "SharedFuture" && isInToplevelNamespace(decl, "nsp"))) {
                if (templ->getNumArgs() < 1)
                    return nullptr;

                auto interfaceType = make_shared<InterfaceType>();
                interfaceType->kind = InterfaceType::Kind::Future;

                // Register the underlying type first.
                auto underlyingType = templ->getArg(0).getAsType().getUnqualifiedType();
                interfaceType->underlyingType = registerType(clang::SourceLocation(),
                                                             underlyingType,
                                                             typeStack);
                if (!interfaceType->underlyingType)
                    return nullptr;

                return interfaceType;
            }

            // Handle std::optional<T>
            if (decl->getName() == "optional" && isInToplevelNamespace(decl, "std")) {
                if (templ->getNumArgs() < 1)
                    return nullptr;

                auto interfaceType = make_shared<InterfaceType>();
                interfaceType->kind = InterfaceType::Kind::Optional;

                // Register the underlying type first.
                auto underlyingType = templ->getArg(0).getAsType().getUnqualifiedType();
                interfaceType->underlyingType = registerType(clang::SourceLocation(),
                                                             underlyingType,
                                                             typeStack);
                if (!interfaceType->underlyingType)
                    return nullptr;

                return interfaceType;
            }

            // Handle std::variant<T1, T2, ...>
            if (decl->getName() == "variant" && isInToplevelNamespace(decl, "std")) {
                if (templ->getNumArgs() < 1)
                    return nullptr;

                auto interfaceType = make_shared<InterfaceType>();
                interfaceType->kind = InterfaceType::Kind::Variant;

                // Register the underlying types first.
                // TODO: Could filter out std::monostate and mark the variant as potentially empty
                for (unsigned count = 0; count < templ->getNumArgs(); ++count) {
                    auto templateArgType = templ->getArg(count).getAsType().getUnqualifiedType();
                    auto underlyingType = registerType(clang::SourceLocation(),
                                                       templateArgType,
                                                       typeStack);
                    if (!underlyingType)
                        return nullptr;
                    interfaceType->underlyingTypesList.push_back(underlyingType);
                }

                return interfaceType;
            }
        }

    // Handle struct/class/union.
    if (auto* record = type->getAsCXXRecordDecl())
        return registerRecord(type, typeStack);

    // Handle enums.
    if (auto* enumDecl = dyn_cast_or_null<EnumDecl>(type->getAsTagDecl())) {
        auto interfaceType = make_shared<InterfaceType>();
        interfaceType->kind = InterfaceType::Kind::Enum;
        interfaceType->id = fullyQualifiedName;
        interfaceType->fullyQualifiedName = fullyQualifiedName;
        auto enumLoc = enumDecl->getLocation();
        interfaceType->declarationLocation
            = FileLocation{m_context.getSourceManager().getFilename(enumLoc).str(),
                           m_context.getSourceManager().getSpellingLineNumber(enumLoc)};
        interfaceType->underlyingType = registerType(enumLoc,
                                                     enumDecl->getIntegerType().getUnqualifiedType(),
                                                     std::set<std::string>());
        if (!interfaceType->underlyingType) {
            unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Note,
                                                       "add ': std::uint16_t' for example");
            m_diagEngine.Report(enumLoc, id);
            return nullptr;
        }

        for (auto* constant : enumDecl->enumerators()) {
            EnumConstant ec;
            ec.fieldName = constant->getNameAsString();
            ec.value = constant->getInitVal().getExtValue();

            interfaceType->enumConstants.push_back(ec);
        }

        m_typeRegistry.registerType(fullyQualifiedName, interfaceType);
        return interfaceType;
    }

    // Report errors about non-serializable types.
    if (type->isIntegerType()) {
        unsigned id = m_diagEngine.getCustomDiagID(
            DiagnosticsEngine::Error, "integral type of undetermined size cannot be serialized");
        m_diagEngine.Report(diagLoc, id);
    }
    else if (type->isScalarType()) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                   "not a known built-in type");
        m_diagEngine.Report(diagLoc, id);
    }
    else {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                   "non-scalar type cannot be serialized");
        m_diagEngine.Report(diagLoc, id);
    }

    // The type is neither a struct/class nor an enum.
    return nullptr;
}

std::shared_ptr<InterfaceType> AstVisitor::registerRecord(clang::QualType type,
                                                          std::set<std::string> typeStack)
{
    assert(type->getAsCXXRecordDecl() != nullptr && "Not a CXXRecordDecl");
    const clang::CXXRecordDecl* record = type->getAsCXXRecordDecl();
    if (!record->hasDefinition()) {
        if (auto* templ = type->getAs<TemplateSpecializationType>())
            if (TemplateDecl* decl = templ->getTemplateName().getAsTemplateDecl())
                record = dyn_cast_or_null<CXXRecordDecl>(decl->getTemplatedDecl());
    }

    if (!record || !record->hasDefinition())
        return nullptr;

    // Allow structs and classes but do not allow unions.
    if (record->getTagKind() != TTK_Struct && record->getTagKind() != TTK_Class)
        return nullptr;
    // Structures should be trivially copyable to participate in an RPC.
    if (!record->isTriviallyCopyable()) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Warning,
                                                   "record is not trivially copyable");
        m_diagEngine.Report(record->getLocation(), id);
    }

    // Structs used in RPCs must be non-empty.
    if (record->isEmpty()) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error, "record is empty");
        m_diagEngine.Report(record->getLocation(), id);
    }

    auto fullyQualifiedName = getFullyQualifiedNameOfType(type, m_context);
    auto interfaceType = make_shared<InterfaceType>();
    interfaceType->kind = InterfaceType::Kind::Struct;
    interfaceType->id = fullyQualifiedName;
    interfaceType->fullyQualifiedName = fullyQualifiedName;
    auto recordLoc = record->getLocation();
    interfaceType->declarationLocation
        = FileLocation{m_context.getSourceManager().getFilename(recordLoc).str(),
                       m_context.getSourceManager().getSpellingLineNumber(recordLoc)};

    // Traverse the base classes before the fields of the struct.
    // TODO: Bases should be visited recursively, not just one level.
    for (const CXXBaseSpecifier& base : record->bases()) {
        const CXXRecordDecl* baseDecl = base.getType()->getAsCXXRecordDecl();
        if (!baseDecl) {
            if (auto* tst = base.getType()->getAs<TemplateSpecializationType>())
                if (TemplateDecl* typeTemplateDecl = tst->getTemplateName().getAsTemplateDecl())
                    baseDecl = dyn_cast_or_null<CXXRecordDecl>(typeTemplateDecl->getTemplatedDecl());
        }
        if (!baseDecl)
            continue;

        // TODO: I think, this is already obsolete because we store the version of a configuration
        // in the configuration declaration and not the struct type itself.
        if (baseDecl->getName() == "ConfigurationVersion"
            && isInToplevelNamespace(baseDecl, "frem")) {
            if (auto* templ = base.getType()->getAs<TemplateSpecializationType>()) {
                std::optional<llvm::APSInt> version;
                if (templ->getNumArgs() > 0)
                    version = toInteger(templ->getArg(0));
                if (version)
                    interfaceType->configurationVersion = version->getExtValue();
            }
        }

        for (auto field : baseDecl->fields()) {
            StructFieldData fd;
            fd.name = field->getNameAsString();
            fd.type = registerType(field->getBeginLoc(), field->getType(), typeStack);
            if (!fd.type)
                return nullptr;

            interfaceType->structFields.push_back(fd);
        }
    }

    // Recursively register the fields of a struct.
    for (auto field : record->fields()) {
        StructFieldData fd;
        fd.name = field->getNameAsString();
        fd.type = registerType(field->getBeginLoc(), field->getType(), typeStack);
        if (!fd.type)
            return nullptr;

        interfaceType->structFields.push_back(fd);
    }

    m_typeRegistry.registerType(fullyQualifiedName, interfaceType);
    return interfaceType;
}

std::optional<llvm::APSInt> AstVisitor::toInteger(const clang::TemplateArgument& arg)
{
    switch (arg.getKind()) {
    case TemplateArgument::Integral:
        return arg.getAsIntegral();

    case TemplateArgument::Expression: {
        Expr::EvalResult result;
        if (arg.getAsExpr()->EvaluateAsInt(result, m_context) && result.Val.isInt())
            return result.Val.getInt();
        else
            return {};
    }

    default:
        return {};
    }
}

std::optional<llvm::APSInt> AstVisitor::toInteger(const CXXConstructExpr* expr)
{
    Expr::EvalResult result;
    bool worked = expr->EvaluateAsRValue(result, m_context);
    if (worked && result.Val.isStruct()) {
        auto field = result.Val.getStructField(0);
        if (field.isInt())
            return field.getInt();
    }
    return {};
};

std::optional<Parameter> AstVisitor::getParameter(clang::SourceLocation diagLoc,
                                                  clang::QualType type)
{
    Parameter parameter;
    // Remember the type as written in the function's signature.
    parameter.fullyQualifiedType = getFullyQualifiedNameOfType(type, m_context);
    parameter.direction = Parameter::Input;

    if (auto* refType = type->getAs<LValueReferenceType>()) {
        type = refType->getPointeeType();
        if (!type.isConstQualified())
            parameter.direction = Parameter::Output;
    }
    if (type->isPointerType()) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                   "cannot serialize a pointer type");
        m_diagEngine.Report(diagLoc, id);
        return {};
    }

    // Decay the type.
    type = type.getUnqualifiedType();
    parameter.decayedType = getFullyQualifiedNameOfType(type, m_context);

    parameter.interfaceType = registerType(diagLoc, type, std::set<std::string>());
    if (!parameter.interfaceType) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Note, "from this parameter");
        m_diagEngine.Report(diagLoc, id);
        return {};
    }

    return parameter;
}

std::optional<ReturnType> AstVisitor::getReturnType(clang::SourceLocation diagLoc,
                                                    clang::QualType type)
{
    ReturnType retType;
    // Remember the type as written in the function's signature.
    retType.fullyQualifiedType = getFullyQualifiedNameOfType(type, m_context);

    if (auto refType = type->getAs<LValueReferenceType>()) {
        type = refType->getPointeeType();
    }

    type = type.getUnqualifiedType();
    retType.decayedType = getFullyQualifiedNameOfType(type, m_context);

    retType.interfaceType = registerType(diagLoc, type, std::set<std::string>());
    if (!retType.interfaceType) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                   "return type is not serializable");
        m_diagEngine.Report(diagLoc, id);
        return {};
    }

    return retType;
}

auto AstVisitor::findClassInstanceGetter(clang::CXXRecordDecl* record)
    -> llvm::Optional<RpcFunction::Invokee>
{
    // If derived from frem::RpcService:
    //     instance getter is: 'Class::m_fremSelf.load()'
    // If a function matches the signature "static Class& someName()":
    //     instance getter is: 'Class::someName()'
    // If a function matches the signature "static Class* someName()":
    //     instance getter is: 'Class::someName()'

    auto* canonical = record->getCanonicalDecl();

    auto qualifiedClassName = getFullyQualifiedNameOfType(m_context.getRecordType(canonical),
                                                          m_context);

    for (const auto& base : record->bases()) {
        QualType baseType = base.getType();
        const CXXRecordDecl* baseDecl = baseType->getAsCXXRecordDecl();
        if (!baseDecl)
            continue;

        if (baseDecl->getName() == "RpcService" && isInToplevelNamespace(baseDecl, "frem")) {
            RpcFunction::Invokee invokee;
            invokee.expression = qualifiedClassName + "::m_fremSelf.load()";
            invokee.isPointer = true;
            return invokee;
        }
    }

    for (auto* method : record->methods()) {
        if (!method->isStatic())
            continue;

        // If the method has parameters, skip it.
        bool hasParameter = false;
        for (const ParmVarDecl* param : method->parameters()) {
            if (!param->hasDefaultArg()) {
                hasParameter = true;
                break;
            }
        }
        if (hasParameter)
            continue;

        // Get the return type of the method. It has to be either a reference
        // type or a pointer type or the method is rejected.
        auto returnType = method->getReturnType().getCanonicalType().getTypePtr();
        RpcFunction::Invokee invokee;
        const clang::Type* pointeeType = nullptr;
        if (auto* refType = returnType->getAs<LValueReferenceType>()) {
            pointeeType = refType->getPointeeType().getCanonicalType().getTypePtr();
            invokee.isPointer = false;
        }
        else if (auto* ptrType = returnType->getAs<clang::PointerType>()) {
            pointeeType = ptrType->getPointeeType().getCanonicalType().getTypePtr();
            invokee.isPointer = true;
        }
        else {
            continue;
        }

        auto* returnRecord = pointeeType->getAsCXXRecordDecl();
        if (returnRecord && returnRecord->getCanonicalDecl() == canonical) {
            invokee.expression = qualifiedClassName + "::" + method->getNameAsString() + "()";
            return invokee;
        }
    }

    return llvm::Optional<RpcFunction::Invokee>();
}
