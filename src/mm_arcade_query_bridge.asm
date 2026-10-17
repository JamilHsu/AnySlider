; MegaMix+ v1.03: Input_IsArcadeControllerEnabled at RVA 2ACA80 and
; its getter at RVA 1D6640 leave all registers except RAX unchanged.
; In particular, the pause initializer keeps a PV pointer in RCX across
; CALL at RVA 656538, then dereferences [RCX+1Ch] at RVA 656546.
; A normal Windows x64 C++ detour violates this contract.
;
; Preserve volatile GPRs and XMM0-5 before entering any compiler-generated
; code. The C++ implementation preserves the standard nonvolatile registers.
; EAX carries the uint32_t result. Flags are not preserved by the original
; getter (ADD RSP changes them). This bridge does not preserve upper YMM/ZMM
; lanes; it targets the game's SSE register usage, not a general AVX ABI.
OPTION CASEMAP:NONE

EXTERN IsArcadeControllerEnabledHookImpl:PROC
PUBLIC IsArcadeControllerEnabledHook

.code
IsArcadeControllerEnabledHook PROC FRAME
    ; 00-1F: shadow space; 20-4F: GPRs; 50-AF: XMMs; B0-B7: padding.
    ; Entry RSP is 8 mod 16; subtracting B8 aligns the nested CALL.
    sub rsp, 0B8h
    .allocstack 0B8h
    .endprolog

    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    mov [rsp+40h], r10
    mov [rsp+48h], r11
    movdqu xmmword ptr [rsp+50h], xmm0
    movdqu xmmword ptr [rsp+60h], xmm1
    movdqu xmmword ptr [rsp+70h], xmm2
    movdqu xmmword ptr [rsp+80h], xmm3
    movdqu xmmword ptr [rsp+90h], xmm4
    movdqu xmmword ptr [rsp+0A0h], xmm5

    call IsArcadeControllerEnabledHookImpl

    movdqu xmm0, xmmword ptr [rsp+50h]
    movdqu xmm1, xmmword ptr [rsp+60h]
    movdqu xmm2, xmmword ptr [rsp+70h]
    movdqu xmm3, xmmword ptr [rsp+80h]
    movdqu xmm4, xmmword ptr [rsp+90h]
    movdqu xmm5, xmmword ptr [rsp+0A0h]
    mov rcx, [rsp+20h]
    mov rdx, [rsp+28h]
    mov r8,  [rsp+30h]
    mov r9,  [rsp+38h]
    mov r10, [rsp+40h]
    mov r11, [rsp+48h]

    ; Standard epilogue and unwind metadata allow Windows stack walking.
    ; Do not restore RAX: it contains the query result.
    add rsp, 0B8h
    ret
IsArcadeControllerEnabledHook ENDP
END
