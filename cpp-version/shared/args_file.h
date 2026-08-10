#ifndef SHARED_ARGS_FILE_H
#define SHARED_ARGS_FILE_H

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Expand any "--args-file <path>", "-A <path>", or "@path" token in argv into the tokens contained
// in that file, so a stable command line can drive different runs by editing the file (avoids
// re-granting run permission for every argument change). File tokens are whitespace/newline
// separated; '#' begins a line comment. One level deep — an --args-file token *inside* a file is
// not re-expanded. Returns the full expanded token list (including argv[0]).
//
// Usage (keeps the owning storage alive for the lifetime of parsing):
//   std::vector<std::string> store = expandArgsFiles(argc, argv);
//   std::vector<char*> av; for (auto& s : store) av.push_back(const_cast<char*>(s.c_str()));
//   argc = (int)av.size(); argv = av.data();
inline std::vector<std::string> expandArgsFiles(int argc, char** argv) {
    std::vector<std::string> out;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        std::string path;
        if ((a == "--args-file" || a == "-A") && i + 1 < argc) path = argv[++i];
        else if (a.size() > 1 && a[0] == '@') path = a.substr(1);
        else { out.push_back(a); continue; }

        std::ifstream f(path);
        if (!f) {
            fprintf(stderr, "Warning: cannot read args file '%s' (ignored)\n", path.c_str());
            continue;
        }
        std::string line;
        while (std::getline(f, line)) {
            std::size_t hash = line.find('#');           // strip trailing line comment
            if (hash != std::string::npos) line.resize(hash);
            std::istringstream ss(line);
            std::string tok;
            while (ss >> tok) out.push_back(tok);
        }
    }
    return out;
}

#endif  // SHARED_ARGS_FILE_H
