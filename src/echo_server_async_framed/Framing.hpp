#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <winsock2.h>
#include <queue>
#include <cstdint>


struct Frame {
    static constexpr uint32_t MAX_FRAME_SIZE = 1048576;
    static constexpr uint32_t HEADER_LEN = sizeof(uint32_t);

    uint32_t length;              // header (message length)
    std::unique_ptr<char[]> data; // message

    Frame() = default;

    Frame(const char* src, uint32_t len): length(len), data(std::make_unique<char[]>(len)) {
        memcpy(data.get(), src, len);
    }

    Frame(Frame&&) = default;
    Frame& operator=(Frame&&) = default;

    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

};


class FrameDecoder {

    public:

        void feed(const char* data, size_t len) {
            buffer.insert(buffer.end(), data, data + len);
        }

        bool nextFrame(Frame& outFrame) {
            if (buffer.size() < Frame::HEADER_LEN) return false;

            uint32_t len = 0;
            memcpy(&len, buffer.data(), Frame::HEADER_LEN);
            len = ntohl(len);
            
            if (len > Frame::MAX_FRAME_SIZE) return false;
            if (buffer.size() < Frame::HEADER_LEN + len) return false;

            Frame tmp = Frame(buffer.data() + Frame::HEADER_LEN, len);
            outFrame = std::move(tmp);
            buffer.erase(buffer.begin(), buffer.begin() + Frame::HEADER_LEN + len);
            return true;
        }

    private:
        std::vector<char> buffer;
};


class FrameEncoder {
public:
    FrameEncoder() {}

    void feed(const char* data, size_t len) {
        std::vector<char> frame(Frame::HEADER_LEN + len);
        uint32_t netLen = htonl(static_cast<uint32_t>(len));
        memcpy(frame.data(), &netLen, Frame::HEADER_LEN);
        memcpy(frame.data() + Frame::HEADER_LEN, data, len);
        frames.push(std::move(frame));
    }

    bool hasNext() const {
        return !frames.empty();
    }

    std::vector<char> next() {
        std::vector<char> out = std::move(frames.front());
        frames.pop();
        return out;
    }

private:
    std::queue<std::vector<char>> frames;
};