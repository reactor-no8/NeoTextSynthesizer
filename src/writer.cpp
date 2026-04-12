#include "writer.hpp"
#include <stdexcept>
#include <sstream>

// Escape a UTF-8 string for safe embedding inside a JSON double-quoted value.
// Handles the mandatory JSON escapes: ", \, and control characters.
static std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            // Pass through all other bytes, including valid UTF-8 multibyte sequences
            out += (char)c;
            break;
        }
    }
    return out;
}

JsonlWriter::JsonlWriter(const std::string &path, int total, size_t batchSize)
    : batchSize_(batchSize), total_(total)
{
    file_.open(path, std::ios::out | std::ios::app);
    if (!file_.is_open())
        throw std::runtime_error("JsonlWriter: cannot open file: " + path);
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
    // Best-effort flush on destruction; swallow exceptions to stay noexcept.
    try
    {
        flush();
    }
    catch (...)
    {
    }
}

std::string JsonlWriter::makeJsonLine(const std::string &image,
                                      const std::string &text,
                                      int length)
{
    // Produces: {"image":"<image>","text":"<text>","length":<length>}
    std::ostringstream ss;
    ss << "{\"image\":\"" << jsonEscape(image)
       << "\",\"text\":\"" << jsonEscape(text)
       << "\",\"length\":" << length << "}";
    return ss.str();
}

void JsonlWriter::write(const std::string &image,
                        const std::string &text,
                        int length)
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.push_back(makeJsonLine(image, text, length));
    if (buffer_.size() >= batchSize_)
        flushLocked();
}

void JsonlWriter::flush()
{
    std::lock_guard<std::mutex> lock(mutex_);
    flushLocked();
}

void JsonlWriter::flushLocked()
{
    if (buffer_.empty())
        return;
    for (const auto &line : buffer_)
        file_ << line << '\n';
    file_.flush();
    written_ += buffer_.size();
    buffer_.clear();

    if (total_ > 0)
    {
        float progress = (float)written_ / total_ * 100.0f;
        bar_.set_progress(progress);
        if (written_ >= total_)
        {
            bar_.mark_as_completed();
        }
    }
}
