#include "text_synth/writer.hpp"

#include <stdexcept>

JsonlWriter::JsonlWriter(const std::string &path, int total, size_t batchSize)
    : batchSize_(batchSize), total_(total)
{
    file_.open(path, std::ios::out | std::ios::app);
    if (!file_.is_open())
    {
        throw std::runtime_error("JsonlWriter: cannot open file: " + path);
    }
    buffer_.reserve(batchSize_);

    using namespace indicators;
    bar_.set_option(option::BarWidth{50});
    bar_.set_option(option::Start{"<"});
    bar_.set_option(option::Fill{"-"});
    bar_.set_option(option::Lead{"-"});
    bar_.set_option(option::Remainder{" "});
    bar_.set_option(option::End{">"});
    bar_.set_option(option::PrefixText{"Generating "});
    bar_.set_option(option::ForegroundColor{Color::green});
    bar_.set_option(option::ShowPercentage{true});
    bar_.set_option(option::ShowElapsedTime{true});
    bar_.set_option(option::ShowRemainingTime{true});
    bar_.set_option(option::FontStyles{std::vector<FontStyle>{FontStyle::bold}});
}

JsonlWriter::~JsonlWriter()
{
    try
    {
        flush();
    }
    catch (...)
    {
    }
}

void JsonlWriter::write(const std::string &jsonLine)
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.push_back(jsonLine);
    if (buffer_.size() >= batchSize_)
    {
        flushLocked();
    }
}

void JsonlWriter::flush()
{
    std::lock_guard<std::mutex> lock(mutex_);
    flushLocked();
}

void JsonlWriter::flushLocked()
{
    if (buffer_.empty())
    {
        return;
    }

    for (const auto &line : buffer_)
    {
        file_ << line << '\n';
    }
    file_.flush();

    written_ += static_cast<int>(buffer_.size());
    buffer_.clear();

    if (total_ > 0)
    {
        const float progress = static_cast<float>(written_) / static_cast<float>(total_) * 100.0f;
        bar_.set_progress(progress);
        if (written_ >= total_)
        {
            bar_.mark_as_completed();
        }
    }
}