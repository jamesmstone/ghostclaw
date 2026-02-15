#include "ghostclaw/memory/chunker.hpp"

#include "ghostclaw/common/fs.hpp"

#include <sstream>

namespace ghostclaw::memory {

namespace {

std::vector<std::string> split_paragraphs(const std::string &text) {
  std::vector<std::string> paragraphs;
  std::string current;

  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) {
      if (!common::trim(current).empty()) {
        paragraphs.push_back(common::trim(current));
        current.clear();
      }
      continue;
    }

    if (!current.empty()) {
      current += '\n';
    }
    current += line;
  }

  if (!common::trim(current).empty()) {
    paragraphs.push_back(common::trim(current));
  }

  return paragraphs;
}

std::vector<std::string> split_sentences(const std::string &text) {
  std::vector<std::string> sentences;
  std::string current;
  for (char ch : text) {
    current.push_back(ch);
    if (ch == '.' || ch == '!' || ch == '?') {
      if (!common::trim(current).empty()) {
        sentences.push_back(common::trim(current));
      }
      current.clear();
    }
  }
  if (!common::trim(current).empty()) {
    sentences.push_back(common::trim(current));
  }
  return sentences;
}

std::vector<std::string> split_words(const std::string &text, const std::size_t max_size) {
  std::vector<std::string> chunks;
  std::istringstream stream(text);
  std::string word;
  std::string current;

  while (stream >> word) {
    if (current.size() + word.size() + 1 > max_size && !current.empty()) {
      chunks.push_back(current);
      current.clear();
    }

    if (!current.empty()) {
      current += ' ';
    }
    current += word;
  }

  if (!current.empty()) {
    chunks.push_back(current);
  }
  return chunks;
}

} // namespace

std::vector<TextChunk> chunk_text(const std::string_view text, const std::size_t max_chunk_size,
                                  const std::size_t overlap) {
  const std::string input(text);
  std::vector<TextChunk> chunks;

  auto paragraphs = split_paragraphs(input);
  std::optional<std::string> current_heading;
  std::size_t offset = 0;

  for (const auto &paragraph : paragraphs) {
    if (common::starts_with(paragraph, "#")) {
      current_heading = paragraph;
    }

    auto emit_chunk = [&](const std::string &content) {
      TextChunk chunk;
      chunk.heading = current_heading;
      chunk.content = content;
      if (chunk.heading.has_value()) {
        chunk.content = *chunk.heading + "\n" + content;
      }
      chunk.start_offset = offset;
      chunk.end_offset = offset + content.size();
      chunks.push_back(std::move(chunk));
      offset += content.size() > overlap ? content.size() - overlap : content.size();
    };

    if (paragraph.size() <= max_chunk_size) {
      emit_chunk(paragraph);
      continue;
    }

    auto sentences = split_sentences(paragraph);
    std::string current;

    for (const auto &sentence : sentences) {
      if (sentence.size() > max_chunk_size) {
        auto words = split_words(sentence, max_chunk_size);
        for (const auto &word_chunk : words) {
          if (!current.empty()) {
            emit_chunk(current);
            current.clear();
          }
          emit_chunk(word_chunk);
        }
        continue;
      }

      if (current.size() + sentence.size() + 1 > max_chunk_size && !current.empty()) {
        emit_chunk(current);
        current.clear();
      }

      if (!current.empty()) {
        current += ' ';
      }
      current += sentence;
    }

    if (!current.empty()) {
      emit_chunk(current);
    }
  }

  if (chunks.empty()) {
    chunks.push_back(TextChunk{.content = input, .start_offset = 0, .end_offset = input.size()});
  }

  return chunks;
}

} // namespace ghostclaw::memory
