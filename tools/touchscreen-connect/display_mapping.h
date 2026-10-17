#pragma once
#include "windows_input.h"
#include <algorithm>
#include <vector>
namespace touchscreen_connect
{
struct Display { RECT bounds{}; bool primary=false; };
inline BOOL CALLBACK CollectDisplay(HMONITOR monitor,HDC,LPRECT,LPARAM data)
{
    MONITORINFO info{}; info.cbSize=sizeof(info);
    if(!GetMonitorInfoW(monitor,&info)) return FALSE;
    reinterpret_cast<std::vector<Display>*>(data)->push_back({info.rcMonitor,(info.dwFlags&MONITORINFOF_PRIMARY)!=0});
    return TRUE;
}
// Primary first, then spatial order. Indices are session-local, not device identities.
inline std::vector<Display> Displays()
{
    std::vector<Display> result;
    if(!EnumDisplayMonitors(nullptr,nullptr,CollectDisplay,reinterpret_cast<LPARAM>(&result))) return {};
    std::sort(result.begin(),result.end(),[](const Display&a,const Display&b){
        if(a.primary!=b.primary)return a.primary;
        if(a.bounds.left!=b.bounds.left)return a.bounds.left<b.bounds.left;
        return a.bounds.top<b.bounds.top;
    });
    return result;
}
inline bool SameBounds(const RECT&a,const RECT&b)
{ return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom; }
inline POINT MapDisplay(float x,float y,const RECT& r)
{
    return {r.left+LONG(std::clamp(x,0.0f,1.0f)*(r.right-r.left-1)),
            r.top+LONG(std::clamp(y,0.0f,1.0f)*(r.bottom-r.top-1))};
}
}
