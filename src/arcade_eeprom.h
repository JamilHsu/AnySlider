#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <span>
#include <string>

namespace anyslider::arcade_eeprom
{
// AFT SBZV CalibData: CRC32 + 60-byte payload, mirrored motherboard EEPROM records.
constexpr unsigned CalibMainOffset = 0x1080, CalibBackupOffset = 0x1180;
using Record = std::array<uint8_t, 64>;

struct AxisPoint
{
    int16_t raw = 0, target = 0;
};

struct TouchCalibration
{
    std::array<AxisPoint, 5> x{}, y{};
    bool valid = false;
    float MapX(uint16_t raw) const;
    float MapY(uint16_t raw) const;
};

struct TargetRatio
{
    float x = 0.0f, y = 0.0f;
};

// Verified standard AFT SBZV target ratios (Center, TopLeft, TopRight, BottomRight, BottomLeft)
// Disassembled from diva.exe 0x140A91D78:
// Point 0: Center (0.50, 0.50)
// Point 1: TopLeft (0.25, 0.25)
// Point 2: TopRight (0.75, 0.05)
// Point 3: BottomRight (0.95, 0.75)
// Point 4: BottomLeft (0.05, 0.95)
constexpr std::array<TargetRatio, 5> StandardTargetRatios = {{
    {0.50f, 0.50f},
    {0.25f, 0.25f},
    {0.75f, 0.05f},
    {0.95f, 0.75f},
    {0.05f, 0.95f}
}};

struct SamplePoint
{
    int16_t rawX = 0, rawY = 0;
};

inline uint32_t Crc32(std::span<const uint8_t> bytes)
{
    uint32_t crc = ~uint32_t(0);
    for (auto byte : bytes)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        }
    }
    return ~crc;
}

inline uint16_t Le16(const uint8_t *p)
{
    return uint16_t(p[0] | (p[1] << 8));
}

inline void Le16Set(uint8_t *p, uint16_t v)
{
    p[0] = uint8_t(v & 0xFF);
    p[1] = uint8_t((v >> 8) & 0xFF);
}

inline bool DecodeCalibration(const Record &record, TouchCalibration &output)
{
    const uint32_t stored = uint32_t(record[0]) | (uint32_t(record[1]) << 8) | (uint32_t(record[2]) << 16) |
                            (uint32_t(record[3]) << 24);
    if (stored != Crc32(std::span(record).subspan(4)))
    {
        return false;
    }
    TouchCalibration candidate;
    for (size_t i = 0; i < 5; ++i)
    {
        const auto *p = record.data() + 4 + i * 8;
        candidate.x[i] = {int16_t(Le16(p)), int16_t(Le16(p + 2))};
        candidate.y[i] = {int16_t(Le16(p + 4)), int16_t(Le16(p + 6))};
    }
    // Saved AFT tables are already sorted/extrapolated. Reject malformed/other-game data.
    for (const auto &axis : {candidate.x, candidate.y})
    {
        if (axis.front().target != 0 || axis.back().target <= 0)
        {
            return false;
        }
        for (size_t i = 0; i < 5; ++i)
        {
            if (axis[i].raw < 0 || axis[i].raw > 4095 || axis[i].target < 0)
            {
                return false;
            }
            if (i && (axis[i].raw <= axis[i - 1].raw || axis[i].target <= axis[i - 1].target))
            {
                return false;
            }
        }
    }
    candidate.valid = true;
    output = candidate;
    return true;
}

// Encodes TouchCalibration into a 64-byte Record (AFT SBZV format):
// Bytes 0..3: CRC32 of bytes 4..63
// Bytes 4..43: 40 bytes (5 pairs of X and Y control points)
// Bytes 44..63: 20 bytes unexplained payload (preserved or zeroed)
inline void EncodeCalibration(const TouchCalibration &calib, Record &record,
                              std::span<const uint8_t> preservedPayload = {})
{
    for (size_t i = 0; i < 5; ++i)
    {
        auto *p = record.data() + 4 + i * 8;
        Le16Set(p, uint16_t(calib.x[i].raw));
        Le16Set(p + 2, uint16_t(calib.x[i].target));
        Le16Set(p + 4, uint16_t(calib.y[i].raw));
        Le16Set(p + 6, uint16_t(calib.y[i].target));
    }
    if (!preservedPayload.empty())
    {
        const size_t copyLen = std::min<size_t>(20, preservedPayload.size());
        std::copy_n(preservedPayload.data(), copyLen, record.data() + 44);
        if (copyLen < 20)
        {
            std::fill_n(record.data() + 44 + copyLen, 20 - copyLen, uint8_t(0));
        }
    }
    else
    {
        std::fill_n(record.data() + 44, 20, uint8_t(0));
    }
    const uint32_t crc = Crc32(std::span(record).subspan(4));
    record[0] = uint8_t(crc & 0xFF);
    record[1] = uint8_t((crc >> 8) & 0xFF);
    record[2] = uint8_t((crc >> 16) & 0xFF);
    record[3] = uint8_t((crc >> 24) & 0xFF);
}

inline float MapAxis(const std::array<AxisPoint, 5> &axis, uint16_t raw)
{
    // Match AFT 6928F0: raw is inverted first, choose bracketing points, integer pixel interpolation.
    const int value = 4095 - std::min<int>(4095, raw);
    auto low = axis.front(), high = axis.back();
    for (size_t i = 1; i < 4; ++i)
    {
        if (axis[i].raw > low.raw && axis[i].raw < value)
        {
            low = axis[i];
        }
        if (axis[i].raw < high.raw && axis[i].raw > value)
        {
            high = axis[i];
        }
    }
    const int target =
        int(float(value - low.raw) * float(high.target - low.target) / float(high.raw - low.raw) +
            float(low.target));
    return float(std::clamp(target, 0, int(axis.back().target))) / float(axis.back().target);
}

inline float TouchCalibration::MapX(uint16_t raw) const
{
    return MapAxis(x, raw);
}

inline float TouchCalibration::MapY(uint16_t raw) const
{
    return MapAxis(y, raw);
}

// Extrapolation helper matching AFT sub_140692890
inline int ExtrapolateRaw(int16_t raw1, int16_t target1, int16_t raw2, int16_t target2, int16_t targetReq)
{
    const int targetDiff = int(target2) - int(target1);
    if (targetDiff != 0)
    {
        const float f = float(int(targetReq) - int(target1)) * float(int(raw2) - int(raw1)) / float(targetDiff) + float(raw1);
        return int(f);
    }
    return int(raw1);
}

// Generates an AFT-compliant TouchCalibration table from 5 sampled points:
// 1. Assigns target coordinates: target = ratio * extent.
// 2. Sorts X pairs by targetX ascending, and Y pairs by targetY ascending (verified from AFT sub_140693520).
// 3. Extrapolates index 0 to target = 0 using points [0] and [1], clamped to [0, 4095].
// 4. Extrapolates index 4 to target = extent using points [3] and [4], clamped to [0, 4095].
// 5. Validates monotonicity and returns valid = true.
// Note: If rawAlreadyInverted is false, raw input is converted via (4095 - raw) to match AFT internal raw representation.
inline bool GenerateCalibration(
    const std::array<SamplePoint, 5> &samples,
    int16_t extentX,
    int16_t extentY,
    TouchCalibration &output,
    bool rawAlreadyInverted = true,
    const std::array<TargetRatio, 5> &ratios = StandardTargetRatios)
{
    output = {};
    if (extentX <= 0 || extentY <= 0)
    {
        return false;
    }

    std::array<AxisPoint, 5> xPoints{}, yPoints{};
    for (size_t i = 0; i < 5; ++i)
    {
        if (samples[i].rawX < 0 || samples[i].rawX > 4095 ||
            samples[i].rawY < 0 || samples[i].rawY > 4095 ||
            !std::isfinite(ratios[i].x) || !std::isfinite(ratios[i].y) ||
            ratios[i].x < 0 || ratios[i].x > 1 || ratios[i].y < 0 || ratios[i].y > 1)
            return false;
        int rawX = rawAlreadyInverted ? samples[i].rawX : (4095 - samples[i].rawX);
        int rawY = rawAlreadyInverted ? samples[i].rawY : (4095 - samples[i].rawY);
        rawX = std::clamp(rawX, 0, 4095);
        rawY = std::clamp(rawY, 0, 4095);

        const int targetX = std::clamp(int(ratios[i].x * float(extentX)), 0, int(extentX));
        const int targetY = std::clamp(int(ratios[i].y * float(extentY)), 0, int(extentY));

        xPoints[i] = {int16_t(rawX), int16_t(targetX)};
        yPoints[i] = {int16_t(rawY), int16_t(targetY)};
    }

    // AFT sub_140693520 sorts each axis by target ascending
    std::sort(xPoints.begin(), xPoints.end(), [](const AxisPoint &a, const AxisPoint &b) {
        return a.target < b.target;
    });
    std::sort(yPoints.begin(), yPoints.end(), [](const AxisPoint &a, const AxisPoint &b) {
        return a.target < b.target;
    });

    // Extrapolate low boundary (target = 0) from points 0 and 1
    const int extRawX0 = ExtrapolateRaw(xPoints[0].raw, xPoints[0].target, xPoints[1].raw, xPoints[1].target, 0);
    xPoints[0].raw = int16_t(std::clamp(extRawX0, 0, 4095));
    xPoints[0].target = 0;

    const int extRawY0 = ExtrapolateRaw(yPoints[0].raw, yPoints[0].target, yPoints[1].raw, yPoints[1].target, 0);
    yPoints[0].raw = int16_t(std::clamp(extRawY0, 0, 4095));
    yPoints[0].target = 0;

    // Extrapolate high boundary (target = extent) from points 3 and 4
    const int extRawX4 = ExtrapolateRaw(xPoints[3].raw, xPoints[3].target, xPoints[4].raw, xPoints[4].target, extentX);
    xPoints[4].raw = int16_t(std::clamp(extRawX4, 0, 4095));
    xPoints[4].target = extentX;

    const int extRawY4 = ExtrapolateRaw(yPoints[3].raw, yPoints[3].target, yPoints[4].raw, yPoints[4].target, extentY);
    yPoints[4].raw = int16_t(std::clamp(extRawY4, 0, 4095));
    yPoints[4].target = extentY;

    // Strict validation matching AFT DecodeCalibration
    for (const auto &axis : {xPoints, yPoints})
    {
        if (axis.front().target != 0 || axis.back().target <= 0)
        {
            return false;
        }
        for (size_t i = 0; i < 5; ++i)
        {
            if (axis[i].raw < 0 || axis[i].raw > 4095 || axis[i].target < 0)
            {
                return false;
            }
            if (i && (axis[i].raw <= axis[i - 1].raw || axis[i].target <= axis[i - 1].target))
            {
                return false;
            }
        }
    }

    output.x = xPoints;
    output.y = yPoints;
    output.valid = true;
    return true;
}

enum class CalibrationFileFormat
{
    Auto,
    Binary64,
    Text
};

// Logs full calibration table to regular Log (source, native extent, 5 axis control point pairs)
void LogTouchCalibration(const TouchCalibration &calib, const char *source = "EEPROM");

// Loads calibration from an offline file (64-byte AFT record or human-readable text format)
bool LoadCalibrationFromFile(
    const std::string &path,
    TouchCalibration &output,
    CalibrationFileFormat format = CalibrationFileFormat::Auto,
    std::array<uint8_t, 20> *preservedPayload = nullptr);

// Saves calibration to an offline file (64-byte AFT record or human-readable text format)
bool SaveCalibrationToFile(
    const std::string &path,
    const TouchCalibration &calib,
    CalibrationFileFormat format = CalibrationFileFormat::Auto,
    std::span<const uint8_t> preservedPayload = {});

// Read only. Never initialize, clear, repair or write motherboard backup data.
bool ReadTouchCalibration(TouchCalibration &output);

} // namespace anyslider::arcade_eeprom
