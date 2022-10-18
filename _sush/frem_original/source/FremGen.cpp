
#include "fremgen/Archive.hpp"
#include "fremgen/AstConsumer.hpp"

#include "clang/Basic/Diagnostic.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/Tooling.h"
#include "date/date.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>


using namespace date;
using namespace date::literals;
using namespace llvm;
using namespace std;

/// The string printed with the `--version` flag.
static constexpr const char* g_versionString = "FremGen v1.0.2";


/// Simple utility class to add the program's version to the stack trace.
class PrettyStackTraceWithVersion : public llvm::PrettyStackTraceEntry
{
public:
    void print(llvm::raw_ostream& out) const override { out << g_versionString << '\n';
    }
};


/// The basic FremGen frontend action.
///
/// An instance of this class is passed to the invocation of the Clang tooling library,
/// which in turn creates the AST consumer from it.
class FremGen : public clang::ASTFrontendAction
{
public:
    explicit FremGen(bool writeRpcs, ParseResult& result)
        : m_result(result)
        , m_writeRpcs(writeRpcs)
    {
    }

    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& CI,
                                                                  llvm::StringRef InFile) override
    {
        // Setup some preprocessor options mainly to speed up parsing of source files.
        CI.getPreprocessor().enableIncrementalProcessing(true);
        CI.getPreprocessor().SetSuppressIncludeNotFoundError(false);

        // As long as there is no interesting content in the body of a function, we can skip
        //over them and avoid generating the function body's AST.
        CI.getFrontendOpts().SkipFunctionBodies = true;

        CI.getLangOpts().DelayedTemplateParsing = true;
        CI.getLangOpts().MicrosoftExt = true;
        CI.getLangOpts().DollarIdents = true;
        CI.getLangOpts().CPlusPlus11 = true;
        CI.getLangOpts().CPlusPlus14 = true;
        CI.getLangOpts().CPlusPlus17 = true;
        CI.getLangOpts().GNUMode = true;

        return std::unique_ptr<clang::ASTConsumer>(
            new AstConsumer(CI, InFile, m_writeRpcs, m_result));
    }

private:
    ParseResult& m_result;
    bool m_writeRpcs;
};

static bool overlayHeaderFilesFromArchive(const std::string archiveFilename,
                                          std::vector<std::string>& compilerArguments,
                                          vfs::InMemoryFileSystem& fileSystem)
{
    if (!std::filesystem::exists(archiveFilename)) {
        std::cerr << "Header archive file '" << archiveFilename << "' does not exist\n";
        return false;
    }

    auto relocated = [](const std::string& filename) {
        std::filesystem::path input(filename);
        return (std::filesystem::path("/packed_header") / input.relative_path()).generic_string();
    };

    InArchive ar(archiveFilename);

    uint32_t version;
    ar >> version;
    if (version != 1) {
        std::cerr << "Invalid header archive file version\n";
        return false;
    }

    std::vector<std::string> includeDirectories;
    ar >> includeDirectories;
    compilerArguments.push_back("-nostdinc");
    for (const auto& dir : includeDirectories) {
        compilerArguments.push_back("-isystem");
        compilerArguments.push_back(relocated(dir));
    }

    uint32_t numFiles;
    ar >> numFiles;
    for (uint32_t count = 0; count < numFiles; ++count) {
        std::string headerFilename;
        std::vector<char> content;
        ar >> headerFilename;
        ar >> content;
        fileSystem.addFile(relocated(headerFilename),
                           0,
                           llvm::MemoryBuffer::getMemBufferCopy(
                               llvm::StringRef(content.data(), content.size())));
    }

    return true;
}

int main(int argc, char* argv[])
{
    using namespace clang;

    std::cout << g_versionString << " starting." << std::endl;

    struct Options
    {
        std::string outputFile;
        std::string headerArchiveFile;
        bool appendOutput = false;
    };
    Options options;

    enum class ParserState
    {
        ParseOption,
        ParseInputFile,
        ParseTypeInputFile,
        ParseOutputFile,
        ParseHeaderArchiveFile
    };

    // Generate a list of compiler arguments.
    std::vector<std::string> compilerArguments;
    compilerArguments.push_back(argv[0]);
    // Push default arguments. This list could also be deleted in which case the user has to supply
    // the arguments on the command line.
    for (const char* arg : {"-xc++", "-fsyntax-only", "-Wno-attributes", "-Wall", "-Werror"})
        compilerArguments.push_back(arg);
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
    compilerArguments.push_back("-fno-ms-compatibility");
#endif

    struct InputFileSpec
    {
        std::string name;
        bool isTypeSource;
    };
    std::vector<InputFileSpec> inputFiles;

    ParserState state = ParserState::ParseOption;
    for (int index = 1; index < argc; ++index) {
        std::string_view arg(argv[index]);
        if (arg == "--version") {
            std::cout << g_versionString << std::endl;
            return 0;
        }

        switch (state) {
        case ParserState::ParseOption:
            if (arg.starts_with("--")) {
                if (arg == "--source") {
                    state = ParserState::ParseInputFile;
                }
                else if (arg == "--type-source") {
                    state = ParserState::ParseTypeInputFile;
                }
                else if (arg == "--out") {
                    state = ParserState::ParseOutputFile;
                }
                else if (arg == "--header-archive") {
                    state = ParserState::ParseHeaderArchiveFile;
                }
                else if (arg == "--incremental") {
                    options.appendOutput = true;
                    state = ParserState::ParseOption;
                }
                else {
                    std::cerr << "Unknown argument '" << arg << "'" << std::endl;
                    return -1;
                }
            }
            else {
                compilerArguments.push_back(std::string(arg));
            }
            break;

        case ParserState::ParseOutputFile:
            options.outputFile = arg;
            state = ParserState::ParseOption;
            break;

        case ParserState::ParseHeaderArchiveFile:
            options.headerArchiveFile = arg;
            state = ParserState::ParseOption;
            break;

        case ParserState::ParseInputFile:
            [[fallthrough]];
        case ParserState::ParseTypeInputFile:
            if (arg.find("--") == 0) {
                state = ParserState::ParseOption;
                --index;
                break;
            }
            else {
                inputFiles.emplace_back(
                    InputFileSpec{std::string(arg), state == ParserState::ParseTypeInputFile});
                break;
            }
        }
    }

    llvm::setBugReportMsg("PLEASE submit a bug report to "
                          "the Maintainer listed in Confluence"
                          " and include the crash backtrace.\n");
    PrettyStackTraceWithVersion stackTrace;

    // Create an overlay file system, which consists of an in-memory file system layered above the real
    // file system. The in-memory file system is populated with the header files bundled with the
    // executable.
    auto fileSystem = makeIntrusiveRefCnt<vfs::OverlayFileSystem>(vfs::getRealFileSystem());
    llvm::IntrusiveRefCntPtr<vfs::InMemoryFileSystem> memoryFileSystem(new vfs::InMemoryFileSystem);
    fileSystem->pushOverlay(memoryFileSystem);
    if (!options.headerArchiveFile.empty()) {    
        if (!overlayHeaderFilesFromArchive(options.headerArchiveFile,
                                           compilerArguments,
                                           *memoryFileSystem)) {
            return -2;
        }
    }    

    llvm::IntrusiveRefCntPtr<clang::FileManager> fileManager(
        new clang::FileManager(FileSystemOptions(), fileSystem));

    // The result of the frontend action.
    ParseResult parseResult;

    // If the output shall be appended to the input, we have to read the
    // out file first.
    if (options.appendOutput && !options.outputFile.empty()) {
        llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file = fileManager->getBufferForFile(
            options.outputFile);
        if (file) {
            llvm::yaml::Input input((*file)->getBuffer());
            input >> parseResult;
        }
    }

    // Process all input files.
    for (const auto& file : inputFiles) {
        bool success = true;
        // In order to invoke the Clang tool on many different source files in a row, we have to
        // cheat a bit and invoke the tool on a per-file basis. For this, we have to generate a
        // new set of command line arguments to which we append the name of a single input file.
        std::vector<std::string> argCopy = compilerArguments;
        argCopy.push_back(file.name);
        clang::tooling::ToolInvocation
            invocation(argCopy,
                       std::make_unique<FremGen>(/*writeRpcs=*/!file.isTypeSource, parseResult),
                       fileManager.get());
        success = invocation.run();
        if (!success)
            return -3;
    }

    // Write the parse results into a YAML file.
    std::string yaml;
    {
        llvm::raw_string_ostream stream(yaml);
        llvm::yaml::Output output(stream);
        output << parseResult;
    }
    if (!options.outputFile.empty()) {
        std::ofstream file(options.outputFile);
        file << yaml;
    }
    else {
        std::cout << yaml;
    }

    return 0;
}
