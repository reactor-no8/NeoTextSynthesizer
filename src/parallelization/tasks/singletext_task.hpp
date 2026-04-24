#pragma once

#include <string>

#include "parallelization/parallel.hpp"
#include "text_synth/text_synthesizer.hpp"
#include "text_synth/textsampler.hpp"
#include "backgrounds/background_resources.hpp"

class BackgroundResources;
class GlyphCache;
class FontSelector;

class SingleLineGenerationTask
{
public:
    struct ExecutorResources
    {
        SingleLineTextSynthesizer *synthesizer = nullptr;
        TextSampler *sampler = nullptr;
        SingleLineRenderer *renderer = nullptr;
        FontSelector &fontSelector;
        BackgroundResources &bgResources;
    };

    explicit SingleLineGenerationTask(const ExecutorResources &resources);

    GenerationResult executeTask();

private:
    SingleLineTextSynthesizer *synthesizer_;
    TextSampler *sampler_;
    SingleLineRenderer *renderer_;
    FontSelector &fontSelector_;
    BackgroundResources &bgResources_;
};