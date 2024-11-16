#pragma once

#include <stdexcept>
#include <string>

class BaseSGFException : public std::runtime_error {
public:
    BaseSGFException(const std::string& message, int start, int end, bool detail = false, const std::string& sgf = "", int offset = 20, const std::string& highlight_start = "\033[1;31m", const std::string& highlight_end = "\033[0m")
        : std::runtime_error([&]() {
              if (!detail) {
                  return message + " at " + std::to_string(start) + ":" + std::to_string(end);
              } else {
                  if (sgf.empty()) {
                      return message + " at " + std::to_string(start) + ":" + std::to_string(end);
                  }
                  int s = std::max(0, start - offset);
                  int e = std::min(static_cast<int>(sgf.length()), end + offset);
                  return message + " at " + std::to_string(start) + ":" + std::to_string(end) + "\n" + sgf.substr(s, start - s) + highlight_start + sgf.substr(start, end - start) + highlight_end + sgf.substr(end, e - end);
              }
          }()) {}
};

class LexicalError : public BaseSGFException {
public:
    LexicalError(const std::string& message, int start, int end, bool detail = false, const std::string& sgf = "", int offset = 20, const std::string& highlight_start = "\033[1;31m", const std::string& highlight_end = "\033[0m")
        : BaseSGFException(message, start, end, detail, sgf, offset, highlight_start, highlight_end) {}
};

class SGFError : public BaseSGFException {
public:
    SGFError(const std::string& message, int start, int end, bool detail = false, const std::string& sgf = "", int offset = 20, const std::string& highlight_start = "\033[1;31m", const std::string& highlight_end = "\033[0m")
        : BaseSGFException(message, start, end, detail, sgf, offset, highlight_start, highlight_end) {}
};
