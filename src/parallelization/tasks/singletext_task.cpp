#include "parallelization/tasks/singletext_task.hpp"

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>

#include "utils/utils.hpp"

SingleLineGenerationTask::SingleLineGenerationTask(const ExecutorResources &resources)
    : synthesizer_(resources.synthesizer),
      sampler_(resources.sampler),
      renderer_(resources.renderer),
      fontSelector_(resources.fontSelector),
      bgResources_(resources.bgResources)
{
}

GenerationResult SingleLineGenerationTask::executeTask()
{
    const int target = randInt(
        synthesizer_->getConfig()["text_sampler"].value("min_targets", 5),
        synthesizer_->getConfig()["text_sampler"].value("max_targets", 50));

    const std::string text = sampler_->generateString(target);
    SingleLineImageResult imageResult = synthesizer_->generateSingleImage(text, *renderer_, fontSelector_, bgResources_);

    std::vector<uchar> encoded;
    cv::imencode(".png", imageResult.image, encoded, {cv::IMWRITE_PNG_COMPRESSION, 3});

    GenerationResult result;
    result.encodedData = std::move(encoded);
    result.json_data = {
        {"text", imageResult.text},
        {"width", imageResult.width},
        {"height", imageResult.height},
        {"vertical", imageResult.vertical}
    };

    return result;
}