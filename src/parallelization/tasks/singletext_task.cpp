#include "parallelization/tasks/singletext_task.hpp"

#include <opencv2/opencv.hpp>

#include "utils/utils.hpp"

SingleLineGenerationTask::SingleLineGenerationTask(const ExecutorResources &resources)
    : synthesizer_(resources.synthesizer),
      sampler_(resources.sampler)
{
}

GenerationResult SingleLineGenerationTask::executeTask()
{
    const int target = randInt(
        synthesizer_->getConfig()["text_sampler"].value("min_targets", 5),
        synthesizer_->getConfig()["text_sampler"].value("max_targets", 50));

    const std::string text = sampler_->generateString(target);
    SingleLineImageResult imageResult = synthesizer_->generateSingleImage(text);

    std::vector<uchar> encoded;
    cv::imencode(".png", imageResult.image, encoded, {cv::IMWRITE_PNG_COMPRESSION, 3});

    GenerationResult result;
    result.encodedData = std::move(encoded);
    result.json_string = synthesizer_->makeJsonRecord(
        "",
        imageResult.text,
        imageResult.width,
        imageResult.height,
        imageResult.vertical);
    return result;
}