#include "arcade_eeprom_write.h"
#include "anyslider_log.h"
#include "arcade_overlapped_io.h"
#include <setupapi.h>
#include <fstream>
#include <filesystem>
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

bool TransferRecord(Handle &device, bool read, unsigned offset, Record &record, DWORD timeoutMs, bool &quarantined)
{
    DWORD count = 0;
    quarantined = false;
    const bool ok = ArcadeTransfer(device.value, read, record.data(), DWORD(record.size()), offset, timeoutMs,
                                   count, quarantined);
    if (quarantined) device.value = INVALID_HANDLE_VALUE;
    return ok && count == record.size();
}

bool SaveBackupToFile(const std::string &path, const Record &mainRecord, const Record &mirrorRecord)
{
    if (path.empty())
    {
        return false;
    }
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::array<uint8_t,128> data{}, check{};
    std::copy(mainRecord.begin(),mainRecord.end(),data.begin());
    std::copy(mirrorRecord.begin(),mirrorRecord.end(),data.begin()+64);
    DWORD written=0, read=0;
    bool ok=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr) && written==data.size();
    ok=ok && FlushFileBuffers(file);
    LARGE_INTEGER zero{};
    ok=ok && SetFilePointerEx(file,zero,nullptr,FILE_BEGIN);
    ok=ok && ReadFile(file,check.data(),DWORD(check.size()),&read,nullptr) && read==check.size() && check==data;
    CloseHandle(file);
    return ok;
}

} // namespace

const char *WriteStatusToString(WriteStatus status)
{
    switch (status)
    {
    case WriteStatus::HardwareCommitNotValidated:
        return "HardwareCommitNotValidated";
    case WriteStatus::Success:
        return "Success";
    case WriteStatus::DryRunSimulated:
        return "DryRunSimulated";
    case WriteStatus::InvalidCalibration:
        return "InvalidCalibration";
    case WriteStatus::DeviceNotFound:
        return "DeviceNotFound";
    case WriteStatus::OpenFailed:
        return "OpenFailed";
    case WriteStatus::MutexTimeout:
        return "MutexTimeout";
    case WriteStatus::BackupFailed:
        return "BackupFailed";
    case WriteStatus::SnapshotMismatch:
        return "SnapshotMismatch";
    case WriteStatus::MirrorWriteFailed:
        return "MirrorWriteFailed";
    case WriteStatus::MirrorVerificationFailed:
        return "MirrorVerificationFailed";
    case WriteStatus::PrimaryWriteFailed:
        return "PrimaryWriteFailed";
    case WriteStatus::PrimaryVerificationFailed:
        return "PrimaryVerificationFailed";
    case WriteStatus::DeviceQuarantined:
        return "DeviceQuarantined";
    case WriteStatus::FileIOFailed:
        return "FileIOFailed";
    default:
        return "UnknownStatus";
    }
}

HardwareWriteResult WriteTouchCalibrationHardware(
    const TouchCalibration &calib,
    const HardwareWriteOptions &options)
{
    HardwareWriteResult result{};

    const bool committing = !options.dryRun && options.commit;
    // Static AFT evidence is not device/layout or failure-path acceptance.
    // Keep physical submission closed until separately authorized and validated.
    if (committing)
    {
        result.status = WriteStatus::HardwareCommitNotValidated;
        result.message = "Hardware commit remains disabled; use offline image mode pending device acceptance.";
        return result;
    }
    if (committing && (options.devicePath.empty() || !options.requireBackup ||
        !options.checkSnapshot || options.backupFilePath.empty()))
    {
        result.status = WriteStatus::HardwareCommitNotValidated;
        result.message = "Commit requires explicit device identity, expected snapshots and mandatory new backup.";
        return result;
    }
    if (options.timeoutMs == 0 || options.timeoutMs > 10000)
    {
        result.status = WriteStatus::OpenFailed;
        result.message = "Transfer timeout must be in 1..10000 ms.";
        return result;
    }

    // Independent validation: the public valid flag is not a trust boundary.
    Record validation{};
    TouchCalibration checked{};
    EncodeCalibration(calib, validation);
    if (!calib.valid || !DecodeCalibration(validation, checked))
    {
        result.status = WriteStatus::InvalidCalibration;
        result.message = "TouchCalibration data failed internal validation.";
        return result;
    }

    std::wstring devicePath = options.devicePath;
    if (devicePath.empty())
    {
        HDEVINFO devices =
            SetupDiGetClassDevsW(&EepromGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devices == INVALID_HANDLE_VALUE)
        {
            result.status = WriteStatus::DeviceNotFound;
            result.message = "EEPROM interface enumeration failed: " + std::to_string(GetLastError());
            return result;
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
            result.status = WriteStatus::DeviceNotFound;
            result.message = "Motherboard EEPROM interface not present on system.";
            return result;
        }
        std::vector<uint8_t> storage(size);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(storage.data());
        detail->cbSize = sizeof(*detail);
        const BOOL detailed =
            SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, size, nullptr, nullptr);
        if (!detailed)
        {
            SetupDiDestroyDeviceInfoList(devices);
            result.status = WriteStatus::DeviceNotFound;
            result.message = "Failed to retrieve EEPROM interface path.";
            return result;
        }
        devicePath = detail->DevicePath;
        SetupDiDestroyDeviceInfoList(devices);
    }

    // Open handle: read-only for dry-run simulation, read-write only if committed
    const DWORD desiredAccess = (options.dryRun || !options.commit)
                                    ? GENERIC_READ
                                    : (GENERIC_READ | GENERIC_WRITE);
    Handle device{CreateFileW(devicePath.c_str(), desiredAccess,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
    if (device.value == INVALID_HANDLE_VALUE)
    {
        result.status = WriteStatus::OpenFailed;
        result.message = "Failed to open EEPROM device handle: " + std::to_string(GetLastError());
        return result;
    }

    // Acquire shared mutex Global\AM_EEPROM_MUTEX
    Handle mutex{CreateMutexW(nullptr, FALSE, L"Global\\AM_EEPROM_MUTEX")};
    if (!mutex.value)
    {
        result.status = WriteStatus::MutexTimeout;
        result.message = "Global\\AM_EEPROM_MUTEX creation/open failed: " + std::to_string(GetLastError());
        return result;
    }
    const DWORD wait = WaitForSingleObject(mutex.value, options.timeoutMs);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
    {
        result.status = WriteStatus::MutexTimeout;
        result.message = "Global\\AM_EEPROM_MUTEX lock acquisition timed out.";
        return result;
    }

    struct MutexGuard
    {
        HANDLE m = nullptr;
        ~MutexGuard()
        {
            if (m)
            {
                ReleaseMutex(m);
            }
        }
    } lock{mutex.value};

    // Step 1: Pre-write snapshot read of both copies
    bool quarantined = false;
    Record mainCurrent{}, mirrorCurrent{};
    if (!TransferRecord(device, true, CalibMainOffset, mainCurrent, options.timeoutMs, quarantined))
    {
        result.status = quarantined ? WriteStatus::DeviceQuarantined : WriteStatus::OpenFailed;
        result.message = "Failed to read primary CalibData snapshot from 0x1080.";
        return result;
    }
    if (!TransferRecord(device, true, CalibBackupOffset, mirrorCurrent, options.timeoutMs, quarantined))
    {
        result.status = quarantined ? WriteStatus::DeviceQuarantined : WriteStatus::OpenFailed;
        result.message = "Failed to read mirror CalibData snapshot from 0x1180.";
        return result;
    }
    TouchCalibration existing{};
    if (!DecodeCalibration(mainCurrent, existing) || !DecodeCalibration(mirrorCurrent, existing))
    {
        result.status = WriteStatus::InvalidCalibration;
        result.message = "Both existing records must validate; automatic repair is forbidden.";
        return result;
    }
    result.backupMain = mainCurrent;
    result.backupMirror = mirrorCurrent;

    // Step 2: Snapshot verification against expected content if requested
    if (options.checkSnapshot)
    {
        if (mainCurrent != options.expectedSnapshotMain || mirrorCurrent != options.expectedSnapshotMirror)
        {
            result.status = WriteStatus::SnapshotMismatch;
            result.message = "Current EEPROM content differs from expected pre-write snapshot. Write refused.";
            return result;
        }
    }

    // Step 3: Mandatory backup persistence
    if (committing)
    {
        if (options.backupFilePath.empty() || !SaveBackupToFile(options.backupFilePath, mainCurrent, mirrorCurrent))
        {
            result.status = WriteStatus::BackupFailed;
            result.message = "Failed to save pre-write backup file to: " + options.backupFilePath;
            return result;
        }
    }

    // Step 4: Encode new records, preserving each copy's unexplained trailing 20 bytes
    std::span<const uint8_t, 20> mainPreserved(mainCurrent.data() + 44, 20);
    std::span<const uint8_t, 20> mirrorPreserved(mirrorCurrent.data() + 44, 20);
    EncodeCalibration(calib, result.targetMain, mainPreserved);
    EncodeCalibration(calib, result.targetMirror, mirrorPreserved);

    // Step 5: Safety gate: Refuse commit if dry-run is active
    if (options.dryRun || !options.commit)
    {
        result.status = WriteStatus::DryRunSimulated;
        result.message = "Dry-run mode active. Simulation verified: records prepared, CRCs computed, no hardware write executed.";
        return result;
    }

    // Step 6: Verified AFT Commit Chain
    // Proven commit order: Mirror first (0x1180), then Primary (0x1080).
    // AFT sub_1406EB980 writes mirror at a1+24 first, then primary at a1.
    // If mirror write or verification fails, primary remains completely untouched.

    // 6a. Write Mirror record (0x1180)
    if (!TransferRecord(device, false, CalibBackupOffset, result.targetMirror, options.timeoutMs, quarantined))
    {
        result.status = quarantined ? WriteStatus::DeviceQuarantined : WriteStatus::MirrorWriteFailed;
        result.message = "Hardware write failed for mirror record at 0x1180. Primary record untouched.";
        return result;
    }
    result.mirrorWritten = true;

    // 6b. Read back & verify Mirror record
    Record mirrorReadback{};
    if (!TransferRecord(device, true, CalibBackupOffset, mirrorReadback, options.timeoutMs, quarantined) ||
        mirrorReadback != result.targetMirror)
    {
        result.status = quarantined ? WriteStatus::DeviceQuarantined : WriteStatus::MirrorVerificationFailed;
        result.message = "Mirror readback verification failed at 0x1180. Primary record untouched.";
        return result;
    }
    result.mirrorVerified = true;

    // 6c. Write Primary record (0x1080)
    if (!TransferRecord(device, false, CalibMainOffset, result.targetMain, options.timeoutMs, quarantined))
    {
        result.status = quarantined ? WriteStatus::DeviceQuarantined : WriteStatus::PrimaryWriteFailed;
        result.message = "Hardware write failed for primary record at 0x1080. Mirror was written and verified.";
        return result;
    }
    result.primaryWritten = true;

    // 6d. Read back & verify Primary record
    Record mainReadback{};
    if (!TransferRecord(device, true, CalibMainOffset, mainReadback, options.timeoutMs, quarantined) ||
        mainReadback != result.targetMain)
    {
        result.status = quarantined ? WriteStatus::DeviceQuarantined : WriteStatus::PrimaryVerificationFailed;
        result.message = "Primary readback verification failed at 0x1080. Mirror is valid.";
        return result;
    }
    result.primaryVerified = true;

    result.status = WriteStatus::Success;
    result.message = "Successfully committed and verified EEPROM CalibData (mirror 0x1180, then primary 0x1080).";
    return result;
}

bool UpdateEepromImageFile(
    const std::string &imagePath,
    const TouchCalibration &calib,
    std::string *errorMessage,
    std::string *backupPath)
{
    // Independent validation: the public valid flag is not a trust boundary.
    Record validation{};
    TouchCalibration checked{};
    EncodeCalibration(calib, validation);
    if (!calib.valid || !DecodeCalibration(validation, checked))
    {
        if (errorMessage) *errorMessage = "TouchCalibration is invalid.";
        return false;
    }

    // Offline mode requires a new backup destination, never the source itself.
    if (!backupPath || backupPath->empty() || std::filesystem::exists(*backupPath))
    {
        if (errorMessage) *errorMessage = "A new non-existing backup path is required.";
        return false;
    }
    std::fstream file(imagePath, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open())
    {
        if (errorMessage) *errorMessage = "Failed to open EEPROM image file: " + imagePath;
        return false;
    }

    file.seekg(0, std::ios::end);
    const auto fileSize = file.tellg();
    constexpr size_t requiredSize = CalibBackupOffset + 64; // 0x11C0 = 4544 bytes
    if (fileSize < static_cast<std::streampos>(requiredSize))
    {
        if (errorMessage) *errorMessage = "EEPROM image file too small (must be >= 0x11C0 bytes).";
        return false;
    }

    // Read existing records to preserve unexplained 20 bytes
    Record oldMain{}, oldMirror{};
    file.seekg(CalibMainOffset, std::ios::beg);
    file.read(reinterpret_cast<char *>(oldMain.data()), 64);
    if (file.gcount() != 64)
    {
        if (errorMessage) *errorMessage = "Failed to read main CalibData from image.";
        return false;
    }

    file.seekg(CalibBackupOffset, std::ios::beg);
    file.read(reinterpret_cast<char *>(oldMirror.data()), 64);
    if (file.gcount() != 64)
    {
        if (errorMessage) *errorMessage = "Failed to read mirror CalibData from image.";
        return false;
    }

    TouchCalibration oldCheck{};
    if (!DecodeCalibration(oldMain, oldCheck) || !DecodeCalibration(oldMirror, oldCheck))
    { if (errorMessage) *errorMessage = "Existing image calibration records are invalid; refusing repair."; return false; }

    // Create backup file if requested
    if (backupPath && !backupPath->empty())
    {
        std::ofstream backupOut(*backupPath, std::ios::binary);
        if (!backupOut.is_open())
        {
            if (errorMessage) *errorMessage = "Failed to open backup destination: " + *backupPath;
            return false;
        }
        file.seekg(0, std::ios::beg);
        backupOut << file.rdbuf();
        backupOut.flush();
        backupOut.close();
        if (backupOut.fail())
        {
            if (errorMessage) *errorMessage = "Failed to write complete image backup.";
            return false;
        }
    }

    // Prepare updated records
    Record newMain{}, newMirror{};
    std::span<const uint8_t, 20> mainPreserved(oldMain.data() + 44, 20);
    std::span<const uint8_t, 20> mirrorPreserved(oldMirror.data() + 44, 20);
    EncodeCalibration(calib, newMain, mainPreserved);
    EncodeCalibration(calib, newMirror, mirrorPreserved);

    // Commit mirror (0x1180), then primary (0x1080)
    file.seekp(CalibBackupOffset, std::ios::beg);
    file.write(reinterpret_cast<const char *>(newMirror.data()), 64);

    file.seekp(CalibMainOffset, std::ios::beg);
    file.write(reinterpret_cast<const char *>(newMain.data()), 64);
    file.flush();

    if (!file.good()) { if (errorMessage) *errorMessage = "Image write failed."; return false; }

    // Verify written data
    Record checkMain{}, checkMirror{};
    file.seekg(CalibMainOffset, std::ios::beg);
    file.read(reinterpret_cast<char *>(checkMain.data()), 64);
    file.seekg(CalibBackupOffset, std::ios::beg);
    file.read(reinterpret_cast<char *>(checkMirror.data()), 64);

    if (!file.good() || checkMain != newMain || checkMirror != newMirror)
    {
        if (errorMessage) *errorMessage = "Readback verification failed after writing EEPROM image.";
        return false;
    }

    return true;
}

} // namespace anyslider::arcade_eeprom
