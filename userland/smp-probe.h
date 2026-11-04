/* SPDX-License-Identifier: BSD-2-Clause */
#include <pthread.h>
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <sys/sysctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#ifndef LILAC_EXPECT_CPUS
#define LILAC_EXPECT_CPUS 1
#endif
static volatile uint32_t smp_seen, smp_bad;
static volatile uint32_t *smp_page;
static uint64_t smp_deadline;
static pthread_t smp_threads[8];
static unsigned smp_created;
static void *smp_work(void *unused)
{
    (void)unused; volatile uint64_t sum=0;
    while (mach_absolute_time()<smp_deadline) {
        for (unsigned i=0;i<20000;i++) sum+=i*(uint64_t)i;
        if (*smp_page!=0x8998540) __sync_fetch_and_or(&smp_bad,1);
        size_t cpu=0;
        if (!pthread_cpu_number_np(&cpu) && cpu<8) __sync_fetch_and_or(&smp_seen,1u<<cpu);
    }
    return NULL;
}
static int smp_probe(void)
{
    int active=0; size_t size=sizeof(active);
    if (sysctlbyname("hw.activecpu",&active,&size,NULL,0) || active<LILAC_EXPECT_CPUS || active>8) return 0;
    if (active==1) { puts("SMP WORK: SKIP single CPU requested"); return 1; }
    processor_info_array_t before=NULL,after=NULL;
    mach_msg_type_number_t nb=0,na=0; natural_t cb=0,ca=0;
    /* Project mach/arm/processor_info.h: ten packed 64-bit counters per CPU. */
    if (host_processor_info(mach_host_self(),0x10000004,&cb,&before,&nb) || cb!=(unsigned)active || nb!=20*cb) return 0;
    smp_page=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if (smp_page==MAP_FAILED) return 0;
    *smp_page=0x8998540;
    mach_timebase_info_data_t t; mach_timebase_info(&t);
    smp_deadline=mach_absolute_time()+3000000000ull*t.denom/t.numer;
    for (;smp_created<(unsigned)active;smp_created++) if (pthread_create(&smp_threads[smp_created],NULL,smp_work,NULL)) break;
    /* Broadcast pmap permission changes while readers run on other cores. */
    int ok=smp_created==(unsigned)active;
    for (unsigned i=0;i<256;i++) {
        if (mprotect((void *)smp_page,16384,PROT_READ) || mprotect((void *)smp_page,16384,PROT_READ|PROT_WRITE)) ok=0;
    }
    for (unsigned i=0;i<smp_created;i++) if (pthread_join(smp_threads[i],NULL)) ok=0;
    munmap((void *)smp_page,16384);
    if (host_processor_info(mach_host_self(),0x10000004,&ca,&after,&na) || ca!=cb || na!=nb) ok=0;
    if (after && ca==cb && na==nb) for (unsigned i=0;i<ca;i++) {
        uint64_t bi,ai,bt,at;
        memcpy(&bi,(char *)before+80*i+8,8); memcpy(&ai,(char *)after+80*i+8,8);
        memcpy(&bt,(char *)before+80*i+16,8); memcpy(&at,(char *)after+80*i+16,8);
        printf("SMP IRQ: cpu=%u IPI=%llu timer=%llu\n",i,ai-bi,at-bt);
        if (ai<=bi || at<=bt) ok=0;
    }
    vm_deallocate(mach_task_self(),(vm_address_t)before,nb*sizeof(integer_t));
    if (after) vm_deallocate(mach_task_self(),(vm_address_t)after,na*sizeof(integer_t));
    ok=ok && !smp_bad && smp_seen==((1u<<active)-1);
    printf("SMP WORK: %s active=%d executed_mask=%x permission_changes=512 data_errors=%u\n",ok?"PASS":"FAIL",active,smp_seen,smp_bad);
    return ok;
}
