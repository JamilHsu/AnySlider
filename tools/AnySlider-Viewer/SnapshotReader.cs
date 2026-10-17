using System.Buffers.Binary;
using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;

namespace AnySlider.Viewer;

// Read-only bridge. File mode is an explicit ABI fixture, never a game fallback.
sealed class SnapshotReader(string? fixture) : IDisposable
{
    MemoryMappedFile? mapping;
    MemoryMappedViewAccessor? view;
    public string Status { get; private set; } = "waiting";
    public string? Error { get; private set; }
    public string Source => fixture is null ? "windows-mapping" : "abi-fixture";

    public ViewerFrame Read()
    {
        try
        {
            if (view is null)
            {
                if (fixture is not null)
                    mapping = MemoryMappedFile.CreateFromFile(fixture, FileMode.Open, null, 0, MemoryMappedFileAccess.Read);
                else if (OperatingSystem.IsWindows()) mapping = OpenWindows();
                else throw new PlatformNotSupportedException("Live MMIO requires Windows; use --snapshot-file only for ABI fixture testing.");
                view = mapping.CreateViewAccessor(0, 6368, MemoryMappedFileAccess.Read);
            }
            if (view.ReadUInt32(0) != 0x4F494D4D || view.ReadUInt16(4) != 3 || view.ReadUInt16(6) != 0 || view.ReadUInt32(8) != 6368)
                throw new InvalidDataException("Expected MMIO ABI 3.0 / 6368 bytes.");
            var led = ReadLed();
            Status = led is null ? "waiting-for-snapshot" : "available";
            Error = null;
            return new(Source, Status, Error, led);
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException or PlatformNotSupportedException)
        {
            Dispose();
            Status = "unavailable";
            Error = ex.Message;
            return new(Source, Status, Error, null);
        }
    }

    [SupportedOSPlatform("windows")]
    static MemoryMappedFile OpenWindows() => MemoryMappedFile.OpenExisting(@"Local\MMIO_SHARED_BUFFER", MemoryMappedFileRights.Read);

    long Sequence(long offset)
    {
        // Aligned native 64-bit reads plus fences; don't split sequence into bytes.
        Thread.MemoryBarrier();
        var result = view!.ReadInt64(offset);
        Thread.MemoryBarrier();
        return result;
    }

    LedFrame? ReadLed()
    {
        for (var attempt = 0; attempt < 3; attempt++)
        {
            var before = Sequence(6248);
            if (before <= 0 || (before & 1) != 0) continue;
            var bytes = new byte[112];
            view!.ReadArray(6256, bytes, 0, bytes.Length);
            if (Sequence(6248) != before) continue;
            if (bytes[12] != 0x3f || bytes.AsSpan(109, 3).ContainsAnyExcept((byte)0))
                throw new InvalidDataException("Invalid LED payload prefix/reserved bytes.");
            return new(before.ToString(), BinaryPrimitives.ReadUInt64LittleEndian(bytes).ToString(),
                BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(8)), Convert.ToBase64String(bytes.AsSpan(12, 97)));
        }
        return null;
    }

    public void Dispose()
    {
        view?.Dispose(); view = null;
        mapping?.Dispose(); mapping = null;
    }
}
record LedFrame(string Sequence, string TimestampMs, uint Io3Bitmap, string SliderBase64);
record ViewerFrame(string Source, string Status, string? Error, LedFrame? Led);
