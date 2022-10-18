//===---- tools/extra/ToolTemplate.cpp - Template for refactoring tool ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements an empty refactoring tool using the clang tooling.
//  The goal is to lower the "barrier to entry" for writing refactoring tools.
//
//  Usage:
//  tool-template <cmake-output-dir> <file1> <file2> ...
//
//  Where <cmake-output-dir> is a CMake build directory in which a file named
//  compile_commands.json exists (enable -DCMAKE_EXPORT_COMPILE_COMMANDS in
//  CMake to get this output).
//
//  <file1> ... specify the paths of files in the CMake source tree. This path
//  is looked up in the compile command database. If the path of a file is
//  absolute, it needs to point into CMake's source tree. If the path is
//  relative, the current working directory needs to be in the CMake source
//  tree and the file must be in a subdirectory of the current working
//  directory. "./" prefixes in the relative files will be automatically
//  removed, but the rest of a relative path must be a suffix of a path in
//  the compile command line database.
//
//  For example, to use tool-template on all files in a subtree of the
//  source tree, use:
//
//    /path/in/subtree $ find . -name '*.cpp'|
//        xargs tool-template /path/to/build
//
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Execution.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Refactoring/AtomicChange.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"

#include <cstring>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

namespace sush {
std::vector<clang::ParmVarDecl *> params;
std::vector<clang::CXXMethodDecl *> methods;
} // namespace sush

namespace {
class ToolTemplateCallback : public MatchFinder::MatchCallback {
public:
  ToolTemplateCallback(ExecutionContext &Context) : Context(Context) {}

  void run(const MatchFinder::MatchResult &Result) override {

    m_SourceManager = Result.SourceManager;
    // auto *namedDecl = Result.Nodes.getNodeAs<NamedDecl>("decl");
    // assert(namedDecl);

    //        << m_SourceManager->getFilename(namedDecl->getLocation()).str();

    // TODO: This routine will get called for each thing that the matchers
    // find.
    // At this point, you can examine the match, and do whatever you want,
    // including replacing the matched text with other text

    if (auto *cxxMethodDecl = Result.Nodes.getNodeAs<CXXMethodDecl>("decl");
        cxxMethodDecl && cxxMethodDecl->getBeginLoc().isValid()) {
      visitMethod(cxxMethodDecl);
    }

    if (auto *cxxRecordDecl = Result.Nodes.getNodeAs<CXXRecordDecl>("decl");
        cxxRecordDecl && cxxRecordDecl->getBeginLoc().isValid()) {
      visitStructOrClass(cxxRecordDecl);
    }

    if (auto *varDecl = Result.Nodes.getNodeAs<VarDecl>("decl");
        varDecl && varDecl->getBeginLoc().isValid()) {
      visitStructOrClass_referenced_type_test(varDecl);
    }
  }

  void visitStructOrClass_referenced_type_test(const clang::VarDecl *varDecl) {
    // https://clang.llvm.org/doxygen/classclang_1_1CXXRecordDecl.html
    // https://clang.llvm.org/doxygen/classclang_1_1ParmVarDecl.html

    if (!varDecl->hasAttr<AnnotateAttr>()) {
      return;
    }

    AnnotateAttr *attr = varDecl->getAttr<AnnotateAttr>();
    auto annotation = attr->getAnnotation();
    if (!annotation.startswith("AP_REFERENCE_TYPE")) {
      return;
    }

    llvm::outs() << "AP_REFERENCE_TYPE: \n";
    llvm::outs() << "  <VarDecl> \n";

    if (varDecl->getType()->isAggregateType()) {
      _visitCxxRecordDecl(varDecl->getType()->getAsCXXRecordDecl(), annotation);
    }

  }

  void visitStructOrClass(const clang::CXXRecordDecl *cxxRecordDecl) {
    // https://clang.llvm.org/doxygen/classclang_1_1CXXRecordDecl.html
    // https://clang.llvm.org/doxygen/classclang_1_1ParmVarDecl.html

    if (!cxxRecordDecl->hasAttr<AnnotateAttr>()) {
      return;
    }

    AnnotateAttr *attr = cxxRecordDecl->getAttr<AnnotateAttr>();
    auto annotation = attr->getAnnotation();
    if (!annotation.startswith("AP_TYPE")) {
      return;
    }

    llvm::outs() << "AP_TYPE: \n";
    _visitCxxRecordDecl(cxxRecordDecl, annotation);
  }

  void _visitCxxRecordDecl(const clang::CXXRecordDecl *cxxRecordDecl,
                           llvm::StringRef annotation) {

    llvm::outs() << "  <CXXRecordDecl> \n";
    llvm::outs() << "  Fully Qualified Name: "
                 << cxxRecordDecl->getQualifiedNameAsString() << "\n";
    llvm::outs() << "  Short Name: " << cxxRecordDecl->getDeclName() << "\n";
    llvm::outs() << "  Annotation: " << annotation << "\n";
    llvm::outs()
        << "  Filename: "
        << m_SourceManager->getFilename(cxxRecordDecl->getLocation()).str()
        << "\n";
    llvm::outs() << "  Fields: "
                 << "\n";

    if (cxxRecordDecl->fields().empty()) {
      llvm::outs() << "    <no fields>\n";
    } else {
      for (const auto &p : cxxRecordDecl->fields()) {
        llvm::outs() << "    Name: " << p->getNameAsString() << "\n ";
        llvm::outs() << "      Fully Qualified Type Name: "
                     << p->getType().getAsString() << "\n";
      }
    }
  }

  void visitMethod(const clang::CXXMethodDecl *cxxMethodDecl) {
    // https://clang.llvm.org/doxygen/classclang_1_1CXXMethodDecl.html

    if (!cxxMethodDecl->hasAttr<AnnotateAttr>()) {
      return;
    }

    AnnotateAttr *attr = cxxMethodDecl->getAttr<AnnotateAttr>();
    auto annotation = attr->getAnnotation();
    if (!annotation.startswith("AP_RPC")) {
      return;
    }

    llvm::outs() << "AP_RPC: \n";
    llvm::outs() << "  <CXXMethodDecl> \n";
    llvm::outs() << "  Fully Qualified Name: "
                 << cxxMethodDecl->getQualifiedNameAsString() << "\n";
    llvm::outs() << "  Short Name: " << cxxMethodDecl->getDeclName() << "\n";
    llvm::outs() << "  Annotation: " << annotation << "\n";
    llvm::outs()
        << "  Filename: "
        << m_SourceManager->getFilename(cxxMethodDecl->getLocation()).str()
        << "\n";
    llvm::outs() << "  Parameters as in Argument List: "
                 << "\n";
    if (cxxMethodDecl->parameters().empty()) {
      llvm::outs() << "    <no parameters>\n";
    } else {
      for (const auto &p : cxxMethodDecl->parameters()) {
        llvm::outs() << "    Name: " << p->getNameAsString() << "\n ";
        llvm::outs() << "      Fully Qualified Type Name: "
                     << p->getOriginalType().getAsString() << "\n";
      }
    }
  }

  void onStartOfTranslationUnit() override {
    Context.reportResult("START", "Start of TU.");
  }
  void onEndOfTranslationUnit() override {
    Context.reportResult("END", "End of TU.");
  }
  // bool VisitNamedDevl_Attrs(const NamedDecl *v,
  //                          clang::SourceManager *sourcemanager) {
  //  v->dump();
  //  if (v->hasAttrs()) {
  //    clang::AttrVec vec = v->getAttrs();
  //    printf("getSpelling: %s\n", vec[0]->getSpelling());
  //    printf("getSourceText: %s\n",
  //           Lexer::getSourceText(
  //               CharSourceRange::getTokenRange(vec[0]->getRange()),
  //               *sourcemanager, LangOptions())
  //               .str()
  //               .c_str());
  //  }
  //  return true;
  //}

private:
  ExecutionContext &Context;
  clang::SourceManager *m_SourceManager{nullptr};
};
} // end anonymous namespace

// Set up the command line options
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::OptionCategory ToolTemplateCategory("tool-template options");

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

  auto Executor = clang::tooling::createExecutorFromCommandLineArgs(
      argc, argv, ToolTemplateCategory);

  if (!Executor) {
    llvm::errs() << llvm::toString(Executor.takeError()) << "\n";
    return 1;
  }

  ast_matchers::MatchFinder Finder;
  ToolTemplateCallback Callback(*Executor->get()->getExecutionContext());

  // TODO: Put your matchers here.
  // Use Finder.addMatcher(...) to define the patterns in the AST that you
  // want to match against. You are not limited to just one matcher!
  //
  // This is a sample matcher:
  // Finder.addMatcher(
  //    namedDecl(cxxRecordDecl(), isExpansionInMainFile()).bind("decl"),
  //    &Callback);

  // Finder.addMatcher(cxxRecordDecl().bind("decl"), &Callback);
  Finder.addMatcher(cxxMethodDecl(decl().bind("decl"), hasAttr(attr::Annotate)),
                    &Callback);
  Finder.addMatcher(cxxRecordDecl(decl().bind("decl"), hasAttr(attr::Annotate)),
                    &Callback);
  Finder.addMatcher(varDecl(decl().bind("decl"), hasAttr(attr::Annotate)),
                    &Callback);

  auto Err = Executor->get()->execute(newFrontendActionFactory(&Finder));
  if (Err) {
    llvm::errs() << llvm::toString(std::move(Err)) << "\n";
  }
  Executor->get()->getToolResults()->forEachResult(
      [](llvm::StringRef key, llvm::StringRef value) {
        llvm::errs() << "----" << key.str() << "\n" << value.str() << "\n";
      });
}
