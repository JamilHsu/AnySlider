using System.Net.WebSockets;
using System.Text.Json;
using AnySlider.Viewer;
using Microsoft.Extensions.FileProviders;

string? Option(string name)
{
    var index = Array.IndexOf(args, name);
    if (index < 0) return null;
    if (index + 1 >= args.Length) throw new ArgumentException($"Missing value for {name}");
    return args[index + 1];
}
if (args.Contains("--help"))
{
    Console.WriteLine("AnySlider-Viewer: --port 8765 --snapshot-file <ABI fixture>\nLive mode reads Windows Local\\MMIO_SHARED_BUFFER. HTTP and WS bind loopback only.");
    return;
}
var port = int.Parse(Option("--port") ?? "8765");
if (port is < 1 or > 65535) throw new ArgumentOutOfRangeException("port");
var fixture = Option("--snapshot-file");
if (fixture is not null) fixture = Path.GetFullPath(fixture);
var builder = WebApplication.CreateBuilder();
builder.WebHost.UseUrls($"http://127.0.0.1:{port}");
var app = builder.Build();
var webRoot = Path.Combine(AppContext.BaseDirectory, "web");
app.Use(async (context, next) =>
{
    if (context.Request.Host.Host != "127.0.0.1" && context.Request.Host.Host != "localhost")
    { context.Response.StatusCode = 403; return; }
    var origin = context.Request.Headers.Origin.ToString();
    if (origin.Length > 0 && (!Uri.TryCreate(origin, UriKind.Absolute, out var uri) ||
        uri.Scheme != "http" || uri.Port != port || (uri.Host != "127.0.0.1" && uri.Host != "localhost")))
    { context.Response.StatusCode = 403; return; }
    await next(context);
});
var files = new PhysicalFileProvider(webRoot);
app.UseDefaultFiles(new DefaultFilesOptions { FileProvider = files });
app.UseStaticFiles(new StaticFileOptions { FileProvider = files });
app.UseWebSockets(new WebSocketOptions { KeepAliveInterval = TimeSpan.FromSeconds(20) });
app.MapGet("/api/status", () =>
{
    using var reader = new SnapshotReader(fixture);
    return Results.Json(reader.Read());
});
app.Map("/ws", async context =>
{
    if (!context.WebSockets.IsWebSocketRequest) { context.Response.StatusCode = 426; return; }
    using var socket = await context.WebSockets.AcceptWebSocketAsync();
    using var reader = new SnapshotReader(fixture);
    using var lifetime = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
    using var input = new InputWriter(fixture);
    var receive = ObserveClose(socket, lifetime, input);
    try
    {
        byte[]? previous = null;
        var lastSent = DateTime.MinValue;
        while (!lifetime.IsCancellationRequested && socket.State == WebSocketState.Open)
        {
            string? inputError = null;
            try { input.Tick(); } catch (Exception ex) { inputError = ex.Message; }
            var frame = reader.Read();
            var payload = JsonSerializer.SerializeToUtf8Bytes(new { frame.Source, frame.Status, frame.Error, frame.Led, InputEnabled = input.Enabled, InputError = inputError ?? input.Error }, new JsonSerializerOptions(JsonSerializerDefaults.Web));
            if (previous is null || !payload.AsSpan().SequenceEqual(previous) || DateTime.UtcNow - lastSent > TimeSpan.FromSeconds(1))
            {
                using var send = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
                send.CancelAfter(TimeSpan.FromSeconds(3));
                await socket.SendAsync(payload, WebSocketMessageType.Text, true, send.Token);
                previous = payload; lastSent = DateTime.UtcNow;
            }
            await Task.Delay(16, lifetime.Token);
        }
    }
    catch (Exception ex) when (ex is OperationCanceledException or WebSocketException) { }
    finally
    {
        lifetime.Cancel();
        await receive;
        if (socket.State == WebSocketState.CloseReceived)
            await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, "closed", CancellationToken.None);
    }
});
Console.WriteLine($"AnySlider-Viewer · http://127.0.0.1:{port}/ · OBS: /?overlay=1 · source: {(fixture is null ? "Windows MMIO" : "ABI fixture (test only)")}");
await app.RunAsync();

static async Task ObserveClose(WebSocket socket, CancellationTokenSource lifetime, InputWriter input)
{
    try
    {
        var buffer = new byte[512];
        while (!lifetime.IsCancellationRequested)
        {
            var message = await socket.ReceiveAsync(buffer, lifetime.Token);
            if (message.MessageType == WebSocketMessageType.Close) break;
            if (message.MessageType != WebSocketMessageType.Text || !message.EndOfMessage) break;
            try
            {
                var command = JsonSerializer.Deserialize<InputCommand>(buffer.AsSpan(0,message.Count), new JsonSerializerOptions(JsonSerializerDefaults.Web));
                if (command is null) throw new InvalidDataException("Empty command");
                input.Command(command);
                input.Error = null;
            }
            catch (Exception ex) when (ex is IOException or InvalidOperationException or ArgumentException or PlatformNotSupportedException or JsonException or UnauthorizedAccessException)
            {
                input.Error = ex.Message;
            }
        }
    }
    catch (Exception ex) when (ex is OperationCanceledException or WebSocketException) { }
    finally { lifetime.Cancel(); }
}
