#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace anyslider::elo
{
using Packet = std::array<uint8_t, 10>;
inline Packet Encode(uint8_t command, std::span<const uint8_t> data = {})
{
    Packet p{};
    p[0] = 0x55;
    p[1] = command;
    std::copy_n(data.begin(), std::min<size_t>(7, data.size()), p.begin() + 2);
    p[9] = 0xAA;
    for (size_t i = 0; i < 9; ++i)
    {
        p[9] += p[i];
    }
    return p;
}
class Decoder
{
    std::vector<uint8_t> pending_;

  public:
    std::vector<Packet> Feed(std::span<const uint8_t> bytes)
    {
        std::vector<Packet> result;
        // Process incrementally: cache stays below ten bytes even with corrupt input.
        for (uint8_t b : bytes)
        {
            pending_.push_back(b);
            while (!pending_.empty())
            {
                if (pending_[0] != 0x55)
                {
                    pending_.erase(pending_.begin());
                    continue;
                }
                if (pending_.size() < 10)
                {
                    break;
                }
                uint8_t sum = 0xAA;
                for (size_t i = 0; i < 9; ++i)
                {
                    sum += pending_[i];
                }
                if (sum != pending_[9])
                {
                    pending_.erase(pending_.begin());
                    continue;
                }
                Packet p{};
                std::copy_n(pending_.begin(), 10, p.begin());
                result.push_back(p);
                pending_.clear();
            }
        }
        return result;
    }
};
struct Event
{
    uint16_t x = 0, y = 0;
    bool down = false;
    bool initial = false;
};
inline bool DecodeTouch(const Packet &p, Event &event)
{
    if (p[1] != 'T' || !(p[2] & 7))
    {
        return false;
    }
    event.x = std::min<unsigned>(4095, p[3] | (p[4] << 8));
    event.y = std::min<unsigned>(4095, p[5] | (p[6] << 8));
    event.down = !(p[2] & 4);
    event.initial = (p[2] & 1) != 0;
    return true;
}
inline float Normalize(uint16_t value, int low, int high, bool invert)
{
    float v = std::clamp(float(int(value) - low) / float(high - low), 0.0f, 1.0f);
    return invert ? 1.0f - v : v;
}
} // namespace anyslider::elo
