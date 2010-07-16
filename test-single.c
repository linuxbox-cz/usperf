#include <stdio.h>
#include <linux/perf_event.h>
#include "uperf.h"

int
/*main(int argc, char *argv[])*/
main()
{
	// grep /usr/include/linux/perf_event.h for PERF_COUNT_HW for alternatives
	/*int c = uperf_init(&uperf, PERF_COUNT_HW_CPU_CYCLES);*/
	int c = UPERF_INIT_S(128, PERF_COUNT_HW_INSTRUCTIONS);

	if( c != 0 ) {
		printf("uperf init failed: %d\n", c);
		return c;
	}

	PERFPOINT_S(1);

	for( int i=0; i < 5; i++ ) {
		PERFPOINT_S(2);
	}

	PERFPOINT_S(3);
	PERFPOINT_S(10);

	/*save_uperf_log(&uperf, "test.log");*/
	UPERF_PRINT_S(stdout, UPERF_PRINT_DEFAULT, NULL);

#ifdef UPERF
	FILE *log = fopen("uperf.dot", "w");

	if (log != NULL) {
		UPERF_PRINT_S(log, UPERF_PRINT_DOT, NULL);

		fclose(log);
	}
	else {
		printf("Opening .dot file failed!");
	}
#endif

	UPERF_CLOSE_S;

	return 0;
}
