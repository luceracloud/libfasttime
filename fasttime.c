#include <assert.h>
#include <dlfcn.h>
#ifdef __sun
#include <fcntl.h>
#endif
/* remove limits when done debugging */
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef __linux
#include <string.h>
#endif
#ifdef __sun
#include <sys/processor.h>
#include <sys/stat.h>
#endif
#include <sys/types.h>
#include <time.h>
#ifdef __sun
#include <unistd.h>
#endif


static void __attribute__ ((constructor)) _init_fasttime();

static struct timespec	base_ts = {0,0}; /* used by clock_gettime() {s,ns} */
static uint64_t		base_tsc = 0UL;	/* base nanoseconds from TSC */
static uint64_t		nsec_scale;	/* NANOSEC / CPU Hz */

/*
 * Pointers to system functions.
 */
static int (*_sys_clock_gettime)(clockid_t clock_id, struct timespec *tp);

#ifdef __linux
#define NANOSEC 1000000000
#endif
#define	MHZ_TO_HZ(mhz)	(mhz * 1000000)
#define	NSEC_SHIFT 5
#define	TSC_CONVERT(tsc, scale)						\
	(tsc.tsc_64 =							\
	    (((uint64_t)tsc.tsc_32[1] * nsec_scale) << NSEC_SHIFT) +	\
	    (((uint64_t)tsc.tsc_32[0] * nsec_scale) >> (32 - NSEC_SHIFT)))

typedef union tscu {
	uint64_t tsc_64;
	uint32_t tsc_32[2];
} tscu_t;

#ifdef __sun

/*
 * Determine if proper TSC support . If proper TSC
 * is not available, or there are issues determining if support is
 * available, then print an error and exit the process.
 */
static void
check_tsc()
{
	int fd;
	struct {
		uint32_t r_eax, r_ebx, r_ecx, r_edx;
	} _r, *rp = &_r;

	if ((fd = open("/dev/cpu/self/cpuid", O_RDONLY)) == -1) {
		perror("failed to open /dev/cpu/self/cpuid");
		exit(1);
	}

	if (pread(fd, rp, sizeof (*rp), 1) != sizeof (*rp)) {
		perror("failed to read CPUID.1");
		exit(1);
	}

	/*
	 * CPUID.1:EDX[4] -- presence of TSC
	 */
	if ((rp->r_edx & 0x10) == 0) {
		perror("No TSC present (CPUID.1:EDX[4])");
		exit(1);
	}

	if (pread(fd, rp, sizeof (*rp), 0x80000001) != sizeof (*rp)) {
		perror("failed to read CPUID.80000001H");
		exit(1);
	}

	/*
	 * CPUID.80000001H:EDX[27] -- presence of invariant TSC
	 *
	 * The invariant TSC does not fluctuate during transitions in
	 * ACPI P-, C-, and T-states.
	 */
	if ((rp->r_edx & 0x8000000) == 0) {
		perror("No invariant TSC present (CPUID.80000001H:EDX[27])");
		exit(1);
	}

	(void) close(fd);
}

static int
get_cpu_mhz()
{
	processor_info_t pinfo;

	if (processor_info(getcpuid(), &pinfo) == -1) {
		perror("failed to get processor info");
		exit(1);
	}

	return (pinfo.pi_clock);
}

#elif __linux

/*
 * XXX LX is not exposing all the flags that it should in
 *     /proc/cpuinfo. That should be fixed. In the meantime we could
 *     use inline assembly but for the sake of time I'm just going to
 *     noop for now. We control our hardware so the check is more of a
 *     paranoid sanity check than a requirement. That changes if this
 *     ever becomes a public library.
 */
void
check_tsc()
{
	return;
}

/*
 * Linux, go home, you're drunk.
 */
static int
get_cpu_mhz()
{
	char *cpu_freq_s, line[64];
	double cpu_freq_d;
	int freq_found = 0, rc;
	FILE *fp;

	if ((fp = fopen("/proc/cpuinfo", "r")) == NULL) {
		perror("failed to open /proc/cpuinfo");
		exit(1);
	}

	while (fgets(line, 64, fp) != NULL) {
		if (strncmp("cpu MHz", line, 7) == 0) {
			freq_found = 1;
			break;
		}
	}

	if (freq_found != 1) {
		fprintf(stderr, "failed to determine CPU frequency\n");
		exit(1);
	}

	if ((cpu_freq_s = strchr(line, ':')) == NULL) {
		fprintf(stderr, "failed to extract cpu MHz value\n");
		exit(1);
	}

	/* Skip the ": " sequence. */
	cpu_freq_s = &cpu_freq_s[2];
	rc = sscanf(cpu_freq_s, "%lf", &cpu_freq_d);

	if ((rc != 1) || (rc == EOF)) {
		perror("failed to parse cpu MHz value");
		exit (1);
	}

	return (lrint(cpu_freq_d));
}

#endif

/*
 * The retrieval of the clock time and the TSC are not atomic, there
 * may be time unaccounted for.
 *
 * Could profile the RDTSC call at startup and use that measurement to
 * determine lost time...I'm not kidding, this comes from an Intel
 * guide about benchmarking and TSC.
 *
 * Now, if the clock time is stored somewhere whenever the TSC is
 * reset then I could use that value as the base and it would be
 * accurate, but for now use this hack.
 */
static void
_init_fasttime()
{
	unsigned int a, d, approx_cpu_hz;

	(void) check_tsc();

	if ((_sys_clock_gettime = dlsym(RTLD_NEXT, "clock_gettime")) == NULL) {
		perror("failed to load system clock_gettime()");
		exit(1);
	}

	/*
	 * The approximate value of the kernel's cpu_freq_hz.
	 * Approximate because the kernel uses emperical readings of
	 * the TSC against PIT timeouts to determine the clock
	 * frequency. The pi_clock value should be based on this value
	 * but some of the precision is lost, not sure if that
	 * matters much in practice.
	 */
	approx_cpu_hz = MHZ_TO_HZ(get_cpu_mhz());
	nsec_scale =
	    (uint64_t)(((uint64_t)NANOSEC << (32 - NSEC_SHIFT)) / approx_cpu_hz);

	if (_sys_clock_gettime(CLOCK_REALTIME, &base_ts) == -1) {
		perror("failed to init fasttime base");
		exit(1);
	}

	/*
	 * Since I'm pulling the TSC _after_ the clock nanos it means
	 * that the gettimeofday() derived from the TSC deltas may be
	 * behind the real kernel clock because of missing nanos.
	 *
	 */
	__asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
	base_tsc = ((uint64_t)a) | ((uint64_t)d) << 32;
}


int
gettimeofday(struct timeval *tp, void __attribute__((unused)) *tzp)
{
	unsigned int a, d;
	tscu_t tsc;

	if (tp == NULL)
		return (0);

	__asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
	tsc.tsc_64 = (((uint64_t)a) | ((uint64_t)d) << 32) - base_tsc;
	TSC_CONVERT(tsc, nsec_scale);
	time_t sec = tsc.tsc_64 / NANOSEC;
	long nsec = tsc.tsc_64 % NANOSEC;

	tp->tv_sec = base_ts.tv_sec + sec;
	tp->tv_usec = (base_ts.tv_nsec + nsec) / 1000;

	assert(tp->tv_sec > -1);
	assert(tp->tv_usec > -1 && tp->tv_usec < 1000000);

	return (0);
}

#ifdef __sun

hrtime_t
gethrtime()
{
	unsigned int a, d;
	tscu_t tsc;

	__asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
	tsc.tsc_64 = (((hrtime_t)a) | ((hrtime_t)d) << 32);
	TSC_CONVERT(tsc, nsec_scale);

	return ((hrtime_t)tsc.tsc_64);
}

#endif

int
clock_gettime(clockid_t clock_id, struct timespec *tp)
{
	unsigned int a, d;
	tscu_t tsc;

	switch (clock_id) {
	case CLOCK_REALTIME:
		__asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
		tsc.tsc_64 = (((uint64_t)a) | ((uint64_t)d) << 32) - base_tsc;
		TSC_CONVERT(tsc, nsec_scale);
		time_t sec = tsc.tsc_64 / NANOSEC;
		long nsec = tsc.tsc_64 % NANOSEC;
		tp->tv_sec = base_ts.tv_sec + sec;
		tp->tv_nsec = base_ts.tv_nsec + nsec;
		assert(tp->tv_sec > -1);
		assert(tp->tv_nsec > -1 && tp->tv_nsec < NANOSEC);
		break;

	case CLOCK_MONOTONIC:
		__asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
		tsc.tsc_64 = (((uint64_t)a) | ((uint64_t)d) << 32);
		TSC_CONVERT(tsc, nsec_scale);
		tp->tv_sec = tsc.tsc_64 / NANOSEC;
		tp->tv_nsec = tsc.tsc_64 % NANOSEC;
		assert(tp->tv_sec > -1);
		assert(tp->tv_nsec > -1 && tp->tv_nsec < NANOSEC);
		break;

	default:
		_sys_clock_gettime(clock_id, tp);
		break;
	}

	return (0);
}
