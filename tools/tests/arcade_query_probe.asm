; Standalone regression probe; never linked into the production DLL.
OPTION CASEMAP:NONE
EXTERN IsArcadeControllerEnabledHook:PROC
EXTERN testReturn:DWORD
EXTERN implStackAlignment:DWORD
PUBLIC ProbeArcadeQuery
PUBLIC IsArcadeControllerEnabledHookImpl
.code
ProbeArcadeQuery PROC FRAME
    push rbx
    .pushreg rbx
    sub rsp, 20h
    .allocstack 20h
    .endprolog
    mov rbx, rcx
    mov rcx, 1122334455667701h
    mov rdx, 1122334455667702h
    mov r8, 1122334455667703h
    mov r9, 1122334455667704h
    mov r10, 1122334455667705h
    mov r11, 1122334455667706h
    pcmpeqd xmm0, xmm0
    pcmpeqd xmm1, xmm1
    pcmpeqd xmm2, xmm2
    pcmpeqd xmm3, xmm3
    pcmpeqd xmm4, xmm4
    pcmpeqd xmm5, xmm5
    call IsArcadeControllerEnabledHook
    mov [rbx+0h], rcx
    mov [rbx+8h], rdx
    mov [rbx+10h], r8
    mov [rbx+18h], r9
    mov [rbx+20h], r10
    mov [rbx+28h], r11
    movdqu xmmword ptr [rbx+30h], xmm0
    movdqu xmmword ptr [rbx+40h], xmm1
    movdqu xmmword ptr [rbx+50h], xmm2
    movdqu xmmword ptr [rbx+60h], xmm3
    movdqu xmmword ptr [rbx+70h], xmm4
    movdqu xmmword ptr [rbx+80h], xmm5
    mov [rbx+90h], eax
    add rsp, 20h
    pop rbx
    ret
ProbeArcadeQuery ENDP

; Adversarial implementation: overwrite every volatile register protected
; by the bridge and all four home slots; return a configurable EAX value.
IsArcadeControllerEnabledHookImpl PROC
    mov rax, rsp
    and eax, 0Fh
    mov implStackAlignment, eax
    xor eax, eax
    mov [rsp+8], rax
    mov [rsp+10h], rax
    mov [rsp+18h], rax
    mov [rsp+20h], rax
    xor rcx, rcx
    xor rdx, rdx
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    mov eax, testReturn
    ret
IsArcadeControllerEnabledHookImpl ENDP
END
