#pragma once

#include "sgf_exceptions.hpp"
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

enum class SGFTokenType : int {
    LEFT_PAREN,
    RIGHT_PAREN,
    SEMICOLON,
    TAG,
    VALUE,
    IGNORE,
    ENDOFFILE,
    NONE,
};

class SGFToken {
public:
    SGFToken(SGFTokenType type, const std::string& value, int start, int end)
        : type(type), value(value), start(start), end(end) {}

    SGFTokenType type;
    std::string value;
    size_t start;
    size_t end;
};

class BaseInputStream {
public:
    virtual ~BaseInputStream() = default;
    virtual char peek() = 0;
    virtual char get() = 0;
    virtual void unget() = 0;
    virtual int tellg() = 0;
};

class FileInputStream : public BaseInputStream {
public:
    explicit FileInputStream(const std::string& filename)
        : file(filename)
    {
        if (!file) {
            throw std::invalid_argument("Cannot open file: " + filename);
        }
    }

    void close()
    {
        file.close();
    }

    char peek() override
    {
        return file.peek();
    }

    char get() override
    {
        char c = file.get();
        if (file.eof()) {
            return '\0';
        }
        return c;
    }

    void unget() override
    {
        file.unget();
    }

    int tellg() override
    {
        return file.tellg();
    }

private:
    std::ifstream file;
};

class StringInputStream : public BaseInputStream {
public:
    explicit StringInputStream(const std::string& s)
        : s(s), index(0) {}

    char peek() override
    {
        if (index >= s.length()) {
            return '\0';
        }
        return s[index];
    }

    char get() override
    {
        if (index >= s.length()) {
            return '\0';
        }
        return s[index++];
    }

    void unget() override
    {
        if (index > 0) {
            --index;
        }
    }

    int tellg() override
    {
        return index;
    }

private:
    std::string s;
    size_t index;
};

class SGFLexer {
public:
    SGFLexer(BaseInputStream& input_stream, size_t start = 0, size_t length = 0, std::function<void(size_t, size_t)> progress_callback = nullptr)
        : length_(length), input_stream_(input_stream), last_token_(SGFTokenType::NONE, "", start, start), progress_callback_(std::move(progress_callback)) {}

    const SGFToken& nextToken()
    {
        _nextToken();
        if (last_token_.type != SGFTokenType::ENDOFFILE && progress_callback_) {
            progress_callback_(input_stream_.tellg(), length_);
        }
        return last_token_;
    }

    const SGFToken& currentToken() const
    {
        return last_token_;
    }

private:
    void _nextToken()
    {
        while (true) {
            char c = input_stream_.get();
            if (c == '\0') {
                last_token_ = SGFToken(SGFTokenType::ENDOFFILE, "", input_stream_.tellg(), input_stream_.tellg());
                return;
            }
            if (c == '(') {
                last_token_ = SGFToken(SGFTokenType::LEFT_PAREN, std::string(1, c), input_stream_.tellg() - 1, input_stream_.tellg());
                return;
            }
            if (c == ')') {
                last_token_ = SGFToken(SGFTokenType::RIGHT_PAREN, std::string(1, c), input_stream_.tellg() - 1, input_stream_.tellg());
                return;
            }
            if (c == ';') {
                last_token_ = SGFToken(SGFTokenType::SEMICOLON, std::string(1, c), input_stream_.tellg() - 1, input_stream_.tellg());
                return;
            }
            if (c == '[') {
                std::string value;
                bool escape = false;
                while (true) {
                    c = input_stream_.get();
                    if (c == '\0') {
                        throw LexicalError("Unexpected end of file", input_stream_.tellg(), input_stream_.tellg());
                    }
                    if (c == ']' && !escape) {
                        break;
                    }
                    if (c == '\\' && !escape) {
                        value += c; // Add the escape character
                        escape = true;
                        continue;
                    }
                    value += c;
                    escape = false;
                }
                last_token_ = SGFToken(SGFTokenType::VALUE, value, input_stream_.tellg() - value.size() - 1, input_stream_.tellg());
                return;
            }
            if (isAlnum(c) || c == '_') {
                std::string tag(1, c);
                while (true) {
                    c = input_stream_.peek();
                    if (c == '\0' || !isAlnum(c) && c != '_') {
                        break;
                    }
                    tag += input_stream_.get();
                }
                last_token_ = SGFToken(SGFTokenType::TAG, tag, input_stream_.tellg() - tag.size(), input_stream_.tellg());
                return;
            }
            if (isspace(c)) {
                continue; // Skip whitespace
            }
            throw LexicalError("Invalid character", input_stream_.tellg() - 1, input_stream_.tellg());
        }
    }

    static bool isAlnum(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    size_t length_;
    BaseInputStream& input_stream_;
    SGFToken last_token_;
    std::function<void(size_t, size_t)> progress_callback_;
};
