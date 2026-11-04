/* SPDX-License-Identifier: BSD-2-Clause
 * Two processes alternate GPU writes to the SAME virtual address. Each must
 * retain its own sentinel after the other finishes. No CPU writes the sentinel.
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/wait.h>
#include "../kernel/lilac-a5xx-pm4.h"
struct isolation_buffer { io_connect_t connection; uint32_t handle; uint64_t va; mach_vm_address_t cpu; };
static int isolation_call(io_connect_t c, uint32_t op, void *data, size_t size)
{
    size_t out=size; return IOConnectCallStructMethod(c,op,data,size,data,&out)==KERN_SUCCESS && out==size;
}
static int isolation_open(struct isolation_buffer *b)
{
    io_service_t s=IOServiceGetMatchingService(0,IOServiceMatching("HakiosGPU"));
    if (!s) return 0;
    kern_return_t r=IOServiceOpen(s,mach_task_self(),0,&b->connection); IOObjectRelease(s);
    if (r) return 0;
    struct { uint64_t size; uint32_t flags,handle; } make={16384,0,0};
    if (!isolation_call(b->connection,0x42,&make,sizeof(make))) return 0;
    b->handle=make.handle;
    struct { uint32_t handle,info; uint64_t value; uint32_t len,pad; } info={b->handle,1,0,0,0};
    if (!isolation_call(b->connection,0x43,&info,sizeof(info))) return 0;
    b->va=info.value; mach_vm_size_t size=16384;
    return !IOConnectMapMemory64(b->connection,b->handle,mach_task_self(),&b->cpu,&size,kIOMapAnywhere);
}
static int isolation_target(struct isolation_buffer *b, uint32_t value, uint64_t target)
{
    struct lilac_pm4 packet={(uint32_t *)(uintptr_t)b->cpu,32,0};
    if (target==0x831) {
        uint32_t zero=0,one=1;
        /* An IB must not disable protection and grant itself APRIV. */
        lilac_pm4_type7(&packet,0x5f,&zero,1);
        lilac_pm4_type4(&packet,0x831,&one,1);
        lilac_pm4_type7(&packet,0x5f,&one,1);
        target=0x02018040;
    }
    uint32_t data[]={(uint32_t)target,(uint32_t)(target>>32),value};
    if (!lilac_pm4_type7(&packet,0x3d,data,3)) return 0;
    struct { uint32_t flags,handle; uint64_t presumed; } bo={3,b->handle,b->va};
    struct { uint32_t type,index,offset,size,pad,relocs; uint64_t reloc; } cmd={1,0,0,(uint32_t)packet.used*4,0,0,0};
    struct { uint32_t flags,fence,bos,cmds; uint64_t bo,cmd; int32_t fd; uint32_t queue;
        uint64_t in,out; uint32_t ni,no,stride,pad; } submit={0};
    submit.bos=submit.cmds=1; submit.bo=(uintptr_t)&bo; submit.cmd=(uintptr_t)&cmd;
    if (!isolation_call(b->connection,0x46,&submit,sizeof(submit))) return 0;
    mach_timebase_info_data_t t; mach_timebase_info(&t);
    uint64_t until=mach_continuous_time()*t.numer/t.denom+2000000000ull;
    struct { uint32_t fence,flags; int64_t seconds,nanos; uint32_t queue; } wait={submit.fence,0,(int64_t)(until/1000000000),(int64_t)(until%1000000000),0};
    return isolation_call(b->connection,0x47,&wait,sizeof(wait)) ? 1 : -1;
}
static int isolation_write(struct isolation_buffer *b,uint32_t value)
{
    return isolation_target(b,value,b->va+256)==1 && *(volatile uint32_t *)(uintptr_t)(b->cpu+256)==value;
}
static void isolation_close(struct isolation_buffer *b)
{
    IOConnectUnmapMemory64(b->connection,b->handle,mach_task_self(),b->cpu);
    IOServiceClose(b->connection);
}
static int isolation_probe(void)
{
    int to_child[2],to_parent[2];
    if (pipe(to_child) || pipe(to_parent)) return 0;
    pid_t child=fork(); if (child<0) return 0;
    if (!child) {
        close(to_child[1]);close(to_parent[0]);
        struct isolation_buffer b={0}; uint32_t token; int ok=isolation_open(&b);
        if (write(to_parent[1],&b.va,8)!=8) _exit(1);
        for (unsigned i=0;ok && i<32;i++) {
            if (read(to_child[0],&token,4)!=4) break;
            if (i && *(volatile uint32_t *)(uintptr_t)(b.cpu+256)!=(0xb5400000u+i-1)) ok=0;
            if (ok) ok=isolation_write(&b,0xb5400000u+i);
            token=ok;
            if (write(to_parent[1],&token,4)!=4) {ok=0;break;}
        }
        if (b.connection) isolation_close(&b);
        _exit(ok?0:1);
    }
    close(to_child[0]);close(to_parent[1]);
    struct isolation_buffer a={0}; uint64_t other=0; uint32_t token=1;
    int ok=isolation_open(&a) && read(to_parent[0],&other,8)==8 && other==a.va;
    for (unsigned i=0;ok && i<32;i++) {
        ok=isolation_write(&a,0xa5400000u+i);
        if (!ok || write(to_child[1],&token,4)!=4 || read(to_parent[0],&token,4)!=4 || !token) {ok=0;break;}
        ok=*(volatile uint32_t *)(uintptr_t)(a.cpu+256)==(0xa5400000u+i);
    }
    close(to_child[1]);close(to_parent[0]);
    int status=0; if (waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)) ok=0;
    if (a.connection) isolation_close(&a);
    printf("A540 PROCESS ISOLATION: %s same VA=%llx peer=%llx, 32 alternating GPU writes per process\n",ok?"PASS":"FAIL",a.va,other);
    return ok;
}

static int isolation_protection(void)
{
    /* The private completion page, read-only kernel ring, and an unmapped
     * user VA. Each rejected client is followed by a healthy new client. */
    const uint64_t targets[]={0x02018040,0x02000000,0x04010000,0x831};
    for (unsigned i=0;i<4;i++) {
        struct isolation_buffer bad={0},good={0};
        int ok=isolation_open(&bad) && isolation_target(&bad,0xabcdef01,targets[i])==-1;
        if (bad.connection) isolation_close(&bad);
        ok=ok && isolation_open(&good) && isolation_write(&good,0x54000000+i);
        if (good.connection) isolation_close(&good);
        printf("A540 PROTECTION: %s target=%llx rejected and next client completed\n",ok?"PASS":"FAIL",targets[i]);
        if (!ok) return 0;
    }
    return 1;
}

struct resume_allocation {
    io_connect_t connection;
    atomic_int started, stop;
    unsigned allocations, errors;
    kern_return_t allocation_error;
};
static void *resume_allocate(void *argument)
{
    struct resume_allocation *state=argument;
    atomic_store(&state->started,1);
    while (!atomic_load(&state->stop)) {
        struct { uint64_t size; uint32_t flags,handle; } make={16384,0,0};
        size_t returned=sizeof(make);
        kern_return_t result=IOConnectCallStructMethod(state->connection,0x42,
            &make,sizeof(make),&make,&returned);
        if (result || returned!=sizeof(make)) {
            state->allocation_error=result;
            state->errors++; break;
        }
        state->allocations++;
        struct { uint32_t handle,pad; } close={make.handle,0};
        if (!isolation_call(state->connection,0x09,&close,sizeof(close))) {
            state->errors++; break;
        }
        usleep(1000);
    }
    return NULL;
}
static int resume_allocation_probe(void)
{
    struct isolation_buffer buffer={0};
    unsigned allocations=0,errors=0,cycles=0;
    kern_return_t allocation_error=0;
    int ok=isolation_open(&buffer);
    for (;ok && cycles<4;cycles++) {
        usleep(800000); /* The next submission must wake the idle GPU. */
        struct resume_allocation state={.connection=buffer.connection};
        pthread_t worker;
        if (pthread_create(&worker,NULL,resume_allocate,&state)) {ok=0;break;}
        while (!atomic_load(&state.started)) usleep(100);
        usleep(2000);
        /* Another CPU keeps requesting GEM_NEW while this one resumes. */
        ok=isolation_write(&buffer,0x540a0000u+cycles);
        atomic_store(&state.stop,1);
        pthread_join(worker,NULL);
        allocations+=state.allocations;errors+=state.errors;
        if (state.allocation_error) allocation_error=state.allocation_error;
        ok=ok && state.allocations && !state.errors;
    }
    if (buffer.connection) isolation_close(&buffer);
    printf("A540 RESUME ALLOCATION: %s cycles=%u allocations=%u errors=%u allocation_error=%x\n",
        ok?"PASS":"FAIL",cycles,allocations,errors,allocation_error);
    return ok;
}
