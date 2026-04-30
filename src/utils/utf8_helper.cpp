#include "utils/utf8_helper.hpp"

int UTF8Helper::Length(const std::string &input)
{
    int count = 0;
    for (unsigned char c : input)
    {
        if ((c & 0xC0) != 0x80)
        {
            count++;
        }
    }
    return count;
}

std::vector<std::string> UTF8Helper::Split(const std::string &input)
{
    std::vector<std::string> chars;
    size_t i = 0;
    while (i < input.size())
    {
        const int len = static_cast<int>(CharLenFromLead(static_cast<unsigned char>(input[i])));
        chars.push_back(input.substr(i, len));
        i += len;
    }
    return chars;
}

std::vector<uint32_t> UTF8Helper::ToCodepoints(const std::string &input)
{
    std::vector<uint32_t> cps;
    size_t i = 0;
    while (i < input.size())
    {
        uint32_t cp = 0;
        const unsigned char c = static_cast<unsigned char>(input[i]);
        int len = 1;
        if ((c & 0x80) == 0x00)
        {
            cp = c;
            len = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            cp = c & 0x1F;
            len = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            cp = c & 0x0F;
            len = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            cp = c & 0x07;
            len = 4;
        }

        for (int j = 1; j < len && i + static_cast<size_t>(j) < input.size(); ++j)
        {
            cp = (cp << 6) | (static_cast<unsigned char>(input[i + static_cast<size_t>(j)]) & 0x3F);
        }
        cps.push_back(cp);
        i += static_cast<size_t>(len);
    }
    return cps;
}

std::string UTF8Helper::Truncate(const std::string &input, int codepointCount)
{
    int count = 0;
    size_t i = 0;
    while (i < input.size() && count < codepointCount)
    {
        i += CharLenFromLead(static_cast<unsigned char>(input[i]));
        count++;
    }
    return input.substr(0, i);
}

std::string UTF8Helper::Strip(const std::string &input)
{
    size_t start = 0;
    size_t end = input.size();

    while (start < end)
    {
        unsigned char c = static_cast<unsigned char>(input[start]);

        if (c < 0x80 && std::isspace(c))
        {
            start++;
        }
        else
        {
            break;
        }
    }

    while (end > start)
    {
        unsigned char c = static_cast<unsigned char>(input[end - 1]);
        if (c < 0x80 && std::isspace(c))
        {
            end--;
        }
        else
        {
            break;
        }
    }

    return input.substr(start, end - start);
}


size_t UTF8Helper::CharLenFromLead(unsigned char c)
{
    if ((c & 0x80) == 0x00)
    {
        return 1;
    }
    if ((c & 0xE0) == 0xC0)
    {
        return 2;
    }
    if ((c & 0xF0) == 0xE0)
    {
        return 3;
    }
    if ((c & 0xF8) == 0xF0)
    {
        return 4;
    }
    return 1;
}

bool UTF8Helper::IsValidUtf8Start(unsigned char c)
{
    return (c & 0x80) == 0x00 || (c & 0xE0) == 0xC0 || (c & 0xF0) == 0xE0 || (c & 0xF8) == 0xF0;
}