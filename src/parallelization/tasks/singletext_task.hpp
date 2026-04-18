#pragma once

#include <string>

#include "parallelization/parallel.hpp"
#include "text_synth/text_synthesizer.hpp"
#include "text_synth/textsampler.hpp"

class SingleLineGenerationTask
{
public:
    struct ExecutorResources
    {
        SingleLineTextSynthesizer *synthesizer = nullptr;
        TextSampler *sampler = nullptr;
    };

    explicit SingleLineGenerationTask(const ExecutorResources &resources);

    GenerationResult executeTask();

private:
    SingleLineTextSynthesizer *synthesizer_;
    TextSampler *sampler_;
};