#pragma once
#include <windows.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace anyslider
{
// Own all memory the driver can reference. A broken driver must not pin the game
// thread or access freed stack memory. Caller quarantines device on retained=true.
inline bool ArcadeTransfer(HANDLE device, bool read, void *data, DWORD size, DWORD offset, DWORD timeout,
                           DWORD &count, bool &retained)
{
    struct Pending
    {
        OVERLAPPED operation{};
        std::vector<unsigned char> bytes;
        ~Pending()
        {
            if (operation.hEvent)
            {
                CloseHandle(operation.hEvent);
            }
        }
    };
    auto pending = std::make_unique<Pending>();
    pending->bytes.resize(size);
    pending->operation.Offset = offset;
    pending->operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    count = 0;
    retained = false;
    if (!pending->operation.hEvent)
    {
        return false;
    }
    if (!read)
    {
        std::copy_n(static_cast<unsigned char *>(data), size, pending->bytes.data());
    }
    BOOL ok = read ? ReadFile(device, pending->bytes.data(), size, nullptr, &pending->operation)
                   : WriteFile(device, pending->bytes.data(), size, nullptr, &pending->operation);
    if (!ok && GetLastError() != ERROR_IO_PENDING)
    {
        return false;
    }
    if (!ok && WaitForSingleObject(pending->operation.hEvent, timeout) != WAIT_OBJECT_0)
    {
        CancelIoEx(device, &pending->operation);
        if (WaitForSingleObject(pending->operation.hEvent, 100) != WAIT_OBJECT_0)
        {
            // Fail closed. Deliberate process-lifetime quarantine, no retry on this device.
            retained = true;
            pending.release();
            SetLastError(ERROR_TIMEOUT);
            return false;
        }
        GetOverlappedResult(device, &pending->operation, &count, FALSE);
        count = 0;
        SetLastError(ERROR_TIMEOUT);
        return false;
    }
    if (!GetOverlappedResult(device, &pending->operation, &count, FALSE) || count > size)
    {
        return false;
    }
    if (read)
    {
        std::copy_n(pending->bytes.data(), count, static_cast<unsigned char *>(data));
    }
    return true;
}
} // namespace anyslider
