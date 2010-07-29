/** @file
 * uperf userspace interface.
 */
#include "uperf.h"
#include <stdlib.h>


#if !(defined(__i386) || defined(__ia64) || defined(__amd64) )
#error "Not a right architecture."
#endif

/// Global evil for UPERF_*_S single-thread macros.
struct uperf_s uperf_global;

/// Get raw timer data.
inline uint64_t
get_timer(struct uperf_s * uperf)
{
	return pcounter_get(&(uperf->cnt));
}

/// Get count since last perfpoint.
inline uint64_t
get_count(struct uperf_s * uperf)
{
	uint64_t timer = get_timer(uperf);
	uint64_t delta = timer - uperf->last_count; 
	uperf->last_count = timer;

	return delta;
}

/**
 * Init uperf structure.
 *
 * Prepare supplied struct uperf_s - init HW perf counter and alloc perfpoints
 * array. Note that (perfpoints_max + 1)^2 array of <tt>struct perfpoint_edge_s</tt>
 * (24 bytes at time of writing) will be allocated.
 *
 * @param[in,out] uperf uperf structure to init
 * @param[in] perfpoints_max max number of perfpoints
 * @param[in] counter_type what to count - <tt>`grep PERF_COUNT_HW /usr/include/linux/perf_event.h`</tt> for possible values. The two most popular ones:
 * @li PERF_COUNT_HW_CPU_CYCLES - count CPU cycles
 * @li PERF_COUNT_HW_INSTRUCTION - count instructions
 * @return 0 on success, see perf.c:pcounter_init() for error values
 */
int
uperf_init(struct uperf_s * uperf, int perfpoints_max, int counter_type)
{
	struct perfpoint_edge_s *edge;

	if( pcounter_init(&(uperf->cnt), counter_type) != 0 )
		return uperf->cnt.state;

	uperf->perfpoints_max = perfpoints_max + 1;
	uperf->edges_max = perfpoints_max * perfpoints_max;
	uperf->points = malloc(sizeof(struct perfpoint_edge_s) * uperf->edges_max);

	edge = uperf->points;
	for( int i = 0; i < PERFPOINT_EDGES_MAX; i++) {
		edge->user_count = 0;
		edge->user_sum = 0;
		edge->system_count = 0;
		edge->system_sum = 0;

		edge++;
	}

	pcounter_enable(&(uperf->cnt));

	// NOTE: 0 is used for entry point
	uperf->counter_type = counter_type;
	uperf->last_point = 0;
	uperf->last_count = get_timer(uperf);

	return uperf->cnt.state;
}

const char UPERF_DOT_HEAD[] = "digraph \"uperf\" {\n"
"	rankdir=UD\n"
"	size = \"6,6\"\n"
"	node [ fontname = \"Helvetica\" ];\n";
const char UPERF_DOT_TAIL[] = "}\n";
const char UPERF_PRINT_MALLOC_FAILED[] = "Memory allocation required for printing failed. What the...";

#define UPERF_DOT_MAX_PENWIDTH 20

/**
 * Print uperf statistics to specified stream.
 *
 * Text and dot (graphviz) formats are supported. If point_name_fnc function
 * is supplied, it will be used to obtain the names of perfpoints.
 *
 * @param[in] uperf uperf structure to print
 * @param[out] stream stream to print output to
 * @param[in] format print format, one of:
 * @li UPERF_PRINT_DEFAULT - text format
 * @li UPERF_PRINT_DOT - dot (graphviz) format (run through dot to obtain a graph)
 * @param point_name_fnc custom function that will return the name of perfpoint. Leave NULL to use perfpoint numbers.
 */
void
uperf_print(struct uperf_s * uperf, FILE *stream, int format, const char * (*point_name_fnc)(int))
{
	struct perfpoint_edge_s *edge;
	uint64_t uavg, savg;
	uint64_t avg_max = 0, avg_min = INT64_MAX;
	uint32_t cnt_max = 0;
	uint32_t peniswidth, color;

	if( format == UPERF_PRINT_DOT ) {
		fprintf(stream, UPERF_DOT_HEAD);

		// We need maxima to print something meaningful.
		edge = uperf->points;
		for( int i = 0; i < PERFPOINTS_MAX; i++) {
			for( int j = 0; j < PERFPOINTS_MAX; j++) {
				if( edge->user_count > 0 ) {
					uavg = edge->user_sum / edge->user_count;

					if( uavg > avg_max) 
						avg_max = uavg;

					if( uavg < avg_min) 
						avg_min = uavg;

					if( edge->user_count > cnt_max) 
						cnt_max = edge->user_count;
				}

				edge++;
			}
		}

		// with small cnt_max, line'd be ridiculously large for small counts
		if( cnt_max < UPERF_DOT_MAX_PENWIDTH )
			cnt_max = UPERF_DOT_MAX_PENWIDTH;

		avg_max += 1;
	}

	edge = uperf->points;

	for( int i = 0; i < PERFPOINTS_MAX; i++) {
		for( int j = 0; j < PERFPOINTS_MAX; j++) {

			if( edge->user_count > 0 ) {

				uavg = edge->user_sum / edge->user_count;

				if( edge->system_count > 0 )
					savg = edge->system_sum / edge->system_count;
				else
					savg = 0;

				if( format == UPERF_PRINT_DOT ) {
					// pen width depends on number of transitions
					peniswidth = (int)((float)(edge->user_count) / (float)(cnt_max) * UPERF_DOT_MAX_PENWIDTH) + 1;
					color = (int)(((float)(uavg - avg_min) / (float)(avg_max - avg_min)) * 254) + 1;
					if (point_name_fnc == NULL) {
						fprintf(stream, "	\"%d\" -> \"%d\"", j, i);
					}
					else {
						fprintf(stream, "	\"%s\" -> \"%s\"", point_name_fnc(j), point_name_fnc(i));
					}

					fprintf(stream, "[ label = \"%ld (x%d = %ld)\", penwidth = %d, color=\"#%02X0000\" ];\n",
							uavg, edge->user_count, edge->user_sum, peniswidth, color
							);
				}
				else {
					if (point_name_fnc == NULL) {
						fprintf(stream, "%3d => %3d: ", j, i); 
					}
					else {
						fprintf(stream, "%10s => %-10s: ", point_name_fnc(j), point_name_fnc(i)); 
					}
					fprintf(stream, "%8dx usr: %8ld avg, %12ld sum | %6dx sys: %10ld avg\n",
							edge->user_count, uavg, edge->user_sum, edge->system_count, savg);
				}
			}

			edge++;
		}
	}

	if( format == UPERF_PRINT_DOT ) {
		fprintf(stream, UPERF_DOT_TAIL);
	}
}

/**
 * Insert perfpoint.
 *
 * Each time this function is called, timer is read and elapsed time since last
 * perfpoint is written into the supplied uperf structure. Each perfpoint is
 * identified by an integer ranging from 1 to perfpoints_max (specified in
 * uperf_init). Index 0 is reserved for program entry (counter
 * initialization). Also this is C, so if you supply too big index, expect
 * usual SIGSEGV-and-friends fun.
 *
 * @param[in,out] uperf uperf structure to write info to.
 * @param[in] index perfpoint index <1; perfpoints_max>
 */
void
perfpoint(struct uperf_s * uperf, int index)
{
	struct perfpoint_edge_s *edge;
   
	edge = uperf->points + (index * PERFPOINTS_MAX) + uperf->last_point;

	uint64_t cnt = get_count(uperf);

	// TODO: insert awesome system time detection here
	edge->user_count += 1;
	edge->user_sum += cnt;

	uperf->last_point = index;
}

/**
 * Clean up uperf structure.
 * @param[in,out] uperf uperf structure to close
 */
void
uperf_close(struct uperf_s * uperf)
{
	free(uperf->points);
	pcounter_close(&(uperf->cnt));
}
