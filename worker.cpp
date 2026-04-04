#include "worker.hpp"
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_set>
#include "utils.hpp"
#include "writer.hpp"
#include "renderer.hpp"
#include "textsampler.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

BlockingQueue::BlockingQueue(size_t capacity) : capacity_(capacity), closed_(false) {}

bool BlockingQueue::push(WriteTask &&item) {
    std::unique_lock<std::mutex> lock(mutex_);
    notFull_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
    if (closed_) return false;
    queue_.push_back(std::move(item));
    notEmpty_.notify_one();
    return true;
}

std::optional<WriteTask> BlockingQueue::pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    notEmpty_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return std::nullopt;
    WriteTask item = std::move(queue_.front());
    queue_.pop_front();
    notFull_.notify_one();
    return item;
}

void BlockingQueue::close() {
    std::unique_lock<std::mutex> lock(mutex_);
    closed_ = true;
    notEmpty_.notify_all();
    notFull_.notify_all();
}

void workerTask(int numToGen,
                TextSampler &sampler,
                const json &imgCfg,
                const std::vector<SharedFontMeta> &defaultMeta,
                const std::vector<SharedFontMeta> &fallbackMeta,
                GlyphCache &glyphCache,
                SharedBgResources &bgRes,
                BlockingQueue &ioQueue,
                const json &config,
                std::atomic<int64_t> &globalIndex,
                const std::vector<int64_t> &hierLevels)
{
    // Create a thread-local Renderer with its own FT_Library / FT_Face instances.
    Renderer renderer(imgCfg, defaultMeta, fallbackMeta, glyphCache, bgRes);

    const json &genCfg = config["generate"];
    int minTargets = genCfg["min_targets"].get<int>();
    int maxTargets = genCfg["max_targets"].get<int>();
    int outputHeight = genCfg["output_height"].get<int>();

    int fontSize = imgCfg.value("font_size", 55);
    double scaleMin = imgCfg.contains("scale_range") ? imgCfg["scale_range"][0].get<double>() : 1.0;
    double scaleMax = imgCfg.contains("scale_range") ? imgCfg["scale_range"][1].get<double>() : 1.0;
    bool recomputeWidth = imgCfg.value("recompute_width", false);
    int mMin = imgCfg["margin_range"][0].get<int>();
    int mMax = imgCfg["margin_range"][1].get<int>();
    double offsetProb = imgCfg.value("offset_prob", 0.0);
    int hOffMin = imgCfg.contains("h_offset_range") ? imgCfg["h_offset_range"][0].get<int>() : 0;
    int hOffMax = imgCfg.contains("h_offset_range") ? imgCfg["h_offset_range"][1].get<int>() : 0;
    int vOffMin = imgCfg.contains("v_offset_range") ? imgCfg["v_offset_range"][0].get<int>() : 0;
    int vOffMax = imgCfg.contains("v_offset_range") ? imgCfg["v_offset_range"][1].get<int>() : 0;

    for (int i = 0; i < numToGen; i++)
    {
        int target = randInt(minTargets, maxTargets);
        std::string text = sampler.generateString(target);

        BgInfo bgInfo = renderer.getRandomBgPredict();
        cv::Vec3b approxColor = renderer.getBgApproxColor(bgInfo);

        cv::Mat textImg = renderer.renderTightText(text, approxColor);
        if (textImg.empty())
        {
            i--; // Retry
            continue;
        }

        int origW = textImg.cols;
        int origH = textImg.rows;
        double fontScale = (double)origH / fontSize;

        double scale = randDouble(scaleMin, scaleMax);
        if (std::abs(scale - 1.0) > 1e-4)
        {
            int nw = (int)(origW * scale), nh = (int)(origH * scale);
            cv::resize(textImg, textImg, cv::Size(nw, nh), 0, 0, cv::INTER_CUBIC);
        }

        int scaledW = textImg.cols;
        int scaledH = textImg.rows;

        int baseCw = recomputeWidth ? scaledW : origW;
        int baseCh = origH;

        int marginX = (int)(randInt(mMin, mMax) * fontScale);
        int marginY = (int)(randInt(mMin, mMax) * fontScale);

        int cw = baseCw + marginX + marginY;
        int ch = baseCh;

        int drawX = marginX + (baseCw - scaledW) / 2;
        int drawY = (baseCh - scaledH) / 2;

        drawX += (int)(randInt(-5, 5) * fontScale);
        drawY += (int)(randInt(-3, 3) * fontScale);

        if (randDouble(0, 1) < offsetProb)
        {
            drawX += (int)(randInt(hOffMin, hOffMax) * fontScale);
            drawY += (int)(randInt(vOffMin, vOffMax) * fontScale);
        }

        cw = std::max(10, cw);
        ch = std::max(10, ch);

        auto [finalBg, _unused] = renderer.getBgCropAndColor(bgInfo, cw, ch);
        cv::Mat finalBgOut = finalBg;
        for (int r = 0; r < textImg.rows; r++)
        {
            const cv::Vec4b *srcRow = textImg.ptr<cv::Vec4b>(r);
            int dy = drawY + r;
            if (dy < 0 || dy >= finalBgOut.rows)
                continue;
            cv::Vec3b *dstRow = finalBgOut.ptr<cv::Vec3b>(dy);
            for (int c = 0; c < textImg.cols; c++)
            {
                int dx = drawX + c;
                if (dx < 0 || dx >= finalBgOut.cols)
                    continue;
                const cv::Vec4b &src = srcRow[c];
                uint8_t alpha = src[3];
                if (alpha == 0)
                    continue;
                cv::Vec3b &dst = dstRow[dx];
                float a = alpha / 255.0f;
                dst[0] = (uint8_t)(src[0] * a + dst[0] * (1 - a));
                dst[1] = (uint8_t)(src[1] * a + dst[1] * (1 - a));
                dst[2] = (uint8_t)(src[2] * a + dst[2] * (1 - a));
            }
        }

        int outW = (int)((double)finalBgOut.cols * outputHeight / finalBgOut.rows);
        cv::Mat finalImg;
        cv::resize(finalBgOut, finalImg, cv::Size(outW, outputHeight), 0, 0, cv::INTER_CUBIC);

        // Parallel image encoding: move the CPU-intensive compression to the worker thread.
        std::vector<uchar> encoded;
        cv::imencode(".png", finalImg, encoded, {cv::IMWRITE_PNG_COMPRESSION, 3});

        int64_t idx = globalIndex.fetch_add(1, std::memory_order_relaxed);
        std::string relPath = indexToHierarchicalPath(idx, hierLevels);

        ioQueue.push({std::move(encoded), std::move(relPath), std::move(text), finalImg.cols});
    }
}

void ioWorker(BlockingQueue &ioQueue,
              JsonlWriter &writer,
              const std::string &outDir)
{
    std::unordered_set<std::string> createdDirs;
    while (true)
    {
        auto maybeTask = ioQueue.pop();
        if (!maybeTask) break;

        WriteTask &task = *maybeTask;
        fs::path fullPath = fs::path(outDir) / task.relPath;
        std::string parentDir = fullPath.parent_path().string();

        // Optimization: avoid redundant directory creation syscalls.
        if (createdDirs.find(parentDir) == createdDirs.end()) {
            fs::create_directories(fullPath.parent_path());
            createdDirs.insert(parentDir);
        }

        // Fast binary write of pre-encoded data.
        std::ofstream f(fullPath.string(), std::ios::binary);
        if (f) {
            f.write(reinterpret_cast<const char*>(task.encodedData.data()), task.encodedData.size());
        }
        
        writer.write(task.relPath, task.text, task.width);
    }
}