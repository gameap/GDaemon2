#include "gstring.h"

#include <cstring>
#include <stdarg.h>  // For va_start, etc.
#include <memory>    // For std::unique_ptr

// ---------------------------------------------------------------------

std::string str_replace(const std::string& search, const std::string& replace, const std::string& subject)
{
    std::string str = subject;
    size_t pos = 0;
    while((pos = str.find(search, pos)) != std::string::npos)
    {
        str.replace(pos, search.length(), replace);
        pos += replace.length();
    }
    return str;
}

std::string string_format(const std::string fmt_str, ...) {
    int final_n, n = ((int)fmt_str.size()) * 2; 
    std::string str;
    std::unique_ptr<char[]> formatted;
    va_list ap;
    while(1) {
        formatted.reset(new char[n]);
        strcpy(&formatted[0], fmt_str.c_str());
        va_start(ap, fmt_str);
        final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
        va_end(ap);
        if (final_n < 0 || final_n >= n)
            n += abs(final_n - n + 1);
        else
            break;
    }
    return std::string(formatted.get());
}
