
#include "fremgen/Archive.hpp"

#include "subprocess/subprocess.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

/// Do not pack header files with the following extensions.
static constexpr const char* g_skippedExtensions[] = {".idl", ".mshtml"};
static constexpr const char* g_archiveFileName = "header-archive.dat";

/// A helper function to deal with C-style FILE* streams in a C++-manner.
/// Reads from the `stream` until the '\n' character or the end-of-stream is encountered.
static std::string readLine(std::FILE* stream)
{
    char temp[256];
    std::string result;
    while (fgets(temp, sizeof(temp), stream)) {
        result += temp;
        auto len = strlen(temp);
        if (feof(stream) || temp[len - 1] == '\n')
            return result;
    }
    return std::string();
}

/// Parses the include directories from the `stream`.
/// The expected output of clang/gcc looks as follows:
///
/// \verbatim
/// prompt> clang -cc1 version 13.0.0 based upon LLVM 13.0.0git default target x86_64-unknown-linux-gnu
/// ignoring nonexistent directory "/opt/compiler-explorer/gcc-snapshot/lib/gcc/x86_64-linux-gnu/12.0.0/../../../../x86_64-linux-gnu/include"
/// ignoring nonexistent directory "/include"
/// #include "..." search starts here:
/// #include <...> search starts here:
///  /opt/compiler-explorer/gcc-snapshot/lib/gcc/x86_64-linux-gnu/12.0.0/../../../../include/c++/12.0.0
///  /opt/compiler-explorer/gcc-snapshot/lib/gcc/x86_64-linux-gnu/12.0.0/../../../../include/c++/12.0.0/x86_64-linux-gnu
///  /opt/compiler-explorer/gcc-snapshot/lib/gcc/x86_64-linux-gnu/12.0.0/../../../../include/c++/12.0.0/backward
///  /opt/compiler-explorer/clang-trunk-20210724/lib/clang/13.0.0/include
///  /usr/local/include
///  /usr/include/x86_64-linux-gnu
///  /usr/include
/// End of search list.
/// \endverbatim
///
/// The include directories are listed line by line and start with a single space.
static std::vector<std::string> getIncludeDirectories(std::FILE* stream)
{
    std::vector<std::string> result;
    while (true) {
        std::string line = readLine(stream);
        if (line.empty())
            break;
        if (line[0] != ' ')
            continue;

        // Trim whitespace from the string (left and right).
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
                       return !std::isspace(ch);
                   }));
        line.erase(std::find_if(line.rbegin(),
                                line.rend(),
                                [](unsigned char ch) { return !std::isspace(ch); })
                       .base(),
                   line.end());

        result.push_back(line);
    }
    return result;
}

/// Scans the `includeDirectories` for header files.
static std::set<std::filesystem::path> findHeaderFiles(
    const std::vector<std::string>& includeDirectories)
{
    auto skipExtension = [](const std::string& extension) {
        return std::find(std::begin(g_skippedExtensions), std::end(g_skippedExtensions), extension)
               != std::end(g_skippedExtensions);
    };

    // Iterate over all files in the include directories. Add these files to a set in order to unique them.
    // Removing duplicates is important since the include paths might be duplicates themselves.
    std::cout << "Scanning include directories for header files\n";
    std::set<std::filesystem::path> headers;
    for (const auto& includeDirectory : includeDirectories) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(includeDirectory)) {
            if (!entry.is_regular_file())
                continue;
            if (skipExtension(entry.path().extension().string()))
                continue;
            headers.insert(entry.path().lexically_normal());
        }
    }
    std::cout << "Found " << headers.size() << " header files\n";

    return headers;
}

/// Loop over all `includeDirectories` and writes the contained header files into an archive file.
static void packHeaders(const std::vector<std::string>& includeDirectories)
{
    auto readFile = [](const std::filesystem::path& path) -> std::vector<char> {
        std::ifstream stream(path, std::ios::in | std::ios::binary | std::ios::ate);
        auto fileSize = stream.tellg();
        stream.seekg(0, std::ios::beg);
        std::vector<char> data(fileSize);
        stream.read(data.data(), fileSize);
        return data;
    };

    auto filesToPack = findHeaderFiles(includeDirectories);

    std::cout << "Packing header files into '" << g_archiveFileName << "'\n";
    OutArchive ar(g_archiveFileName);
    // Archive a version.
    ar << uint32_t(1);
    // Write the include directories.
    ar << includeDirectories;
    // Write the files.
    ar << uint32_t(filesToPack.size());
    for (const auto& file : filesToPack) {
        ar << file.generic_string();
        ar << readFile(file);
    }
    std::cout << "Packed header files\n";
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cout << "Header file packer utility\n";
        std::cout << "Usage:\n" << argv[0] << " /path/to/bin/gcc\n";
        return -1;
    }

    // Create the command line for the compiler invocation.
    std::vector<const char*> commandLine;
    commandLine.push_back(argv[1]);
    commandLine.push_back("-Wp,-v");
    commandLine.push_back("-xc++");
    commandLine.push_back("-");
    commandLine.push_back("-fsyntax-only");
    commandLine.push_back(nullptr);

    auto toolName = std::filesystem::path(argv[1]).filename().string();
    std::cout << "Parsing include directories from '" << toolName << "'\n";

    subprocess_s subprocess;
    int result = subprocess_create(commandLine.data(), 0, &subprocess);
    if (result != 0) {
        std::cerr << "Failed to create subprocess for '" << argv[1] << "'\n";
        return result;
    }
    int returnCode;
    result = subprocess_join(&subprocess, &returnCode);
    if (result != 0) {
        std::cerr << "Failed to join subprocess '" << argv[1] << "'\n";
        return result;
    }
    if (returnCode != 0) {
        std::cerr << "'" << argv[1] << "' failed with return code " << returnCode << "\n";
        return result;
    }

    // Get the list of include directories from the output of the compiler invocation.
    auto includeDirectories = getIncludeDirectories(subprocess_stdout(&subprocess));
    if (includeDirectories.empty())
        includeDirectories = getIncludeDirectories(subprocess_stderr(&subprocess));
    subprocess_destroy(&subprocess);

    if (includeDirectories.empty()) {
        std::cerr << "Failed to parse include directories\n";
        return -1;
    }
    std::cout << "Parsed these include directories:\n";
    for (auto dir : includeDirectories)
        std::cout << "  " << dir << "\n";

    // Pack all header files in the include directories into an archive file.
    packHeaders(includeDirectories);

    return 0;
}
