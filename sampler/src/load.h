#pragma once

#include <filesystem>
#include <vector>

namespace sats {
void loadSentinelProductZips(const std::vector<std::filesystem::path> &paths,
                             const std::vector<std::string> &bands,
                             const std::vector<std::string> &qaBands);
}
