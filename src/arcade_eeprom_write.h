#pragma once
#include <windows.h>
#include "arcade_eeprom.h"
#include <string>

namespace anyslider::arcade_eeprom
{

enum class WriteStatus
{
    HardwareCommitNotValidated,
    Success,
    DryRunSimulated,
    InvalidCalibration,
    DeviceNotFound,
    OpenFailed,
    MutexTimeout,
    BackupFailed,
    SnapshotMismatch,
    MirrorWriteFailed,
    MirrorVerificationFailed,
    PrimaryWriteFailed,
    PrimaryVerificationFailed,
    DeviceQuarantined,
    FileIOFailed
};

const char *WriteStatusToString(WriteStatus status);

struct HardwareWriteOptions
{
    bool dryRun = true;          // MANDATORY: Defaults to true. Simulation only.
    bool commit = false;         // Must be explicitly true to perform physical hardware write.
    std::wstring devicePath;     // Optional explicit path. If empty, auto-enumerates EEPROM device GUID.
    std::string backupFilePath;  // Path where current EEPROM state will be backed up before write.
    bool requireBackup = true;   // Refuse write if backup cannot be written.
    DWORD timeoutMs = 1000;      // Bounded timeout for overlapped transfer.
    bool checkSnapshot = false;  // Refuse write if current hardware state differs from expected snapshot.
    Record expectedSnapshotMain{};
    Record expectedSnapshotMirror{};
};

struct HardwareWriteResult
{
    WriteStatus status = WriteStatus::DryRunSimulated;
    std::string message;
    bool mirrorWritten = false;
    bool mirrorVerified = false;
    bool primaryWritten = false;
    bool primaryVerified = false;
    Record backupMain{};
    Record backupMirror{};
    Record targetMirror{};
    Record targetMain{};
};

// Protected hardware EEPROM write API:
// - Defaults to dry-run (no commit unless dryRun==false && commit==true)
// - Fixed CalibData range ONLY (Main 0x1080, Mirror 0x1180, 64 bytes each)
// - Locks Global\AM_EEPROM_MUTEX
// - Reads current hardware snapshot under mutex
// - Compares against expected snapshot (if checkSnapshot==true)
// - Mandates backup confirmation to file before writing
// - Preserves each copy's unexplained trailing 20 bytes (bytes 44..63)
// - Recomputes CRC32
// - Verified AFT commit order: Mirror first (0x1180), verify readback; then Primary (0x1080), verify readback
// - Bounded overlapped I/O with device quarantine on cancellation stall
// - Checks full 64-byte count on each transfer
// - No blind recovery; reports exact status of each copy; does not claim power-loss atomicity
HardwareWriteResult WriteTouchCalibrationHardware(
    const TouchCalibration &calib,
    const HardwareWriteOptions &options = HardwareWriteOptions{});

// Offline EEPROM image file updater:
// Updates 0x1080 (Main) and 0x1180 (Mirror) records in an existing eeprom.bin file (e.g. segatools image).
// Preserves each record's trailing 20 bytes and all other regions of the file.
bool UpdateEepromImageFile(
    const std::string &imagePath,
    const TouchCalibration &calib,
    std::string *errorMessage = nullptr,
    std::string *backupPath = nullptr);

} // namespace anyslider::arcade_eeprom
