/*
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id$
 *
 * Author: Michael S. Tsirkin <mst@mellanox.co.il>
 */

/* #define DEBUG 1 */
/* #define DEBUG_DATA 1 */
/* #define GET_CPU_MHZ_FROM_PROC 1 */

/* For gettimeofday */
#define _DEFAULT_SOURCE
#include <sys/time.h>

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "get_clock.h"

#ifndef DEBUG
#define DEBUG 0
#endif
#ifndef DEBUG_DATA
#define DEBUG_DATA 0
#endif

#define MEASUREMENTS 200
#define USECSTEP 10
#define USECSTART 100

/*
   Use linear regression to calculate cycles per microsecond.
http://en.wikipedia.org/wiki/Linear_regression#Parameter_estimation
*/
static double sample_get_cpu_mhz(void)
{
  return 2801.000000;
}

#if !defined(__s390x__) && !defined(__s390__)
static double proc_get_cpu_mhz(int no_cpu_freq_warn)
{
	FILE* f;
	char buf[256];
	double mhz = 0.0;
	int print_flag = 0;
	double delta;

	#if defined(__FreeBSD__)
	f = popen("/sbin/sysctl hw.clockrate","r");
	#else
	f = fopen("/proc/cpuinfo","r");
	#endif

	if (!f)
		return 0.0;
	while(fgets(buf, sizeof(buf), f)) {
		double m;
		int rc;

		#if defined (__ia64__)
		/* Use the ITC frequency on IA64 */
		rc = sscanf(buf, "itc MHz : %lf", &m);
		#elif defined (__PPC__) || defined (__PPC64__)
		/* PPC has a different format as well */
		rc = sscanf(buf, "clock : %lf", &m);
		#elif defined (__sparc__) && defined (__arch64__)
		/*
		 * on sparc the /proc/cpuinfo lines that hold
		 * the cpu freq in HZ are as follow:
		 * Cpu{cpu-num}ClkTck      : 00000000a9beeee4
		 */
		char *s;

		s = strstr(buf, "ClkTck\t: ");
		if (!s)
			continue;
		s += (strlen("ClkTck\t: ") - strlen("0x"));
		strncpy(s, "0x", strlen("0x"));
		rc = sscanf(s, "%lf", &m);
		m /= 1000000;
		#else
		#if defined (__FreeBSD__)
		rc = sscanf(buf, "hw.clockrate: %lf", &m);
		#else
		rc = sscanf(buf, "cpu MHz : %lf", &m);
		#endif
		#endif

		if (rc != 1)
			continue;

		if (mhz == 0.0) {
			mhz = m;
			continue;
		}
		delta = mhz > m ? mhz - m : m - mhz;
		if ((delta / mhz > 0.02) && (print_flag ==0)) {
			print_flag = 1;
			if (!no_cpu_freq_warn) {
				fprintf(stderr, "Conflicting CPU frequency values"
						" detected: %lf != %lf. CPU Frequency is not max.\n", mhz, m);
			}
			continue;
		}
	}

#if defined(__FreeBSD__)
	pclose(f);
#else
	fclose(f);
#endif
	return mhz;
}
#endif

double get_cpu_mhz(int no_cpu_freq_warn)
{
  return sample_get_cpu_mhz();
}

#if defined(__riscv)
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>

static long perf_event_open(struct perf_event_attr *hw_event,
		pid_t pid, int cpu, int group_fd,
		unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid,
			cpu, group_fd, flags);
}

cycles_t perf_get_cycles()
{
	cycles_t cycles = 0;
	struct perf_event_attr pe;
	const pid_t pid = 0;		// Current task
	const int cpu = -1;  		// On any CPU
	const int group_fd = -1;	// Use leader group
	const unsigned long flags = 0;
	/* Use this variable just to open perf event here and once.
	   It is appropriate because it touches only this function and
	   not fix other code */
	static int is_open = 0;
	/* Make file discriptor static just to keep it valid during
	   programm execution. It will be closed automatically when
	   test finishes. It is a hack just not to fix other part of test */
        static int fd = -1;

	if (!is_open) {
		memset(&pe, 0, sizeof(pe));

		pe.type = PERF_TYPE_HARDWARE;
		pe.size = sizeof(pe);
		pe.config = PERF_COUNT_HW_CPU_CYCLES;
		pe.disabled = 0;
		pe.exclude_kernel = 0;
		pe.exclude_hv = 0;

		fd = perf_event_open(&pe, pid, cpu, group_fd, flags);
		if (fd == -1) {
			fprintf(stderr, "Error opening perf event (%llx)\n", pe.config);
			exit(EXIT_FAILURE);
		}

		is_open = 1;
	}

	if(read(fd, &cycles, sizeof(cycles)) < 0) {
		fprintf(stderr, "Error reading perf event (%llx)\n", pe.config);
		exit(EXIT_FAILURE);
	}

	return cycles;
}
#endif
