
#include "fremgen/AstConsumer.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/MacroBuilder.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/MacroArgs.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <llvm/Support/Path.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

using namespace clang;
using namespace llvm;
using namespace std;

// ----=====================================================================----
//     MacroCallbacks
// ----=====================================================================----

class MacroCallbacks : public clang::PPCallbacks
{
public:
    explicit MacroCallbacks(clang::Preprocessor& pp,
                            clang::ASTContext& context,
                            clang::DiagnosticsEngine& diagEngine)
        : m_preprocessor(pp)
        , m_context(context)
        , m_diagEngine(diagEngine)
        , m_annotationGroup(0)
        , m_varNameCounter(0)
    {
    }

    virtual void MacroExpands(const clang::Token& macroNameToken,
                              const clang::MacroDefinition& definition,
                              clang::SourceRange range,
                              const clang::MacroArgs* args) override;

private:
    clang::Preprocessor& m_preprocessor;
    clang::ASTContext& m_context;
    clang::DiagnosticsEngine& m_diagEngine;
    // A macro invocation such as `FREM_RPC(Code(123), Alias("Foo"))` results in annotations belonging
    // to each other. They are grouped by the same annotation group.
    unsigned m_annotationGroup;
    // A counter incremented for every variable generated in order to avoid naming conflicts.
    unsigned m_varNameCounter;

    void handleFremRpcAnnotation(const clang::Token& macroNameToken,
                                 const clang::MacroDefinition& definition,
                                 clang::SourceRange range,
                                 const clang::MacroArgs* args);

    void handleFremTypeAlias(const clang::Token& macroNameToken,
                             const clang::MacroDefinition& definition,
                             clang::SourceRange range,
                             const clang::MacroArgs* args);


    /// Creates a unique variable of the form
    ///     '_frem_var_' counter '_' annotationGroup '_' location,
    /// where `counter` is a unique number for every variable (to generate unique names),
    /// `annotationGroup` is the identifier of the current annotation group (to group
    /// annotations from the same macro invocation together) and `location` is the source
    /// location from which the variable was created (needed to generate diagnostic
    /// messages in the AST visitor if something is not correct).
    Token uniqueVariable(const char* prefix, clang::SourceLocation typeTokenLoc)
    {
        ++m_varNameCounter;
        std::stringstream ss;
        ss << prefix << "_" << m_varNameCounter << "_" << m_annotationGroup << "_"
           << typeTokenLoc.getRawEncoding();

        Token variableNameToken;
        variableNameToken.startToken();
        variableNameToken.setKind(tok::identifier);
        variableNameToken.setIdentifierInfo(m_preprocessor.getIdentifierInfo(ss.str()));
        return variableNameToken;
    }
};

void MacroCallbacks::MacroExpands(const clang::Token& macroNameToken,
                                  const clang::MacroDefinition& definition,
                                  clang::SourceRange range,
                                  const clang::MacroArgs* args)
{
    auto* identifierInfo = macroNameToken.getIdentifierInfo();
    if (!identifierInfo)
        return;

    // If we recognice a macro, that we have to intercept, start a new annotation group
    // and replace the macro by source code.

    if (identifierInfo->getName() == "FREM_RPC") {
        ++m_annotationGroup;
        return handleFremRpcAnnotation(macroNameToken, definition, range, args);
    }

    if (identifierInfo->getName() == "FREM_TYPE_ALIAS") {
        ++m_annotationGroup;
        return handleFremTypeAlias(macroNameToken, definition, range, args);
    }
}

void MacroCallbacks::handleFremRpcAnnotation(const clang::Token& macroNameToken,
                                             const clang::MacroDefinition& definition,
                                             clang::SourceRange range,
                                             const clang::MacroArgs* args)
{
    // Handle `FREM_RPC(...)`.

    // Okay, handling the macro arguments is a bit tricky. args->getNumArguments()
    // returns the number of tokens which have been passed to this macro invokation.
    // args->getUnexpArgument(i) gives a pointer to the first token for the
    // i-th *formal* argument. If the macro has only one variadic argument, there is
    // *only one* formal argument through which all user-arguments are passed.
    // Starting with a pointer to the first token (user-argument), we can simply
    // increment the pointer because all tokens are allocated one after the other.
    // tok::eof marks the end of a macro argument.

    // First split up the arguments of `FREM_RPC`. For example, we want to turn
    //     FREM_RPC(Code(123), Alias("Foo"))
    // into
    //     arguments = [Code(123), Alias("Foo")]
    // The pieces inside the `FREM_RPC` macro are actually constructor invocations.
    // So they are of the form `Code(...)` or `Alias{...}`, i.e. we expect an identifier
    // followed by an opening parenthesis or curly brace, followed by more code and
    // finally a closing parenthesis/brace.
    using Argument = SmallVector<const Token*, 8>;
    SmallVector<Argument, 4> arguments;
    const Token* iter = args->getUnexpArgument(0);
    for (;;) {
        if (iter->is(tok::eof))
            break;

        // Expect an identifier.
        if (!iter->isAnyIdentifier()) {
            m_preprocessor.Diag(*iter, diag::err_expected) << "identifier";
            return;
        }
        arguments.push_back(Argument());
        auto& currentArgument = arguments.back();
        currentArgument.push_back(iter++);

        // Advance to an opening parenthesis or brace.
        while (!iter->isOneOf(tok::l_paren, tok::l_brace, tok::eof)) {
            currentArgument.push_back(iter++);
        }
        if (iter->is(tok::eof))
            break;
        currentArgument.push_back(iter++);

        // Append all other tokens until the number of opening and closing parenthesis/braces
        // is balanced again. Note that we also allow that an `(` is matched with `}`. The
        // compiler will catch these mismatches.
        int numOpenParentheses = 1;
        while (iter->isNot(tok::eof)) {
            if (iter->isOneOf(tok::l_paren, tok::l_brace))
                ++numOpenParentheses;
            else if (iter->isOneOf(tok::r_paren, tok::r_brace))
                --numOpenParentheses;
            currentArgument.push_back(iter++);

            if (numOpenParentheses == 0) {
                // If the next token is a comma, skip over it and start parsing the next
                // macro argument. Otherwise this was the last argument.
                if (iter->is(tok::comma))
                    ++iter;
                break;
            }
        }
    }

    // The `FREM_RPC` macro must not be empty.
    if (arguments.empty()) {
        unsigned id = m_diagEngine.getCustomDiagID(DiagnosticsEngine::Error,
                                                   "annotation cannot be empty");
        m_diagEngine.Report(macroNameToken.getLocation(), id);
        return;
    }

    // Prepare tokens to generate source code, which is injected instead of the macro.
    Token staticToken;
    staticToken.startToken();
    staticToken.setKind(tok::kw_static);

    Token constexprToken;
    constexprToken.startToken();
    constexprToken.setKind(tok::kw_constexpr);

    Token lParenToken;
    lParenToken.startToken();
    lParenToken.setKind(tok::l_paren);

    Token rParenToken;
    rParenToken.startToken();
    rParenToken.setKind(tok::r_paren);

    Token lBraceToken;
    lBraceToken.startToken();
    lBraceToken.setKind(tok::l_brace);

    Token rBraceToken;
    rBraceToken.startToken();
    rBraceToken.setKind(tok::r_brace);

    Token semiToken;
    semiToken.startToken();
    semiToken.setKind(tok::semi);

    Token fremToken;
    fremToken.startToken();
    fremToken.setKind(tok::identifier);
    fremToken.setIdentifierInfo(m_preprocessor.getIdentifierInfo("frem"));

    Token coloncolonToken;
    coloncolonToken.startToken();
    coloncolonToken.setKind(tok::coloncolon);

    Token attributeToken;
    attributeToken.startToken();
    attributeToken.setKind(tok::identifier);
    attributeToken.setIdentifierInfo(m_preprocessor.getIdentifierInfo("__attribute__"));

    Token annotateToken;
    annotateToken.startToken();
    annotateToken.setKind(tok::identifier);
    annotateToken.setIdentifierInfo(m_preprocessor.getIdentifierInfo("annotate"));

    // Add an attraibute to annotate the RPC function by injecting:
    // '__attribute__(( annotate("_frem_rpc:<annotationGroup>") ))',
    // where <annotationGroup> is the identifier of the current annotation group.
    {
        std::stringstream ss;
        ss << "\"_frem_rpc:" << m_annotationGroup << '"';
        Token textToken;
        textToken.startToken();
        textToken.setKind(tok::string_literal);
        m_preprocessor.CreateString(ss.str(), textToken);

        SmallVector<Token, 12> tokenList;
        tokenList.push_back(attributeToken);
        tokenList.push_back(lParenToken);
        tokenList.push_back(lParenToken);

        tokenList.push_back(annotateToken);
        tokenList.push_back(lParenToken);
        tokenList.push_back(textToken);
        tokenList.push_back(rParenToken);

        tokenList.push_back(rParenToken);
        tokenList.push_back(rParenToken);

        for (auto& token : tokenList)
            if (token.getLocation().isInvalid())
                token.setLocation(macroNameToken.getLocation());

        auto tokens = std::make_unique<Token[]>(tokenList.size());
        std::copy(tokenList.begin(), tokenList.end(), tokens.get());

        m_preprocessor.EnterTokenStream(std::move(tokens),
                                        tokenList.size(),
                                        /*DisableMacroExpansion=*/false,
                                        /*IsReinject=*/false);
    }

    // Instead of
    //     FREM_RPC(Code(123), Alias("Foo"))
    // inject the two lines
    //     static constexpr ::frem::Code <generatedVarName>(123);
    //     static constexpr ::frem::Alias <generatedVarName>("Foo");
    // These source code lines end up in the AST and can be parsed there.
    for (unsigned annotationCount = 0; annotationCount < arguments.size(); ++annotationCount) {
        auto* argIter = arguments[annotationCount].begin();
        const auto& typeToken = **argIter++;

        // If possible, replace '(' and ')' by '{' and '}' to avoid the
        // most vexing parse.
        if (argIter != arguments[annotationCount].end() && (*argIter)->getKind() == tok::l_paren
            && arguments[annotationCount].back()->getKind() == tok::r_paren) {
            (*argIter) = &lBraceToken;
            arguments[annotationCount].back() = &rBraceToken;
        }

        auto typeTokenLoc = m_context.getSourceManager().getFileLoc(typeToken.getLocation());

        Token variableNameToken = uniqueVariable("_frem_rpc_arg", typeTokenLoc);

        // Inject:
        // 'static constexpr ::frem::' arg[0] generatedVarName arg[1] ... arg[n]
        SmallVector<Token, 32> tokenList;
        tokenList.push_back(staticToken);
        tokenList.push_back(constexprToken);
        tokenList.push_back(coloncolonToken);
        tokenList.push_back(fremToken);
        tokenList.push_back(coloncolonToken);
        tokenList.push_back(typeToken);
        tokenList.push_back(variableNameToken);
        for (; argIter != arguments[annotationCount].end(); ++argIter)
            tokenList.push_back(**argIter);
        tokenList.push_back(semiToken);

        // Note: There is some implicit information transport from the parser
        // to sema via the validity of the token location. For example,
        // the validity of the l-paren location distinguishes between
        // list-initialisation and ordinary constructor invokation. See
        // Sema::BuildCXXTypeConstructExpr() in SemaExprCXX.cpp.
        // By default, the location is invalid, leading to a crash.
        // Simply set all invalid token locations to the location of the
        // type token.
        for (auto& token : tokenList)
            if (token.getLocation().isInvalid())
                token.setLocation(typeTokenLoc);

        auto tokens = std::make_unique<Token[]>(tokenList.size());
        std::copy(tokenList.begin(), tokenList.end(), tokens.get());

        m_preprocessor.EnterTokenStream(std::move(tokens),
                                        tokenList.size(),
                                        /*DisableMacroExpansion=*/false,
                                        /*IsReinject=*/false);
    }
}

void MacroCallbacks::handleFremTypeAlias(const clang::Token& macroNameToken,
                                         const clang::MacroDefinition& definition,
                                         clang::SourceRange range,
                                         const clang::MacroArgs* args)
{
    // Handle `FREM_TYPE_ALIAS(type, aliasString)`.

    Token constexprToken;
    constexprToken.startToken();
    constexprToken.setKind(tok::kw_constexpr);

    Token autoToken;
    autoToken.startToken();
    autoToken.setKind(tok::kw_auto);

    Token sizeofToken;
    sizeofToken.startToken();
    sizeofToken.setKind(tok::kw_sizeof);

    Token coloncolonToken;
    coloncolonToken.startToken();
    coloncolonToken.setKind(tok::coloncolon);

    Token lParenToken;
    lParenToken.startToken();
    lParenToken.setKind(tok::l_paren);

    Token rParenToken;
    rParenToken.startToken();
    rParenToken.setKind(tok::r_paren);

    Token lBraceToken;
    lBraceToken.startToken();
    lBraceToken.setKind(tok::l_brace);

    Token rBraceToken;
    rBraceToken.startToken();
    rBraceToken.setKind(tok::r_brace);

    Token lAngleToken;
    lAngleToken.startToken();
    lAngleToken.setKind(tok::less);

    Token rAngleToken;
    rAngleToken.startToken();
    rAngleToken.setKind(tok::greater);

    Token semiToken;
    semiToken.startToken();
    semiToken.setKind(tok::semi);

    Token fremToken;
    fremToken.startToken();
    fremToken.setKind(tok::identifier);
    fremToken.setIdentifierInfo(m_preprocessor.getIdentifierInfo("frem"));

    Token typeAliasToken;
    typeAliasToken.startToken();
    typeAliasToken.setKind(tok::identifier);
    typeAliasToken.setIdentifierInfo(m_preprocessor.getIdentifierInfo("TypeAlias"));

    auto typeTokenLoc = m_context.getSourceManager().getFileLoc(
        args->getUnexpArgument(0)->getLocation());

    Token variableNameToken1 = uniqueVariable("_frem_var", typeTokenLoc);
    Token variableNameToken2 = uniqueVariable("_frem_var", typeTokenLoc);

    // Inject the following source code:
    //     'constexpr auto' generatedVarName1 '{ sizeof(' arg[0] ')};'
    //     'constexpr ::frem::TypeAlias<' arg[0] '>' generatedVarName2 '{' arg[1] '};'
    // For example,
    //     FREM_TYPE_ALIAS(MyClass, "MyCoolClass")
    // results in
    //     'constexpr auto $generatedVarName1 { sizeof( MyClass )};'
    //     'constexpr ::frem::TypeAlias< myClass >  $generatedVarName2 { "MyCoolClass" };'
    // Note that the `sizeof` operator is necessary in order to force C++ to generate the
    // complete template class. If this was not included, we would see an incomplete class
    // when handling the `TypeAlias` and were not able to inspect its layout.
    SmallVector<Token, 32> tokenList;
    tokenList.push_back(constexprToken);
    tokenList.push_back(autoToken);
    tokenList.push_back(variableNameToken1);
    tokenList.push_back(lBraceToken);
    tokenList.push_back(sizeofToken);
    tokenList.push_back(lParenToken);
    for (const Token* iter = args->getUnexpArgument(0); iter->isNot(tok::eof); ++iter)
        tokenList.push_back(*iter);
    tokenList.push_back(rParenToken);
    tokenList.push_back(rBraceToken);
    tokenList.push_back(semiToken);

    tokenList.push_back(constexprToken);
    tokenList.push_back(coloncolonToken);
    tokenList.push_back(fremToken);
    tokenList.push_back(coloncolonToken);
    tokenList.push_back(typeAliasToken);
    tokenList.push_back(lAngleToken);

    for (const Token* iter = args->getUnexpArgument(0); iter->isNot(tok::eof); ++iter)
        tokenList.push_back(*iter);

    tokenList.push_back(rAngleToken);

    tokenList.push_back(variableNameToken2);
    tokenList.push_back(lBraceToken);

    for (const Token* iter = args->getUnexpArgument(1); iter->isNot(tok::eof); ++iter)
        tokenList.push_back(*iter);

    tokenList.push_back(rBraceToken);
    tokenList.push_back(semiToken);


    // Make all token locations valid.
    for (auto& token : tokenList)
        if (token.getLocation().isInvalid())
            token.setLocation(typeTokenLoc);

    auto tokens = std::make_unique<Token[]>(tokenList.size());
    std::copy(tokenList.begin(), tokenList.end(), tokens.get());

    m_preprocessor.EnterTokenStream(std::move(tokens),
                                    tokenList.size(),
                                    /*DisableMacroExpansion=*/false,
                                    /*IsReinject=*/false);
}

// ----=====================================================================----
//     AstConsumer
// ----=====================================================================----

AstConsumer::AstConsumer(clang::CompilerInstance& compilerInstance,
                         llvm::StringRef inFile,
                         bool writeRpcs,
                         ParseResult& result)
    : m_parseResult(result)
    , m_ci(compilerInstance)
    , m_inFile(inFile)
    , m_visitor(compilerInstance.getASTContext(),
                compilerInstance.getDiagnostics(),
                writeRpcs,
                result)
{
}

void AstConsumer::Initialize(clang::ASTContext& context)
{
    auto& pp = m_ci.getPreprocessor();

    // Pre-definen the macro `FREM_GEN_RUN`. Can be used in source code to hide parts from
    // FremGen (e.g. if it would not cope with the syntax).
    std::string buffer;
    llvm::raw_string_ostream macroCode(buffer);
    clang::MacroBuilder builder(macroCode);
    builder.defineMacro("FREM_GEN_RUN");
    pp.setPredefines(macroCode.str() + pp.getPredefines());

    // Install the callback for handling FremGen macro invocations such as `FREM_RPC`.
    pp.addPPCallbacks(
        std::unique_ptr<PPCallbacks>(new MacroCallbacks(pp, context, m_ci.getDiagnostics())));
}

void AstConsumer::HandleTranslationUnit(clang::ASTContext& context)
{
    // The AST is final at this stage. Traverse it with our AST visitor, which will extract the
    // interface descriptions.
    m_visitor.TraverseDecl(context.getTranslationUnitDecl());
}
