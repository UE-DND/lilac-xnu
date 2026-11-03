/* Lilac APFS/process bring-up. Static PID 1 and static exec child share this image.
 * Uses HakiOS libmini; never opens a disk device or writes to the root filesystem.
 */
#include "mini.h"
#ifdef LILAC_UFS_PROBE
#include "ufs-probe.h"
#endif
#ifdef LILAC_SYSTEM_PROBE
#include "system-probe.h"
#endif
#ifdef LILAC_MOBILE_PROBE
#include "mobile-probe.h"
#endif

#define DATA_SIZE (4 * 16384 + 37)
#define WNOHANG 1
#define EINTR 4
#define SIGKILL 9
#define STATIC_OK 17

static volatile unsigned fork_marker = 0x12345678;

/* Apple clang may lower aggregate zero-initialization in libmini to bzero. */
void
bzero(void *address, size_t size)
{
	memset(address, 0, size);
}

static unsigned char
pattern(unsigned offset)
{
	return (unsigned char)((offset * 73 + (offset >> 8) * 19 + 41) & 255);
}

/* Exercise four 16 KiB boundaries and a partial final VM page. */
static int
file_test(void)
{
	unsigned char buf[1009];
	struct stat st;
	int fd = open("/probe/pattern.bin", O_RDONLY, 0);
	if (fd < 0 || fstat(fd, &st) != 0 || st.st_size != DATA_SIZE) {
		printf("APFS file: open/stat failed errno=%d\n", errno);
		if (fd >= 0) close(fd);
		return 1;
	}
	unsigned offset = 0;
	while (offset < DATA_SIZE) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0 || (unsigned)n > DATA_SIZE - offset) goto bad;
		for (ssize_t i = 0; i < n; i++) {
			if (buf[i] != pattern(offset + (unsigned)i)) goto bad;
		}
		offset += (unsigned)n;
	}
	if (read(fd, buf, sizeof(buf)) != 0) goto bad;
	if (pread(fd, buf, 79, 16384 - 23) != 79) goto bad;
	for (unsigned i = 0; i < 79; i++) {
		if (buf[i] != pattern(16384 - 23 + i)) goto bad;
	}
	unsigned char *mapped = mmap(NULL, DATA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (mapped == MAP_FAILED) goto bad;
	for (unsigned i = 0; i < DATA_SIZE; i++) {
		if (mapped[i] != pattern(i)) {
			munmap(mapped, DATA_SIZE);
			goto bad;
		}
	}
	/* File-backed private COW must leave the APFS data unchanged. */
	mapped[16384] ^= 0xff;
	if (pread(fd, buf, 1, 16384) != 1 || buf[0] != pattern(16384) ||
	    mapped[16384] != (unsigned char)(pattern(16384) ^ 0xff)) {
		munmap(mapped, DATA_SIZE);
		goto bad;
	}
	if (munmap(mapped, DATA_SIZE) != 0) goto bad;
	close(fd);
	puts("APFS read/pread/mmap/private-COW: PASS (65573 bytes)");
	return 0;
bad:
	printf("APFS file validation: FAIL offset=%u errno=%d\n", offset, errno);
	close(fd);
	return 1;
}

/* Polling is bounded even if the child never reaches a working userspace exit. */
static int
wait_child(pid_t child, const char *label, int expected)
{
	for (unsigned tick = 0; tick < 350; tick++) {
		int status = 0;
		pid_t got = wait4(child, &status, WNOHANG, NULL);
		if (got == child) {
			printf("%s: pid=%d exit=%d signal=%d raw=0x%x\n", label, child,
			    WIFEXITED(status) ? WEXITSTATUS(status) : -1, WTERMSIG(status), status);
			return tick < 300 && WIFEXITED(status) && WEXITSTATUS(status) == expected;
		}
		if (got < 0 && errno != EINTR) {
			printf("%s: wait4 failed errno=%d\n", label, errno);
			return 0;
		}
		if (tick == 300) {
			long r = mini_syscall(SYS_kill, child, SIGKILL, 0, 0, 0, 0);
			printf("%s: 30-second polling limit; kill=%ld errno=%d\n", label, r, errno);
		}
		if (usleep(100000) != 0 && errno != EINTR) {
			printf("%s: timer failed errno=%d\n", label, errno);
			return 0;
		}
	}
	printf("%s: child not reaped after timeout\n", label);
	return 0;
}

int
main(int argc, char **argv, char **envp)
{
#ifdef LILAC_MOBILE_PROBE
	if(argc==2 && strcmp(argv[1],"launchd-job")==0 && getpid()!=1)return mobile_job();
#endif
	if (argc == 2 && strcmp(argv[1], "apfs-child") == 0) {
		if (strcmp(argv[0], "/bin/apfs_probe") != 0 || getppid() != 1 ||
		    !envp[0] || strcmp(envp[0], "LILAC_CHILD=1") != 0) {
			puts("static exec: argument/environment/parent mismatch");
			return 18;
		}
		printf("static exec child: pid=%d ppid=%d argc=%d\n", getpid(), getppid(), argc);
		return file_test() == 0 ? STATIC_OK : 19;
	}
	if (getpid() != 1) {
		puts("static exec: unexpected arguments; refusing to start another supervisor");
		return 20;
	}

	int fd = open("/dev/console", O_RDWR, 0);
	if (fd < 0) return 90;
	for (int i = 0; i < 3; i++) {
		if (dup2(fd, i) < 0) return 91;
	}
	if (fd > 2) close(fd);
#ifdef LILAC_FAST_BOOT
	if (system_prepare()) mobile_test(1);
	else puts("FAST BOOT: system image registration or mount failed; PID1 remains resident");
	for (;;) { usleep(1000000); }
#else
	printf("\nLilac userspace probe v1: PID %d; resident supervisor\n", getpid());
	puts("Apple dyld diagnostic has no shared cache in this small RAM volume.");
	int static_ok = 0, dynamic_ok = 0;
	pid_t child = fork();
	if (child == 0) {
		fork_marker = 0xaabbccdd;
		char *args[] = { "/bin/apfs_probe", "apfs-child", NULL };
		char *env[] = { "LILAC_CHILD=1", NULL };
		execve(args[0], args, env);
		printf("static execve failed errno=%d\n", errno);
		_exit(127);
	}
	if (child > 0) static_ok = wait_child(child, "static child", STATIC_OK);
	else printf("static fork failed errno=%d\n", errno);
	static_ok = static_ok && fork_marker == 0x12345678;
	printf("fork/exec/wait/APFS: %s\n", static_ok ? "PASS" : "FAIL");
#ifdef LILAC_UFS_PROBE
	int ufs_ok=ufs_test();
#ifdef LILAC_SYSTEM_PROBE
	int system_ok=system_test();
#ifdef LILAC_MOBILE_PROBE
	mobile_test(static_ok && ufs_ok && system_ok);
#endif
#endif
	puts("UFS kernel/process batch complete; resident PID 1 continues timer waits.");
#else
	if (static_ok) {
		puts("Starting /bin/hello_dyn through Apple /usr/lib/dyld");
		child = fork();
		if (child == 0) {
			char *args[] = { "/bin/hello_dyn", "from", "lilac-init", NULL };
			char *env[] = { "PATH=/bin:/usr/bin", "HOME=/", "DYLD_PRINT_LIBRARIES=1", NULL };
			execve(args[0], args, env);
			printf("dynamic execve failed errno=%d\n", errno);
			_exit(127);
		}
		if (child > 0) dynamic_ok = wait_child(child, "Apple dyld child", 7);
		else printf("dynamic fork failed errno=%d\n", errno);
	}
#endif
	/* PID 1 deliberately remains alive, including after a failed experiment. */
	unsigned waits = 0;
	for (;;) {
		if (waits % 30 == 0) {
#ifdef LILAC_UFS_PROBE
			printf("PID1 alive waits=%u APFS=%s UFS=%s; disk read only\n",waits,static_ok?"PASS":"FAIL",ufs_ok?"PASS":"FAIL");
#ifdef LILAC_SYSTEM_PROBE
			printf("Full UFS system: mount=%s paging=%s dynamic=%s\n",system_mount_ok?"PASS":"FAIL",system_data_ok?"PASS":"FAIL",system_ok?"PASS":"NOT PASSED");
#endif
#else
			printf("PID 1 alive; waits=%u; static/APFS=%s; dynamic=%s\n", waits,
			    static_ok ? "PASS" : "FAIL", dynamic_ok ? "PASS" : "NOT PASSED");
#endif
		}
		if (usleep(1000000) == 0) waits++;
		else if (errno != EINTR) {
			printf("resident timer failed errno=%d; sleeping again\n", errno);
			/* Avoid flooding the console if the timer path is broken. */
			for (;;) { }
		}
		int ignored;
		while (wait4(-1, &ignored, WNOHANG, NULL) > 0) { }
	}
#endif
}
