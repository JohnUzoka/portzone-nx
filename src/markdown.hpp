#pragma once

#include <string>
#include <vector>

namespace pz
{

// A README reduced to what a label can show: no images, no HTML, links as their text.
struct TextBlock
{
    enum class Kind
    {
        Heading,
        Paragraph,
        Bullet,
        Code,
    };
    Kind kind;
    int level = 0; // heading level 1-6, or list depth from 0
    std::string text;
};

std::vector<TextBlock> parseMarkdown(const std::string& markdown);

} // namespace pz
