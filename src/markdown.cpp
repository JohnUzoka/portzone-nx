#include "markdown.hpp"

#include <regex>
#include <sstream>

namespace pz
{

namespace
{

    std::string trim(const std::string& text)
    {
        const auto first = text.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            return "";
        return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    }

    // Inline markup: images go, links keep their text, HTML tags and emphasis markers go.
    std::string inlineText(std::string text)
    {
        static const std::regex image(R"(!\[[^\]]*\]\([^)]*\))");
        static const std::regex link(R"(\[([^\]]*)\]\([^)]*\))");
        static const std::regex refLink(R"(\[([^\]]*)\]\[[^\]]*\])");
        static const std::regex autolink(R"(<(https?://[^>]+)>)");
        static const std::regex tag(R"(</?[A-Za-z][^>]*>)");
        static const std::regex emphasis(R"(\*\*|__|`)");
        static const std::regex italic(R"((^|[\s(])[*_]([^*_\s][^*_]*?)[*_](?=$|[\s.,;:!?)]))");
        static const std::regex spaces(R"([ \t]{2,})");
        text = std::regex_replace(text, image, "");
        text = std::regex_replace(text, link, "$1");
        text = std::regex_replace(text, refLink, "$1");
        text = std::regex_replace(text, autolink, "$1");
        text = std::regex_replace(text, tag, "");
        text = std::regex_replace(text, emphasis, "");
        text = std::regex_replace(text, italic, "$1$2");
        text = std::regex_replace(text, spaces, " ");
        for (const auto& [entity, value] : { std::pair<const char*, const char*> { "&amp;", "&" }, { "&lt;", "<" },
                 { "&gt;", ">" }, { "&quot;", "\"" }, { "&nbsp;", " " }, { "&#39;", "'" } })
        {
            for (size_t at = text.find(entity); at != std::string::npos; at = text.find(entity, at))
                text.replace(at, std::char_traits<char>::length(entity), value);
        }
        // The Switch font has no emoji: drop 4-byte UTF-8 characters and the emoji variation selector.
        std::string drawable;
        for (size_t i = 0; i < text.size();)
        {
            const auto byte = static_cast<unsigned char>(text[i]);
            if (byte >= 0xF0)
                i += 4;
            else if (text.compare(i, 3, "\xEF\xB8\x8F") == 0)
                i += 3;
            else
                drawable += text[i++];
        }
        return trim(drawable);
    }

} // namespace

std::vector<TextBlock> parseMarkdown(const std::string& markdown)
{
    static const std::regex heading(R"(^(#{1,6})\s+(.*?)\s*#*\s*$)");
    static const std::regex bullet(R"(^(\s*)(?:[-*+]|\d+[.)])\s+(.*)$)");
    static const std::regex rule(R"(^\s*([-*_]\s*){3,}$)");
    static const std::regex tableSeparator(R"(^\s*\|?\s*:?-{2,}.*$)");
    static const std::regex referenceDefinition(R"(^\s*\[[^\]]+\]:\s*\S+.*$)");

    std::vector<TextBlock> blocks;
    std::string paragraph, code;
    bool inCode = false, inComment = false;

    auto flush = [&] {
        const std::string text = inlineText(paragraph);
        if (!text.empty())
            blocks.push_back({ TextBlock::Kind::Paragraph, 0, text });
        paragraph.clear();
    };

    std::istringstream lines(markdown);
    std::string line;
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::string trimmed = trim(line);

        if (trimmed.rfind("```", 0) == 0 || trimmed.rfind("~~~", 0) == 0)
        {
            if (inCode)
            {
                blocks.push_back({ TextBlock::Kind::Code, 0, code.empty() ? code : code.substr(0, code.size() - 1) });
                code.clear();
            }
            else
                flush();
            inCode = !inCode;
            continue;
        }
        if (inCode)
        {
            code += line + "\n";
            continue;
        }

        // HTML comments can span lines.
        if (inComment)
        {
            if (trimmed.find("-->") != std::string::npos)
                inComment = false;
            continue;
        }
        if (trimmed.rfind("<!--", 0) == 0)
        {
            inComment = trimmed.find("-->") == std::string::npos;
            continue;
        }

        std::smatch match;
        if (trimmed.empty() || std::regex_match(line, rule) || std::regex_match(line, referenceDefinition))
            flush();
        else if (std::regex_match(line, match, heading))
        {
            flush();
            const std::string text = inlineText(match[2]);
            if (!text.empty())
                blocks.push_back({ TextBlock::Kind::Heading, int(match[1].length()), text });
        }
        else if (std::regex_match(line, match, bullet))
        {
            flush();
            const std::string text = inlineText(match[2]);
            if (!text.empty())
                blocks.push_back({ TextBlock::Kind::Bullet, int(match[1].length() / 2), text });
        }
        else if (trimmed.front() == '|')
        {
            flush();
            if (std::regex_match(line, tableSeparator))
                continue;
            std::string row;
            std::istringstream cells(trimmed);
            std::string cell;
            while (std::getline(cells, cell, '|'))
            {
                cell = inlineText(cell);
                if (!cell.empty())
                    row += (row.empty() ? "" : "  ·  ") + cell;
            }
            if (!row.empty())
                blocks.push_back({ TextBlock::Kind::Paragraph, 0, row });
        }
        else if (!blocks.empty() && paragraph.empty() && blocks.back().kind == TextBlock::Kind::Bullet &&
                 (line.front() == ' ' || line.front() == '\t'))
            blocks.back().text += " " + inlineText(trimmed); // a list item's continuation line
        else if (trimmed.front() == '>')
            paragraph += trimmed.substr(1) + " ";
        else
            paragraph += trimmed + " ";
    }
    if (inCode && !code.empty())
        blocks.push_back({ TextBlock::Kind::Code, 0, code });
    flush();
    return blocks;
}

} // namespace pz
