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
#include <stdio.h>
#include <stdint.h>
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

#define	MS_TO_NS(ns)		(ns * 1000000)
#define	TIMESPEC_TO_NS(ts)	(((uint64_t)ts.tv_sec * NANOSEC) + ts.tv_nsec)

#ifdef __linux
#define NANOSEC 1000000000
typedef	int	processorid_t;
#endif

int get_cpus(processorid_t **cpus, int *size);

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
		printf("\ta_ns: %lu\n", a_ns);
		printf("\tb_ns: %lu\n", b_ns);
		exit(1);
	}
}

#ifdef __sun

void
test_posix_xcore(processorid_t cpus[], size_t num_cpus, unsigned int seed)
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
		printf("\ta_ns: %lu\n", a_ns);
		printf("\tb_ns: %lu\n", b_ns);
		exit(1);
	}

}

#elif __linux

void
test_posix_xcore(processorid_t cpus[], size_t num_cpus, unsigned int seed)
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
		printf("\ta_ns: %lu\n", a_ns);
		printf("\tb_ns: %lu\n", b_ns);
		exit(1);
	}

}

#endif

int
main(int argc, char **argv)
{
	int		i, num_cpus;
	processorid_t	*cpus;
	struct timespec ts;

	for (i = 0; i < 1000; i++) {
		test_posix_monotonic(NULL);
	}

	for (i = 0; i < 1000; i++) {
		ts.tv_sec = 0;
		ts.tv_nsec = MS_TO_NS(0 * i);
		test_posix_monotonic(&ts);
	}

	get_cpus(&cpus, &num_cpus);

	/* XXX ability to pass seed as arg */
	int seed = -1;
	clock_gettime(CLOCK_REALTIME, &ts);
	seed = (seed == -1) ? (unsigned int)ts.tv_nsec : seed;
	srand(seed);

	for (i = 0; i < 1000; i++) {
		test_posix_xcore(cpus, num_cpus, seed);
	}

	return (0);
}

#ifdef __sun

/*
 * Get the active CPUs, output via the cpus array. On input size
 * represents the size of cpus, on output it represents the number
 * of CPUs active. On success a 0 is returned.
 */
int
get_cpus(processorid_t **cpus, int *size)
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
get_cpus(processorid_t **cpus, int *size)
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
