/*
 * XXX This file still needs a lot of work.
 *
 * This file contains tests to verify that libfasttime is not doing
 * anything horribly wrong. It contains short tests for things that
 * can be verified quickly and long-running tests for bugs which take
 * physical time to manifest (such as TSC clock drift). Some of these
 * tests appear in both categories where the only difference is the
 * length of time you want to assert their trueness.
 */
#include <assert.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef __sun
#include <sys/processor.h>
#include <sys/procset.h>
#endif
#ifdef __linux
#include <sched.h>
#endif

/*
 * Tests to implement:
 *
 * - Verify monoticity for POSIX clock and gethrtime, they should
 *   never go backwards. (short & long)
 *
 * - Verify fasttime functions are within a certain bound of the
 *   system, e.g. make sure that the system's gettimeofday() and
 *   fasttime's gettimeofday() don't diverge more than they should.
 *   (short & long)
 *
 * - Verify that fasttime notices changes in the system clock. This
 *   feature has yet to be implemented.
 *
 * - Verify that fasttime doesn't go backwards when switching between
 *   cores. Could do this with processor_bind on illumos.
 */

#ifdef __linux
#define	MICROSEC		1000000
#define	NANOSEC			1000000000
typedef	int			processorid_t;
#endif

#define	MS_TO_NS(ms)		(ms * 1000000)
#define	TIMESPEC_TO_NS(ts)	(((uint64_t)ts.tv_sec * NANOSEC) + ts.tv_nsec)
#define	TIMEVAL_TO_US(tv)	(((uint64_t)tv.tv_sec * MICROSEC) + tv.tv_usec);

/*
 * Pointers to system functions, loaded by libfasttime.so.
 */
extern int (*_sys_clock_gettime)(clockid_t clock_id, struct timespec *tp);
#ifdef __sun
extern int (*_sys_gettimeofday)(struct timeval *tp, void *tzp);
#elif __linux
extern int (*_sys_gettimeofday)(struct timeval *tp, struct timezone *tz);
#endif

#ifdef __sun

/*
 * Get the active CPUs, output via the cpus array. On input size
 * represents the size of cpus, on output it represents the number of
 * CPUs active. On success 0 is returned.
 */
int
get_cpus(processorid_t **cpus, size_t *size)
{
	int num_cpus = sysconf(_SC_CPUID_MAX);
	processorid_t i, j;

	if ((*cpus = calloc(sizeof (processorid_t), num_cpus)) == NULL) {
		perror("failed to calloc()");
	}

	for (i = 0, j = 0; i < num_cpus; i++) {
		if (p_online(i, P_STATUS) != -1)
			(*cpus)[j++] = i;
	}
	*size = j;

	return (0);
}

#elif __linux

int
get_cpus(processorid_t **cpus, size_t *size)
{
	int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	processorid_t i;

	if ((*cpus = calloc(sizeof (processorid_t), num_cpus)) == NULL) {
		perror("failed to calloc()");
	}

	for (i = 0; i < num_cpus; i++) {
		(*cpus)[i] = i;
	}
	*size = i;

	return (0);
}

#endif

void
test_gettimeofday_delta(int64_t max_delta_us)
{
	struct timeval	ft_tv;	/* fasttime time */
	struct timeval	sys_tv;	/* system time */
	uint64_t	ft_us;	/* fasttime micros */
	uint64_t	sys_us;	/* system micros */
	int64_t		delta_us; /* absolute delta */

	if (gettimeofday(&ft_tv, NULL) == -1) {
		perror("failed to call libfasttime gettimeofday()\n");
		exit(1);
	}

	if (_sys_gettimeofday(&sys_tv, NULL) == -1) {
		perror("failed to call system gettimeofday()\n");
		exit(1);
	}

	if (ft_tv.tv_sec < 0 || ft_tv.tv_usec < 0 ||
	    ft_tv.tv_usec >= 1000000) {
		printf("ERROR: bad timeval from libfasttime\n");
		printf("tv.sec: %ld tv.usec: %ld\n",
		    ft_tv.tv_sec, ft_tv.tv_usec);
		exit(1);
	}

	if (sys_tv.tv_sec < 0 || sys_tv.tv_usec < 0 ||
	    sys_tv.tv_usec >= 1000000) {
		printf("ERROR: bad timeval from system\n");
		printf("tv.sec: %ld tv.usec: %ld\n",
		    sys_tv.tv_sec, sys_tv.tv_usec);
		exit(1);
	}

	ft_us = TIMEVAL_TO_US(ft_tv);
	sys_us = TIMEVAL_TO_US(sys_tv);
	delta_us = labs(sys_us - ft_us);
	assert(delta_us >= 0);

	if (delta_us > max_delta_us) {
		printf("ERROR: (1) sys and lib time diverge too much\n");
		printf("\tsys\tsec: %10ld usec: %7ld\n",
		    sys_tv.tv_sec, sys_tv.tv_usec);
		printf("\tlib\tsec: %10ld usec: %7ld\n",
		    ft_tv.tv_sec, ft_tv.tv_usec);
		printf("\tdelta\tsec: %10ld usec: %7ld\n",
		    labs(sys_tv.tv_sec - ft_tv.tv_sec),
		    labs(sys_tv.tv_usec - ft_tv.tv_usec));
		exit(1);
	}

	/* Test again, but in opposite order. */
	if (_sys_gettimeofday(&sys_tv, NULL) == -1) {
		perror("failed to call system gettimeofday()\n");
		exit(1);
	}

	if (gettimeofday(&ft_tv, NULL) == -1) {
		perror("failed to call libfasttime gettimeofday()\n");
		exit(1);
	}

	if (ft_tv.tv_sec < 0 || ft_tv.tv_usec < 0 ||
	    ft_tv.tv_usec >= 1000000) {
		printf("ERROR: bad timeval from libfasttime\n");
		printf("tv.sec: %ld tv.usec: %ld\n",
		    ft_tv.tv_sec, ft_tv.tv_usec);
		exit(1);
	}

	if (sys_tv.tv_sec < 0 || sys_tv.tv_usec < 0 ||
	    sys_tv.tv_usec >= 1000000) {
		printf("ERROR: bad timeval from system\n");
		printf("tv.sec: %ld tv.usec: %ld\n",
		    sys_tv.tv_sec, sys_tv.tv_usec);
		exit(1);
	}

	ft_us = TIMEVAL_TO_US(ft_tv);
	sys_us = TIMEVAL_TO_US(sys_tv);
	delta_us = sys_us - ft_us;
	delta_us = delta_us < 0 ? (delta_us * -1) : delta_us;
	assert(delta_us >= 0);

	if (delta_us > max_delta_us) {
		printf("ERROR: (2) sys and lib time diverge too much\n");
		printf("\tsys\tsec: %10ld usec: %7ld\n",
		    sys_tv.tv_sec, sys_tv.tv_usec);
		printf("\tlib\tsec: %10ld usec: %7ld\n",
		    ft_tv.tv_sec, ft_tv.tv_usec);
		printf("\tdelta\tsec: %10ld usec: %7ld\n",
		    labs(sys_tv.tv_sec - ft_tv.tv_sec),
		    labs(sys_tv.tv_usec - ft_tv.tv_usec));
		exit(1);
	}

}

void
test_posix_monotonic(const struct timespec *sleep)
{
	struct timespec a_ts, b_ts;
	uint64_t a_ns, b_ns;

	if (clock_gettime(CLOCK_MONOTONIC, &a_ts) == -1) {
		perror("failed to query monotonic clock");
		exit(1);
	}

	if (sleep != NULL)
		nanosleep(sleep, NULL);

	if (clock_gettime(CLOCK_MONOTONIC, &b_ts) == -1) {
		perror("failed to query monotonic clock");
		exit(1);
	}

	a_ns = TIMESPEC_TO_NS(a_ts);
	b_ns = TIMESPEC_TO_NS(b_ts);

	/*
	 * If the current time is less than or equal to the previous
	 * time than monotonicity was not held.
	 */
	if (b_ns <= a_ns) {
		printf("ERROR: test_posix_monotonic() failed\n");
		printf("\ta_ns: %" PRIu64 "\n", a_ns);
		printf("\tb_ns: %" PRIu64 "\n", b_ns);
		exit(1);
	}
}

#ifdef __sun

void
test_posix_xcore(processorid_t cpus[], size_t num_cpus)
{
	processorid_t	a_cpu, b_cpu;
	uint64_t	a_ns, b_ns;
	struct timespec	a_ts, b_ts;

	a_cpu = getcpuid();

	/* XXX this will loop forever on single CPU */
	do {
		/*
		 * I realize this is only using the bottom bits and
		 * therefore skews the distribution but that's quite
		 * alright for the purposes of this test.
		 */
		b_cpu = cpus[rand() % num_cpus];
	} while (a_cpu == b_cpu);

	if (processor_bind(P_PID, P_MYID, a_cpu, NULL) == -1) {
		perror("failed to bind process");
		exit(1);
	}

	if (clock_gettime(CLOCK_MONOTONIC, &a_ts) == -1) {
		perror("failed to query monotonic clock");
		exit(1);
	}

	if (processor_bind(P_PID, P_MYID, b_cpu, NULL) == -1) {
		perror("failed to bind process");
		exit(1);
	}

	if (clock_gettime(CLOCK_MONOTONIC, &b_ts) == -1) {
		perror("failed to query monotonic clock");
		exit(1);
	}

	a_ns = TIMESPEC_TO_NS(a_ts);
	b_ns = TIMESPEC_TO_NS(b_ts);

	/*
	 * If the current time is less than or equal to the previous
	 * time than monotonicity was not held.
	 */
	if (b_ns <= a_ns) {
		printf("ERROR: test_posix_monotonic() failed\n");
		printf("\ta_ns: %" PRIu64 "\n", a_ns);
		printf("\tb_ns: %" PRIu64 "\n", b_ns);
		exit(1);
	}

}

#elif __linux

void
test_posix_xcore(processorid_t cpus[], size_t __attribute__((unused)) num_cpus)
{
	cpu_set_t	cpuset;
	processorid_t	a_cpu, b_cpu;
	uint64_t	a_ns, b_ns;
	struct timespec	a_ts, b_ts;

	/* XXX sched_getcpu() always returns same value even after binding */

	/* if ((a_cpu = sched_getcpu()) == -1) { */
	/* 	perror("failed to get current CPU"); */
	/* 	exit(1); */
	/* } */

	/* XXX Either there is a bug in LX or something I don't
	 * understand about Linux. Use only have the online CPUs for
	 * now. */
	a_cpu = cpus[rand() % 31];

	/* XXX this will loop forever on single CPU */
	do {
		/*
		 * I realize this is only using the bottom bits and
		 * therefore skews the distribution but that's quite
		 * alright for the purposes of this test.
		 */
		b_cpu = cpus[rand() % 31];
	} while (a_cpu == b_cpu);

	CPU_ZERO(&cpuset);
	CPU_SET(a_cpu, &cpuset);

	if (sched_setaffinity(0, sizeof (cpu_set_t), &cpuset) == -1) {
		perror("failed to bind process");
		exit(1);
	}

	if (clock_gettime(CLOCK_MONOTONIC, &a_ts) == -1) {
		perror("failed to query monotonic clock");
		exit(1);
	}

	CPU_ZERO(&cpuset);
	CPU_SET(b_cpu, &cpuset);

	if (sched_setaffinity(0, sizeof (cpu_set_t), &cpuset) == -1) {
		perror("failed to bind process");
		exit(1);
	}

	if (clock_gettime(CLOCK_MONOTONIC, &b_ts) == -1) {
		perror("failed to query monotonic clock");
		exit(1);
	}

	a_ns = TIMESPEC_TO_NS(a_ts);
	b_ns = TIMESPEC_TO_NS(b_ts);

	/*
	 * If the current time is less than or equal to the previous
	 * time than monotonicity was not held.
	 */
	if (b_ns <= a_ns) {
		printf("ERROR: test_posix_monotonic() failed\n");
		printf("\ta_ns: %" PRIu64 "\n", a_ns);
		printf("\tb_ns: %" PRIu64 "\n", b_ns);
		exit(1);
	}

}

#endif

/*
 * Run short tests. Each short tests is called back-to-back in rapid
 * succession for the given number of iterations.
 */
void
run_short_tests(unsigned int iters)
{
	unsigned int	i;
	processorid_t	*cpus;
	size_t		cpus_size;
	struct timespec ts;

	for (i = 0; i < iters; i++) {
		/*
		 * Verify max of 100us delta between libfasttime and
		 * system. Even with a 100us leniency this test will
		 * occasionally fail because of gettimeofday()'s large
		 * latency variations. I've seen it take over 100us
		 * many times and even 1ms once.
		 */
		test_gettimeofday_delta(100);
	}

	for (i = 0; i < iters; i++) {
		test_posix_monotonic(NULL);
	}

	for (i = 0; i < iters; i++) {
		ts.tv_sec = 0;
		ts.tv_nsec = MS_TO_NS(0 * i);
		test_posix_monotonic(&ts);
	}

	get_cpus(&cpus, &cpus_size);

	for (i = 0; i < iters; i++) {
		test_posix_xcore(cpus, cpus_size);
	}
}

/*
 * Run the long tests which are the same as the short tests but run
 * for the period of time specified by mins.
 */
void
run_long_tests(unsigned int mins)
{
	unsigned int	i;
	unsigned int	secs = mins * 60;

	for (i = 0; i < secs; i++) {
		run_short_tests(1000);
		sleep(1);
	}
}

int
main(int argc, char **argv)
{
	int		c, i;
	unsigned int	seed = 0;
	unsigned int	mins = 0;
	struct timespec ts;

	while ((c = getopt(argc, argv, ":l:")) != -1) {
		switch (c) {
		case 'l':
			mins = atoi(optarg);
			break;
		case '?':
			fprintf(stderr, "Unknown option: %c\n", c);
			exit(1);
			break;
		case ':':
			fprintf(stderr, "Option %c missing argument\n", c);
			exit(1);
			break;
		}
	}

	/* XXX ability to pass seed as arg */
	clock_gettime(CLOCK_REALTIME, &ts);
	seed = (seed == 0) ? (unsigned int)ts.tv_nsec : seed;
	srand(seed);

	if (mins == 0) {
		for (i = 0; i < 5; i++) {
			run_short_tests(1000);
			sleep(1);
		}
	} else {
		run_long_tests(mins);
	}

	return (0);
}
