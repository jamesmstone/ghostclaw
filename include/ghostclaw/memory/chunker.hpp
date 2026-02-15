#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghostclaw::memory {

struct TextChunk {
  std::string content;
  std::optional<std::string> heading;
  std::size_t start_offset = 0;
  std::size_t end_offset = 0;
};

[[nodiscard]] std::vector<TextChunk> chunk_text(std::string_view text,
                                                std::size_t max_chunk_size = 512,
                                                std::size_t overlap = 50);

} // namespace ghostclaw::memory
