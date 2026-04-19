#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "text_synth/text_synthesizer.hpp"

class SingleLineTextGenerator
{
public:
    explicit SingleLineTextGenerator(const std::string &configStr);

    // Returns {totalGenerated, totalErrors}.
    std::pair<int64_t, int64_t> generate(int total, int workers = 0, bool showProgress = true);
    void generateInstanceFile(const std::string &text, const std::string &savePath) const;
    SingleLineTextSynthesizer::ImageResult generateInstanceExplicit(const std::string &text) const;
    std::string getConfigJson() const;

private:
    SingleLineTextSynthesizer synthesizer_;
};