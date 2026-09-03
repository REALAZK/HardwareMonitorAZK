; primitives.asm
; x86-64 architectural primitives: CPUID, RDTSC, RDTSCP, XGETBV.
; MASM (ml64), Windows x64 calling convention.
;
; CpuidResult is 16 bytes (4x uint32_t) -> returned via hidden pointer in RCX
; per the Windows x64 ABI (aggregates >8 bytes always return by reference).

.code

; CpuidResult cpuid_asm(uint32_t leaf, uint32_t subleaf)
; RCX = out ptr, RDX = leaf, R8 = subleaf
PUBLIC cpuid_asm
cpuid_asm PROC
    push rbx
    mov r10, rcx
    mov eax, edx
    mov ecx, r8d
    cpuid
    mov [r10+0], eax
    mov [r10+4], ebx
    mov [r10+8], ecx
    mov [r10+12], edx
    mov rax, r10
    pop rbx
    ret
cpuid_asm ENDP

; uint64_t rdtsc_asm(void)
PUBLIC rdtsc_asm
rdtsc_asm PROC
    rdtsc
    shl rdx, 32
    or rax, rdx
    ret
rdtsc_asm ENDP

; uint64_t rdtscp_asm(uint32_t *aux_out)
; RCX = aux_out ptr (may be NULL)
PUBLIC rdtscp_asm
rdtscp_asm PROC
    mov r10, rcx
    rdtscp
    test r10, r10
    jz short rdtscp_no_aux
    mov [r10], ecx
rdtscp_no_aux:
    shl rdx, 32
    or rax, rdx
    ret
rdtscp_asm ENDP

; uint64_t xgetbv_asm(uint32_t index)
; RCX = index (ECX already holds it for the xgetbv input)
PUBLIC xgetbv_asm
xgetbv_asm PROC
    xgetbv
    shl rdx, 32
    or rax, rdx
    ret
xgetbv_asm ENDP

END
