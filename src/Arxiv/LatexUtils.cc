#include "Arxiv/LatexUtils.hh"

#include <cctype>
#include <string>

namespace Arxiv {

std::string StripLatex(const std::string &text) {
    std::string out;
    out.reserve(text.size());

    const std::size_t n = text.size();
    std::size_t i = 0;

    while (i < n) {
        char c = text[i];

        // ---------------------------------------------------------------
        // Rule 1 & 2: Strip $...$ and $$...$$ math regions
        // ---------------------------------------------------------------
        if (c == '$') {
            bool display = (i + 1 < n && text[i + 1] == '$');
            const std::string_view close = display ? "$$" : "$";
            std::size_t start = i + (display ? 2 : 1);
            std::size_t end = text.find(close, start);
            if (end == std::string::npos) {
                // Unmatched — consume the dollar sign and continue
                out += ' ';
                ++i;
            } else {
                out += ' ';
                i = end + close.size();
            }
            continue;
        }

        // ---------------------------------------------------------------
        // Rule 3 & 4: Handle \cmd and \cmd{content}
        // ---------------------------------------------------------------
        if (c == '\\' && i + 1 < n && std::isalpha(static_cast<unsigned char>(text[i + 1]))) {
            // Consume the command name
            std::size_t j = i + 1;
            while (j < n && std::isalpha(static_cast<unsigned char>(text[j])))
                ++j;
            // Check if followed by an optional star (e.g. \textbf*)
            if (j < n && text[j] == '*') ++j;

            // Skip optional whitespace before '{'
            std::size_t k = j;
            while (k < n && text[k] == ' ') ++k;

            if (k < n && text[k] == '{') {
                // Rule 3: \cmd{content} — replace with ' ' + content (brace-aware)
                out += ' ';
                ++k; // skip opening '{'
                int depth = 1;
                while (k < n && depth > 0) {
                    if (text[k] == '{') {
                        ++depth;
                        if (depth > 1) out += text[k]; // keep inner { for nested
                    } else if (text[k] == '}') {
                        --depth;
                        if (depth > 0) out += text[k];
                    } else {
                        out += text[k];
                    }
                    ++k;
                }
                i = k;
            } else {
                // Rule 4: bare \cmd — replace with space
                out += ' ';
                i = j;
            }
            continue;
        }

        // ---------------------------------------------------------------
        // Ordinary character — pass through
        // ---------------------------------------------------------------
        out += c;
        ++i;
    }

    return out;
}

} // namespace Arxiv
