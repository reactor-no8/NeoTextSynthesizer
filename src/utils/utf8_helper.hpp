#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class UTF8Helper
{
public:
    static int Length(const std::string &input);
    static std::vector<std::string> Split(const std::string &input);
    static std::vector<uint32_t> ToCodepoints(const std::string &input);
    static std::string Truncate(const std::string &input, int codepointCount);
    static std::string Strip(const std::string &input);

    static size_t CharLenFromLead(unsigned char c);
    static bool IsValidUtf8Start(unsigned char c);
};