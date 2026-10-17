using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;

namespace AnySlider.Viewer;

// One exclusive debug producer; no mapping is opened for writing before enable.
sealed unsafe class InputWriter(string? fixture) : IDisposable
{
    const int StartedOffset=6176, HeartbeatOffset=6184, LeaseOffset=6192, GameStateOffset=6200;
    static readonly object Gate = new();
    static InputWriter? owner;
    MemoryMappedFile? mapping;
    MemoryMappedViewAccessor? view;
    byte* data;
    ulong started;
    ulong buttons;
    uint touch;
    long lastCommand;
    public bool Enabled => view is not null;
    public string? Error { get; set; }
    static ulong Now => unchecked((ulong)Environment.TickCount64);
    ref long Atomic(int offset) => ref *(long*)(data + offset);
    long Read(int offset) => Interlocked.Read(ref Atomic(offset));
    void Write(int offset, long value) => Interlocked.Exchange(ref Atomic(offset), value);
    void Validate()
    {
        if (*(uint*)data != 0x4F494D4D || *(ushort*)(data+4) != 3 || *(ushort*)(data+6) != 0 || *(uint*)(data+8) != 6368)
            throw new InvalidDataException("Expected MMIO ABI 3.0 / 6368 bytes.");
    }
    [SupportedOSPlatform("windows")]
    static MemoryMappedFile OpenWindows() => MemoryMappedFile.OpenExisting(@"Local\MMIO_SHARED_BUFFER", MemoryMappedFileRights.ReadWrite);
    public void Enable()
    {
        lock (Gate)
        {
            if (Enabled) { lastCommand = Environment.TickCount64; return; }
            if (owner is not null) throw new InvalidOperationException("Another Viewer connection owns input.");
            try
            {
                mapping = fixture is not null
                    ? MemoryMappedFile.CreateFromFile(fixture, FileMode.Open, null, 0, MemoryMappedFileAccess.ReadWrite)
                    : OperatingSystem.IsWindows() ? OpenWindows() : throw new PlatformNotSupportedException("Live input requires Windows.");
                view = mapping.CreateViewAccessor(0,6368,MemoryMappedFileAccess.ReadWrite);
                byte* pointer = null;
                view.SafeMemoryMappedViewHandle.AcquirePointer(ref pointer);
                data = pointer + view.PointerOffset;
                Validate();
                var heartbeat = unchecked((ulong)Read(HeartbeatOffset));
                if (*(uint*)(data+6168) != 0 && heartbeat <= Now && Now-heartbeat <= 1500)
                    throw new InvalidOperationException("An active MMIO producer exists. Stop its frontend before enabling debug input.");
                started = Math.Max(Now, unchecked((ulong)Read(StartedOffset)) + 1);
                Write(16,0);
                for (var i=0;i<128;i++) Write(24+i*48,0);
                *(ushort*)(data+6172)=3; *(ushort*)(data+6174)=0;
                Write(StartedOffset,unchecked((long)started)); Write(LeaseOffset,500);
                *(uint*)(data+GameStateOffset)=0; // producer game_state
                *(uint*)(data+6168)=unchecked((uint)Environment.ProcessId);
                owner=this; buttons=0; touch=0; lastCommand=Environment.TickCount64;
                Publish();
            }
            catch { Close(); throw; }
        }
    }
    bool Owns => data != null && *(uint*)(data+6168)==unchecked((uint)Environment.ProcessId) && unchecked((ulong)Read(StartedOffset))==started;
    void Publish(bool inactive=false)
    {
        Validate();
        if (!Owns) throw new InvalidOperationException("MMIO producer ownership changed; debug input stopped.");
        var next=checked(Read(16)+1);
        var offset=24+(int)((next-1)%128)*48;
        Write(offset,0);
        *(ulong*)(data+offset+8)=buttons;
        *(ulong*)(data+offset+16)=0; *(ulong*)(data+offset+24)=0;
        *(uint*)(data+offset+32)=touch;
        *(uint*)(data+offset+36)=inactive ? 0u : 1u;
        *(ulong*)(data+offset+40)=Now;
        Thread.MemoryBarrier(); Write(offset,next); Write(16,next); Write(HeartbeatOffset,unchecked((long)Now));
    }
    public void Command(InputCommand command)
    {
        lock(Gate)
        {
            if(command.Type=="enable") { Enable(); return; }
            if(command.Type=="disable") { Dispose(); return; }
            if(!Enabled) throw new InvalidOperationException("Enable input first.");
            lastCommand=Environment.TickCount64;
            if(command.Type=="keepalive") return;
            if(command.Type=="release") { buttons=0; touch=0; Publish(); return; }
            if(command.Type is not ("state" or "pulse") || (command.Buttons & ~0x780u)!=0)
                throw new InvalidDataException("Invalid input command/button mask.");
            buttons=command.Buttons; touch=command.TouchMask; Publish();
            if(command.Type=="pulse") { buttons=0; touch=0; Publish(); }
        }
    }
    public void Tick()
    {
        lock(Gate)
        {
            if(!Enabled) return;
            if(Environment.TickCount64-lastCommand>1500) { Dispose(); return; }
            try { Publish(); } catch { Close(); throw; }
        }
    }
    public void Dispose()
    {
        lock(Gate)
        {
            try
            {
                if(Owns) { buttons=0; touch=0; Publish(true); Write(HeartbeatOffset,0); *(uint*)(data+6168)=0; }
            }
            finally { Close(); }
        }
    }
    void Close()
    {
        if(data!=null) { view!.SafeMemoryMappedViewHandle.ReleasePointer(); data=null; }
        view?.Dispose(); view=null; mapping?.Dispose(); mapping=null;
        if(owner==this) owner=null;
    }
}
record InputCommand(string Type, uint Buttons=0, uint TouchMask=0);
