#include <windows.h>
#include <setupapi.h>
#include "arcade_eeprom.h"
#include "anyslider_log.h"
#include "arcade_overlapped_io.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace anyslider::arcade_eeprom
{
namespace
{
const GUID EepromGuid{0xB7970F0C, 0x31C4, 0x45FF, {0x96, 0x18, 0x0A, 0x24, 0x00, 0x94, 0xB2, 0x71}};

struct Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle()
    {
        if (value && value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value);
        }
    }
};

bool ReadRecord(Handle &device, unsigned offset, Record &record)
{
    DWORD count = 0;
    bool retained = false;
    const bool ok = ArcadeTransfer(device.value, true, record.data(), DWORD(record.size()), offset, 1000,
                                   count, retained);
    if (retained)
    {
        device.value = INVALID_HANDLE_VALUE;
        Log("EEPROM: cancellation stalled; device quarantined until process exit");
    }
    return ok && count == record.size();
}

std::string Trim(const std::string &s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

} // namespace

void LogTouchCalibration(const TouchCalibration &calib, const char *source)
{
    if (!calib.valid)
    {
        Log("Touchscreen: calibration source=%s; invalid or unavailable", source ? source : "unknown");
        return;
    }
    Log("Touchscreen: calibration source=%s; invert=(true,true); manual overrides ignored; "
        "native extent=(%d,%d)",
        source ? source : "EEPROM", calib.x.back().target, calib.y.back().target);
    for (size_t i = 0; i < calib.x.size(); ++i)
    {
        Log("Touchscreen: axis control point[%zu] X(raw=%d,target=%d) Y(raw=%d,target=%d)", i,
            calib.x[i].raw, calib.x[i].target, calib.y[i].raw, calib.y[i].target);
    }
}

bool LoadCalibrationFromFile(
    const std::string &path,
    TouchCalibration &output,
    CalibrationFileFormat format,
    std::array<uint8_t, 20> *preservedPayload)
{
    output = {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return false;
    }
    const auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    bool tryBinary = (format == CalibrationFileFormat::Binary64) ||
                     (format == CalibrationFileFormat::Auto && fileSize == 64);

    if (format == CalibrationFileFormat::Binary64 && fileSize != 64) return false;

    if (tryBinary && fileSize == 64)
    {
        Record record{};
        file.read(reinterpret_cast<char *>(record.data()), 64);
        if (file.gcount() == 64 && DecodeCalibration(record, output))
        {
            if (preservedPayload)
            {
                std::copy_n(record.data() + 44, 20, preservedPayload->data());
            }
            return true;
        }
        if (format == CalibrationFileFormat::Binary64)
        {
            return false;
        }
        // Fall through to text parse if Auto
        file.clear();
        file.seekg(0, std::ios::beg);
    }

    // Text parsing
    std::string line;
    int extentX = 0, extentY = 0;
    std::array<AxisPoint, 5> xPoints{}, yPoints{};
    bool hasExtent = false;
    bool hasX[5] = {false}, hasY[5] = {false};

    while (std::getline(file, line))
    {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }
        auto eq = trimmed.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        std::string key = Trim(trimmed.substr(0, eq));
        std::string val = Trim(trimmed.substr(eq + 1));
        std::istringstream valStream(val);

        if (key == "extent")
        {
            valStream >> extentX >> extentY;
            hasExtent = (extentX > 0 && extentY > 0);
        }
        else if (key.size() == 2 && key[0] == 'x' && key[1] >= '0' && key[1] <= '4')
        {
            int idx = key[1] - '0';
            int raw = 0, target = 0;
            if (valStream >> raw >> target)
            {
                if (raw < 0 || raw > 4095 || target < 0 || target > 32767) return false;
                xPoints[idx] = {int16_t(raw), int16_t(target)};
                hasX[idx] = true;
            }
        }
        else if (key.size() == 2 && key[0] == 'y' && key[1] >= '0' && key[1] <= '4')
        {
            int idx = key[1] - '0';
            int raw = 0, target = 0;
            if (valStream >> raw >> target)
            {
                if (raw < 0 || raw > 4095 || target < 0 || target > 32767) return false;
                yPoints[idx] = {int16_t(raw), int16_t(target)};
                hasY[idx] = true;
            }
        }
        else if (key == "payload" && preservedPayload)
        {
            std::string hexStr;
            valStream >> hexStr;
            if (hexStr.size() >= 40)
            {
                for (size_t i = 0; i < 20; ++i)
                {
                    unsigned byteVal = 0;
                    std::stringstream ss;
                    ss << std::hex << hexStr.substr(i * 2, 2);
                    ss >> byteVal;
                    (*preservedPayload)[i] = uint8_t(byteVal);
                }
            }
        }
    }

    for (int i = 0; i < 5; ++i)
    {
        if (!hasX[i] || !hasY[i])
        {
            return false;
        }
    }

    if (!hasExtent || extentX != xPoints.back().target || extentY != yPoints.back().target) return false;

    // Validate monotonicity matching DecodeCalibration
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

bool SaveCalibrationToFile(
    const std::string &path,
    const TouchCalibration &calib,
    CalibrationFileFormat format,
    std::span<const uint8_t> preservedPayload)
{
    if (!calib.valid)
    {
        return false;
    }

    Record validationRecord{};
    TouchCalibration validated{};
    EncodeCalibration(calib, validationRecord);
    if (!DecodeCalibration(validationRecord, validated)) return false;

    bool saveBinary = (format == CalibrationFileFormat::Binary64);
    if (format == CalibrationFileFormat::Auto)
    {
        auto dot = path.rfind('.');
        if (dot != std::string::npos)
        {
            std::string ext = path.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".bin" || ext == ".dat")
            {
                saveBinary = true;
            }
        }
    }

    if (saveBinary)
    {
        Record record{};
        EncodeCalibration(calib, record, preservedPayload);
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }
        file.write(reinterpret_cast<const char *>(record.data()), record.size());
        file.flush();
        if (!file.good()) return false;
        file.close();
        return !file.fail();
    }
    else
    {
        std::ofstream file(path);
        if (!file.is_open())
        {
            return false;
        }
        file << "# AnySlider Touchscreen Calibration File\n";
        file << "# Format: AFT SBZV 5-point axis control points\n";
        file << "format = aft_axis_control_points\n";
        file << "extent = " << calib.x.back().target << " " << calib.y.back().target << "\n";
        for (size_t i = 0; i < 5; ++i)
        {
            file << "x" << i << " = " << calib.x[i].raw << " " << calib.x[i].target << "\n";
        }
        for (size_t i = 0; i < 5; ++i)
        {
            file << "y" << i << " = " << calib.y[i].raw << " " << calib.y[i].target << "\n";
        }
        if (!preservedPayload.empty())
        {
            file << "payload = ";
            for (auto byte : preservedPayload)
            {
                file << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
            }
            file << "\n";
        }
        file.flush();
        if (!file.good()) return false;
        file.close();
        return !file.fail();
    }
}

// 只导入已有校准：主记录无效才试镜像，绝不初始化或修复机台备份。
bool ReadTouchCalibration(TouchCalibration &output)
{
    output = {};
    DebugLog("EEPROM: probing motherboard EEPROM for AFT CalibData (main=%04X mirror=%04X, read-only)",
             CalibMainOffset, CalibBackupOffset);
    HDEVINFO devices =
        SetupDiGetClassDevsW(&EepromGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE)
    {
        Log("EEPROM: device enumeration failed: %lu", GetLastError());
        return false;
    }
    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize = sizeof(interfaceData);
    const BOOL found = SetupDiEnumDeviceInterfaces(devices, nullptr, &EepromGuid, 0, &interfaceData);
    DWORD size = 0;
    if (found)
    {
        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0, &size, nullptr);
    }
    if (!found || size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
    {
        SetupDiDestroyDeviceInfoList(devices);
        Log("EEPROM: no motherboard EEPROM interface; skipping calibration");
        return false;
    }
    std::vector<uint8_t> storage(size);
    auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(storage.data());
    detail->cbSize = sizeof(*detail);
    const BOOL detailed =
        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, size, nullptr, nullptr);
    if (!detailed)
    {
        SetupDiDestroyDeviceInfoList(devices);
        Log("EEPROM: interface path unavailable");
        return false;
    }
    DebugLog("EEPROM: device path=%ls", detail->DevicePath);
    Handle device{CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
    SetupDiDestroyDeviceInfoList(devices);
    if (device.value == INVALID_HANDLE_VALUE)
    {
        Log("EEPROM: read-only open failed: %lu; skipping calibration", GetLastError());
        return false;
    }
    // Existing driver/library uses this named mutex. Do not reset or initialize backup objects.
    Handle mutex{CreateMutexW(nullptr, FALSE, L"Global\\AM_EEPROM_MUTEX")};
    if (!mutex.value)
    {
        Log("EEPROM: mutex unavailable: %lu", GetLastError());
        return false;
    }
    DWORD wait = WaitForSingleObject(mutex.value, 1000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
    {
        Log("EEPROM: mutex timeout");
        return false;
    }
    bool success = false;
    Record record{};
    for (unsigned offset : {CalibMainOffset, CalibBackupOffset})
    {
        DebugLog("EEPROM: reading CalibData offset=%04X size=%zu", offset, record.size());
        const bool read = ReadRecord(device, offset, record);
        if (read)
        {
            const uint32_t stored = uint32_t(record[0]) | (uint32_t(record[1]) << 8) |
                                    (uint32_t(record[2]) << 16) | (uint32_t(record[3]) << 24);
            DebugLog("EEPROM: offset=%04X CRC stored=%08X calculated=%08X", offset, stored,
                     Crc32(std::span(record).subspan(4)));
        }
        if (read && DecodeCalibration(record, output))
        {
            Log("EEPROM: applied AFT CalibData from %04X", offset);
            success = true;
            break;
        }
        Log("EEPROM: CalibData %04X unreadable or invalid", offset);
        if (device.value == INVALID_HANDLE_VALUE)
        {
            break;
        }
    }
    if (success)
    {
        LogTouchCalibration(output, "AFT EEPROM");
    }
    ReleaseMutex(mutex.value);
    return success;
}
} // namespace anyslider::arcade_eeprom
