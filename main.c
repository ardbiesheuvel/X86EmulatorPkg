/*
 *  QEMU Emulator glue
 *
 *  Copyright (c) 2017 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include "assert.h"
#include "qemu-common.h"
#include "qemu/osdep.h"
#include "cache-utils.h"
#include "cpu.h"
#include "tcg.h"
#include "ioport.h"
#include "main.h"
#include "X86Emulator.h"
#include <Library/CpuLib.h>
#include <Library/TimerLib.h>

typedef __SIZE_TYPE__ size_t;   // GCC builtin definition

int singlestep;
volatile int in_critical;

/* The same as cpu_single_env - we only ever run in one context at a time */
CPUState *env;

/*
 * We provide up to 8 nesting stacks. While the guest code is running
 * we do not know the exact value of RSP as it might be stored in a
 * register. So for nesting calls, just use separate stacks.
 */
#define MAX_NESTING 8
int nesting_level = -1;

#define STACK_SIZE (1024 * 1024) /* 1MB */
uint8_t *stacks[MAX_NESTING];

/*
 * In addition to stacks, we also provide 8 nesting CPU environments.
 * That way we don't have to save/restore anything on function entry.
 */
CPUState *envs[MAX_NESTING];

/*
 * This option enables a few sanity checks that happen to trigger from
 * time to time. The only reason I can see why they would trigger is buggy
 * code, but I guess nobody realized that yet, as the buggyness didn't
 * result in crashes. By default, let's be compatible rather than paranoid.
 */
/* #define BE_PARANOID */

/* Use Linux's GDT setup */
static uint64_t gdt_table[16] = {
    [5] = 0xffff | (0xf0000ULL << 32) |				/* Limit */
          (DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
           DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
           (0x2ULL << DESC_TYPE_SHIFT)) << 32,			/* Flags */
    [6] = 0xffff | (0xf0000ULL << 32) |				/* Limit */
          (DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
           DESC_S_MASK | DESC_L_MASK |  (3 << DESC_DPL_SHIFT) |
           (0xaULL << DESC_TYPE_SHIFT)) << 32,			/* Flags */
};

#if 0
#define printf_verbose printf
#else
#define printf_verbose(a,...) do { } while(0)
#endif

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    return 0;
}

int cpu_get_pic_interrupt(CPUX86State *s)
{
    assert(0);
    __builtin_unreachable();
}

uint64_t cpu_get_tsc(CPUX86State *env)
{
    uint64_t r = GetPerformanceCounter();
    printf_verbose("XXX TSC: %lx\n", r);
    return r;
}

void cpu_resume_from_signal(CPUState *env, void *puc)
{
    assert(0);
    __builtin_unreachable();
}

void __assert_fail (const char *__assertion, const char *__file,
                    unsigned int __line, const char *__function)
{
    printf("Assert failed: %a file=%a line=%d function=%a\n", __assertion, __file, __line, __function);
    while (1) ;
}

void abort(void)
{
    asm ("hlt #0x86");  // force an exception so we get a backtrace
    while (1) ;
}

void *malloc(unsigned long size)
{
    int Status;
    void *r = NULL;

    Status = gBS->AllocatePool ( EfiBootServicesData,
                                 size,
                                 &r );

    if (Status != EFI_SUCCESS)
        return NULL;

    return r;
}

void free(void *p)
{
    gBS->FreePool (p);
}

void *realloc(void *ptr, size_t size)
{
    void *newptr;

    if (ptr && !size) {
        free(ptr);
        return NULL;
    }

    newptr = malloc(size);
    if (!ptr || !newptr)
        return newptr;

    memcpy(newptr, ptr, size);
    free(ptr);
    return newptr;
}

int fclose(FILE *f)
{
    return 0;
}

int fflush(FILE *f)
{
    return 0;
}

FILE *fopen(const char *n, const char *m)
{
    return NULL;
}

int getpagesize(void)
{
    return 4096;
}

size_t strlen(const char *s)
{
    return (size_t)AsciiStrSize (s);
}

// clang cludge
#ifdef strdup
#undef strdup
extern char *strdup(const char *s) asm ("__strdup");
#endif
char *strdup(const char *s)
{
    char *result;
    int len;

    len = strlen(s);
    result = (char *)AllocateCopyPool (len + 1, s);
    result[len] = 0;
    return result;
}

void disas(FILE *out, void *code, unsigned long size)
{
    uint8_t *c = (void*)code;
    unsigned long i;

    fprintf(out, "Output code: ");
    for (i = 0; i < size; i++) {
        fprintf(out, "%02x", c[i]);
    }
    fprintf(out, "\n");
}

void target_disas(FILE *out, target_ulong code, target_ulong size, int flags)
{
    //assert(0);
    uint8_t *c = (void*)code;
    target_ulong i;

    fprintf(out, "Input code: ");
    for (i = 0; i < size; i++) {
        fprintf(out, "%02x", c[i]);
    }
    fprintf(out, "\n");
}

void qemu_free(void *ptr)
{
    free(ptr);
}

void *qemu_malloc(size_t size)
{
    return malloc(size);
}

void *qemu_mallocz(size_t size)
{
    void *r = qemu_malloc(size);
    memset(r, 0, size);
    return r;
}

const char *lookup_symbol(target_ulong orig_addr)
{
    return "";
}

static void stack_push64(uint64_t val)
{
    uint64_t rsp = env->regs[R_ESP];

    rsp -= 8;
#ifdef BE_PARANOID
    assert(rsp >= (uintptr_t)stacks[nesting_level]);
#endif

    env->regs[R_ESP] = rsp;
    *(uint64_t*)rsp = val;
}

static uint64_t stack_pop64(void)
{
    uint64_t rsp = env->regs[R_ESP];
    uint64_t r;

#ifdef BE_PARANOID
    assert((rsp + 8) <= (uintptr_t)&stacks[nesting_level][STACK_SIZE]);
#endif

    r = *(uint64_t*)rsp;
    rsp += 8;

    env->regs[R_ESP] = rsp;

    return r;
}

void dump_x86_state(void)
{
    target_disas(stdout, env->eip, 0x100, 0);
    cpu_dump_state(env, stdout, fprintf, 0);
}

bool pc_is_native_return(uint64_t pc)
{
    printf_verbose("XXX Current IP: %llx\n", pc);

    return pc == 0x1234567890abcdefULL;
}

uint64_t run_x86_func(void *func, uint64_t *args)
{
    int trapnr;
    uint64_t r;
    int i;
    const int maxargs = 16;
    uint8_t *stack;
    uintptr_t stack_end;

    /* We can not reenter if a translation is ongoing */
    assert(!in_critical);

    nesting_level++;
    assert(nesting_level < MAX_NESTING);

    cpu_single_env = env = envs[nesting_level];
    stack = stacks[nesting_level];
    stack_end = (uintptr_t)&stack[STACK_SIZE] & ~0x8UL;
    env->regs[R_ESP] = stack_end;

    printf_verbose("XXX Calling x86_64 %llx(%llx, %llx, %llx, %llx, %llx, %llx, %llx, %llx)\n",
                   (uint64_t)func,
                   args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);

    env->regs[R_ECX] = args[0];
    env->regs[R_EDX] = args[1];
    env->regs[8] = args[2];
    env->regs[9] = args[3];

    for (i = 0; i < (maxargs - 4); i++) {
        /* Push arguments on stack in reverse order */
        stack_push64(args[(maxargs - 1) - i]);
    }

    for (i = 0; i < 4; i++) {
        /* Home Zone for the called function */
        stack_push64(0);
    }
    env->eip = (uintptr_t)func;
    /* Return pointer, magic value that brings us back */
    stack_push64(0x1234567890abcdefULL);

    for(;;) {
        unsigned long sp;

        asm volatile ("mov %0, sp" : "=r"(sp));
        printf_verbose("XXX Entering x86 at %lx (sp=%lx)\n", env->eip, sp);
        env->exec_tpl = gBS->RaiseTPL (TPL_NOTIFY);
        in_critical = 1;
        trapnr = cpu_x86_exec(env);
        in_critical = 0;
        gBS->RestoreTPL (env->exec_tpl);
        asm volatile ("mov %0, sp" : "=r"(sp));
        printf_verbose("XXX Left x86 at %lx (sp=%lx)\n", env->eip, sp);
        if (trapnr == EXCP_RETURN_TO_NATIVE) {
            printf_verbose("XXX Return from x86\n");
            break;
        } else if (trapnr == EXCP_CALL_TO_NATIVE) {
            uint64_t (*f)(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                          uint64_t e, uint64_t f, uint64_t g, uint64_t h,
                          uint64_t i, uint64_t j, uint64_t k, uint64_t l,
                          uint64_t m, uint64_t n, uint64_t o, uint64_t p) = (void *)env->eip;
            uint64_t *stackargs = (uint64_t*)env->regs[R_ESP];

            /*
             * MS x86_64 Stack Layout (in uint64_t's):
             *
             * ----------------
             *   ...
             *   arg9
             *   arg8
             *   arg7
             *   arg6
             *   arg5
             *   arg4
             *   home zone (reserved for called function)
             *   home zone (reserved for called function)
             *   home zone (reserved for called function)
             *   home zone (reserved for called function)
             *   return pointer
             * ----------------
             */

            if (env->eip < 0x1000) {
                /* Calling into the zero page, this is broken code. Shout out loud. */
                printf("Invalid jump to zero page from caller %llx\n", *stackargs);
                dump_x86_state();
#ifdef BE_PARANOID
                assert(env->eip >= 0x1000);
#endif

                /* Try to rescue ourselves as much as we can */
                env->regs[R_EAX] = EFI_UNSUPPORTED;
                env->eip = stack_pop64();
            }

            printf_verbose("XXX  Calling aarch64 %p(%llx, %llx, %llx, %llx, %llx, %llx, %llx, %llx)\n",
                           f, env->regs[R_ECX], env->regs[R_EDX], env->regs[8],
                           env->regs[9], stackargs[5], stackargs[6], stackargs[7],
                           stackargs[8]);
            assert(!(env->eip & 0x3)); /* Make sure we're calling aarch64 code which is aligned */
            env->regs[R_EAX] = f(env->regs[R_ECX], env->regs[R_EDX], env->regs[8], env->regs[9],
                                 stackargs[5], stackargs[6], stackargs[7], stackargs[8],
                                 stackargs[9], stackargs[10], stackargs[11], stackargs[12],
                                 stackargs[13], stackargs[14], stackargs[15], stackargs[16]);
            printf_verbose("XXX  Finished aarch64 call to %p (return to %lx)\n", f, stackargs[0]);
            env->eip = stack_pop64();
        } else if (trapnr == EXCP_HLT) {
            CpuSleep ();
            env->halted = 0;
        } else {
            printf("XXX  Trap: #%x (eip=%lx)\n", trapnr, env->eip);
            dump_x86_state();
            ASSERT(FALSE);
            break;
        }
    }

    /* Pop stack passed parameters */
    for (i = 0; i < 4; i++) {
        /* Home Zone, modifyable by function */
        stack_pop64();
    }
    for (; i < maxargs; i++) {
        /* Double check that nobody modified the arg */
        uint64_t curarg = stack_pop64();

        if (curarg != args[i]) {
#ifdef BE_PARANOID
            printf("Argument %d mismatch at RSP=%llx: %llx vs %llx\n",
                   i, env->regs[R_ESP], curarg, args[i]);
            assert(curarg == args[i]);
#endif
        }
    }

    assert(env->regs[R_ESP] == stack_end);
    nesting_level--;

    /* Restore old context */
    r = env->regs[R_EAX];
    cpu_single_env = env = envs[nesting_level];

    return r;
}

int x86emu_init(void)
{
    int i;

    x86_cpudef_setup();
    cpu_set_log_filename("qemulog");
    cpu_set_log(0);
    cpu_exec_init_all(0);

    /* Populate our env copies */
    for (i = 0; i < MAX_NESTING; i++) {
        envs[i] = cpu_init("qemu64");

        env = envs[i];
        assert(env);
        cpu_reset(env);

        /* We run everything in user mode, UEFI drivers *should* not need CPL0 */
        cpu_x86_set_cpl(env, 3);

        /* Enable user mode, paging, 64bit, sse */
        env->cr[0] = CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK;
        env->cr[4] |= CR4_OSFXSR_MASK | CR4_PAE_MASK;
        env->hflags |= HF_PE_MASK | HF_OSFXSR_MASK | HF_LMA_MASK;
        env->efer |= MSR_EFER_LMA | MSR_EFER_LME;

        /* Map CS/DS as flat, CS as 64bit code */
        env->gdt.base = (uintptr_t)gdt_table;
        env->gdt.limit = sizeof(gdt_table) - 1;
        cpu_x86_load_seg(env, R_CS, 0x33);
        cpu_x86_load_seg(env, R_SS, 0x2B);
        cpu_x86_load_seg(env, R_DS, 0);
        cpu_x86_load_seg(env, R_ES, 0);
        cpu_x86_load_seg(env, R_FS, 0);
        cpu_x86_load_seg(env, R_GS, 0);

        /* Initialize x86 stack at 16-byte boundary */
        stacks[i] = malloc(STACK_SIZE);
    }

    return 0;
}
