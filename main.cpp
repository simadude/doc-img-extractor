#include <iostream>
#include <string>
#include <vector>
#include <utility>
#include <cstdlib>

int call(std::string c) {
    std::string buffer;
    buffer.append(c);
    buffer.append(" > /dev/null 2>&1");
    return system(buffer.c_str());
};

int check_dependencies() {
    int dep_count = 0;

    // Check: file -v (expected 0)
    {
        int res = call("file -v");
        if (res == 32512) std::cout << "Command not found: file\n";
        else if (res == 0) dep_count++;
        else std::cout << "Unexpected exitcode (" << res << "), expected 0 from file -v\n";
    }

    // Check: ddjvu --help (expected 256)
    {
        int res = call("ddjvu --help");
        if (res == 32512) std::cout << "Command not found: ddjvu\n";
        else if (res == 256) dep_count++;
        else std::cout << "Unexpected exitcode (" << res << "), expected 256 from ddjvu --help\n";
    }

    // Check: djvused --help (expected 2560)
    {
        int res = call("djvused --help");
        if (res == 32512) std::cout << "Command not found: djvused\n";
        else if (res == 2560) dep_count++;
        else std::cout << "Unexpected exitcode (" << res << "), expected 2560 from djvused --help\n";
    }

    // Check: soffice --version (expected 0)
    {
        int res = call("soffice --version");
        if (res == 32512) std::cout << "Command not found: soffice\n";
        else if (res == 0) dep_count++;
        else std::cout << "Unexpected exitcode (" << res << "), expected 0 from soffice --version\n";
    }

    // Check: pdfimages -v (expected 0)
    {
        int res = call("pdfimages -v");
        if (res == 32512) std::cout << "Command not found: pdfimages\n";
        else if (res == 0) dep_count++;
        else std::cout << "Unexpected exitcode (" << res << "), expected 0 from pdfimages -v\n";
    }

    return dep_count;
}

int main() {
    std::cout << "Checking dependencies...\n";
    int dep_count = check_dependencies();
    const int total = 5;
    std::cout << "Dependencies: " << dep_count << "/" << total << std::endl;
    return 0;
};