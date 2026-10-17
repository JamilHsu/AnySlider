#define NOMINMAX
#include <windows.h>
#include "elo_emulator.h"
#include "src/mm_io_touchscreen_protocol.h"
#include "src/arcade_overlapped_io.h"
#include <chrono>
#include <cstdio>
#include <thread>
namespace touchscreen_connect {
int RunEloEmulator(const std::string& port, const std::atomic_bool& running)
{
    using namespace anyslider;
    HANDLE handle = CreateFileA(("\\\\.\\" + port).c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { std::fprintf(stderr,"Emulator open failed: %lu\n",GetLastError()); return 1; }
    struct Owner { HANDLE value; ~Owner(){ if(value != INVALID_HANDLE_VALUE) CloseHandle(value); } } owner{handle};
    DCB dcb{}; dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle,&dcb)) return 1;
    dcb.BaudRate=9600; dcb.ByteSize=8; dcb.Parity=NOPARITY; dcb.StopBits=ONESTOPBIT;
    dcb.fBinary=TRUE; dcb.fParity=FALSE; dcb.fOutxCtsFlow=FALSE; dcb.fOutxDsrFlow=FALSE;
    dcb.fDtrControl=DTR_CONTROL_ENABLE; dcb.fRtsControl=RTS_CONTROL_ENABLE;
    dcb.fOutX=FALSE; dcb.fInX=FALSE; dcb.fDsrSensitivity=FALSE; dcb.fAbortOnError=FALSE;
    COMMTIMEOUTS timeouts{}; timeouts.ReadIntervalTimeout=MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier=MAXDWORD; timeouts.ReadTotalTimeoutConstant=20;
    timeouts.WriteTotalTimeoutConstant=100;
    if (!SetCommState(handle,&dcb) || !SetCommTimeouts(handle,&timeouts)) return 1;
    auto transfer = [&](bool read, void* data, DWORD size, DWORD& count) {
        bool retained=false;
        bool ok=ArcadeTransfer(handle,read,data,size,0,200,count,retained);
        if(retained) owner.value=INVALID_HANDLE_VALUE;
        return ok;
    };
    auto send = [&](const elo::Packet& packet) {
        DWORD count=0;
        return transfer(false,const_cast<uint8_t*>(packet.data()),DWORD(packet.size()),count) && count==packet.size();
    };
    elo::Decoder decoder;
    bool ready=false;
    unsigned step=0;
    auto next=std::chrono::steady_clock::now();
    std::puts("Elo virtual peer: no physical touchscreen. Baud enforcement depends on virtual COM driver.");
    while(running)
    {
        std::array<uint8_t,128> bytes{}; DWORD count=0;
        if(!transfer(true,bytes.data(),DWORD(bytes.size()),count)) return 1;
        for(const auto& packet:decoder.Feed(std::span(bytes.data(),count)))
        {
            std::printf("Emulator RX %c\n",packet[1]);
            if(packet[1]=='R' && packet[2]=='0') { ready=false;step=0; }
            else if(packet[1]=='P' || packet[1]=='a')
            {
                std::array<uint8_t,4> status{'0','0','0','0'};
                if(packet[1]=='P' && (packet[2]!='0' || packet[3]!=6 || packet[4]!=4)) status[0]='1';
                if(!send(elo::Encode('A',status))) return 1;
            }
            else if(packet[1]=='p')
            {
                const std::array<uint8_t,3> parameters{'0',6,4};
                const std::array<uint8_t,4> status{'0','0','0','0'};
                if(!send(elo::Encode('P',parameters)) || !send(elo::Encode('A',status))) return 1;
                ready=true; step=0; next=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            }
        }
        if(ready && step<5 && std::chrono::steady_clock::now()>=next)
        {
            const uint8_t states[]={1,4,1,2,4};
            const uint16_t coordinates[]={1000,1000,2000,3000,3000};
            auto v=coordinates[step];
            const std::array<uint8_t,7> data{uint8_t(states[step]|0x80),uint8_t(v),uint8_t(v>>8),uint8_t(v),uint8_t(v>>8),255,0};
            if(!send(elo::Encode('T',data))) return 1;
            std::printf("Emulator T state=%u raw=(%u,%u)\n",states[step],v,v);
            ++step; next=std::chrono::steady_clock::now()+std::chrono::milliseconds(150);
        }
    }
    return 0;
}
}
