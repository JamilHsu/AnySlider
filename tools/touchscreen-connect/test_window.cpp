#include "test_window.h"
#include <windowsx.h>
#include <cstdio>
namespace touchscreen_connect
{
namespace
{
float targetX = -1, targetY = -1;
float contactX = -1, contactY = -1;
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l)
{
    switch (message)
    {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MOUSEMOVE:
        if (message != WM_MOUSEMOVE || (w & MK_LBUTTON))
            std::printf("RECEIVED mouse message=%04X client=(%d,%d)\n", message, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        break;
    case WM_POINTERDOWN: case WM_POINTERUPDATE: case WM_POINTERUP:
    {
        POINTER_INFO info{};
        if (GetPointerInfo(GET_POINTERID_WPARAM(w), &info))
            std::printf("RECEIVED pointer message=%04X screen=(%ld,%ld) flags=%08X\n", message,
                info.ptPixelLocation.x, info.ptPixelLocation.y, info.pointerFlags);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        const wchar_t text[] = L"Touchscreen input test - keep this window foreground. See console for stop hotkey.";
        TextOutW(dc, 15, 20, text, int(_countof(text) - 1));
        if (targetX >= 0)
        {
            RECT client{}; GetClientRect(window, &client);
            int x = int(targetX * (client.right - 1)), y = int(targetY * (client.bottom - 1));
            Ellipse(dc, x - 12, y - 12, x + 12, y + 12);
            MoveToEx(dc, x - 20, y, nullptr); LineTo(dc, x + 20, y);
            MoveToEx(dc, x, y - 20, nullptr); LineTo(dc, x, y + 20);
        }
        if(contactX >= 0) { RECT r{}; GetClientRect(window,&r);
            int x=int(contactX*(r.right-1)),y=int(contactY*(r.bottom-1));
            Rectangle(dc,x-6,y-6,x+6,y+6); }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
}
void SetPreviewContact(HWND window, float x, float y)
{ contactX=x;contactY=y;InvalidateRect(window,nullptr,TRUE); }
void SetCalibrationTarget(HWND window, float x, float y, unsigned index)
{
    targetX = x; targetY = y;
    wchar_t title[100]{};
    swprintf_s(title, L"Touchscreen calibration: point %u/5 - touch the cross", index + 1);
    SetWindowTextW(window, title);
    InvalidateRect(window, nullptr, TRUE);
}
HWND CreateInputTestWindow()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = Procedure;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TouchscreenConnectInputTest";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"TouchscreenConnect input test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (window) { ShowWindow(window, SW_SHOW); UpdateWindow(window); }
    return window;
}
}
