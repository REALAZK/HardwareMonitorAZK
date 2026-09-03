; hwmon_hv_asm.asm
; Segment/descriptor-table reads with no MSVC intrinsic equivalent, plus
; the raw VM-exit entry trampoline. BUILD-ONLY -- see hwmon_hv.h.

.code

PUBLIC HvReadCs
HvReadCs PROC
    mov eax, 0
    mov ax, cs
    ret
HvReadCs ENDP

PUBLIC HvReadSs
HvReadSs PROC
    mov eax, 0
    mov ax, ss
    ret
HvReadSs ENDP

PUBLIC HvReadDs
HvReadDs PROC
    mov eax, 0
    mov ax, ds
    ret
HvReadDs ENDP

PUBLIC HvReadEs
HvReadEs PROC
    mov eax, 0
    mov ax, es
    ret
HvReadEs ENDP

PUBLIC HvReadFs
HvReadFs PROC
    mov eax, 0
    mov ax, fs
    ret
HvReadFs ENDP

PUBLIC HvReadGs
HvReadGs PROC
    mov eax, 0
    mov ax, gs
    ret
HvReadGs ENDP

PUBLIC HvReadTr
HvReadTr PROC
    mov eax, 0
    str ax
    ret
HvReadTr ENDP

PUBLIC HvReadLdtr
HvReadLdtr PROC
    mov eax, 0
    sldt ax
    ret
HvReadLdtr ENDP

; VOID HvReadGdtr(HV_DESCRIPTOR_TABLE_REGISTER *Out) ; RCX = Out (10 bytes: 2 limit + 8 base)
PUBLIC HvReadGdtr
HvReadGdtr PROC
    sgdt fword ptr [rcx]
    ret
HvReadGdtr ENDP

; VOID HvReadIdtr(HV_DESCRIPTOR_TABLE_REGISTER *Out)
PUBLIC HvReadIdtr
HvReadIdtr PROC
    sidt fword ptr [rcx]
    ret
HvReadIdtr ENDP

; VM-exit lands here per the VMCS HOST_RIP field, with RSP = VMCS
; HOST_RSP (a dedicated host stack, not the guest's). None of the guest's
; general-purpose registers are saved by hardware on exit (only
; RIP/RSP/RFLAGS/segment/control state, via the host-state VMCS fields),
; so we must save them all before touching anything, and restore them
; before VMRESUME so the guest sees itself untouched except where
; HvHandleVmExit deliberately modified a register (e.g. CPUID results).
EXTERN HvHandleVmExit:PROC

PUBLIC HvExitHandler
HvExitHandler PROC
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rcx, rsp
    sub rsp, 20h
    call HvHandleVmExit
    add rsp, 20h

    test al, al
    jnz HvExitBail

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    vmresume
    ; VMRESUME only returns here on failure.
    int 3

HvExitBail:
    ; Bail-out path (VMXOFF + splice back to native execution) is not
    ; implemented in this build-only skeleton -- deliberately trap
    ; instead of doing something half-correct with live VMX state.
    int 3
HvExitHandler ENDP

END
