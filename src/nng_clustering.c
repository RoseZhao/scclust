/* ==============================================================================
 * scclust -- A C library for size constrained clustering
 * https://github.com/fsavje/scclust
 *
 * Copyright (C) 2015-2016  Fredrik Savje -- http://fredriksavje.com
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * ============================================================================== */


#include "../include/nng_clustering.h"

#include <stdbool.h>
#include <stdlib.h>
#include "../include/config.h"
#include "../include/digraph.h"
#include "findseeds.h"

// ==============================================================================
// Internal function prototypes
// ==============================================================================


static inline void iscc_fs_assign_neighbors(scc_Vid s,
                                            scc_Clabel new_label,
                                            const scc_Digraph* restrict nng,
                                            bool* restrict assigned,
                                            scc_Clabel* restrict cluster_label);

static inline void iscc_fs_assign_cl_labels(scc_Vid s,
                                            scc_Clabel new_label,
                                            const scc_Digraph* nng,
                                            scc_Clabel* cluster_label);


// ==============================================================================
// External function implementations
// ==============================================================================


void scc_free_SeedClustering(scc_SeedClustering* const cl) {
	if (cl) {
		free(cl->assigned);
		free(cl->seeds);
		free(cl->cluster_label);
		*cl = SCC_NULL_SEED_CLUSTERING;
	}
}


scc_SeedClustering scc_get_seed_clustering(const scc_Digraph* const nng, const scc_SeedMethod sm, scc_Vid seed_init_capacity) {

	if (!nng || !nng->tail_ptr) return SCC_NULL_SEED_CLUSTERING;

	iscc_SeedArray seed_array = ISCC_NULL_SEED_ARRAY;

	switch(sm) {

		case SCC_LEXICAL:
			seed_array = iscc_findseeds_lexical(nng, seed_init_capacity);
			break;

		case SCC_INWARDS_ORDER:
			seed_array = iscc_findseeds_inwards(nng, seed_init_capacity, false);
			break;

		case SCC_INWARDS_UPDATING:
			seed_array = iscc_findseeds_inwards(nng, seed_init_capacity, true);
			break;

		case SCC_EXCLUSION_ORDER:
			seed_array = iscc_findseeds_exclusion(nng, seed_init_capacity, false);
			break;

		case SCC_EXCLUSION_UPDATING:
			seed_array = iscc_findseeds_exclusion(nng, seed_init_capacity, true);
			break;

		default:
			break;
	}

	if (!seed_array.seeds) {
		return SCC_NULL_SEED_CLUSTERING;
	}

	scc_SeedClustering clustering = SCC_NULL_SEED_CLUSTERING;

	return clustering;
}


bool scc_assign_remaining_lexical(scc_SeedClustering* const clustering, const scc_Digraph* const priority_graph) {

	const scc_Vid vertices = clustering->vertices;
	bool* const assigned = clustering->assigned;
	scc_Clabel* const cluster_label = clustering->cluster_label;

	for (scc_Vid v = 0; v < vertices; ++v) {
		if (!assigned[v]) {
			cluster_label[v] = SCC_CLABEL_MAX;

			const scc_Vid* const v_arc_stop = priority_graph->head + priority_graph->tail_ptr[v + 1];
			for (const scc_Vid* v_arc = priority_graph->head + priority_graph->tail_ptr[v];
			        v_arc != v_arc_stop; ++v_arc) {
				if (assigned[*v_arc]) {
					cluster_label[v] = cluster_label[*v_arc];
					break;
				}
			}
		}
	}

	return true;
}


bool scc_assign_remaining_keep_even(scc_SeedClustering* const clustering, const scc_Digraph* const priority_graph, const scc_Vid desired_size) {

	scc_Vid* cluster_size = calloc(clustering->num_clusters, sizeof(scc_Vid));
	if (!cluster_size) return false;

	const scc_Vid vertices = clustering->vertices;
	bool* const assigned = clustering->assigned;
	scc_Clabel* const cluster_label = clustering->cluster_label;

	for (scc_Vid v = 0; v < vertices; ++v) {
		if (!assigned[v]) {
			scc_Clabel best_cluster = SCC_CLABEL_MAX;
			scc_Vid best_size = 0;

			const scc_Vid* const v_arc_stop = priority_graph->head + priority_graph->tail_ptr[v + 1];
			for (const scc_Vid* v_arc = priority_graph->head + priority_graph->tail_ptr[v];
			        v_arc != v_arc_stop; ++v_arc) {
				if (assigned[*v_arc]) {
					if ((best_cluster == SCC_CLABEL_MAX) || (best_size < cluster_size[cluster_label[*v_arc]])) {
						best_cluster = cluster_label[*v_arc];
						best_size = cluster_size[best_cluster];
					}
				}
			}

			cluster_label[v] = best_cluster;
			if (best_cluster != SCC_CLABEL_MAX) {
				++cluster_size[best_cluster];
				if (cluster_size[best_cluster] % desired_size == 0) cluster_size[best_cluster] = 0;
			}
		}
	}

	free(cluster_size);

	return true;
}

// ==============================================================================
// Internal function implementations 
// ==============================================================================

static inline void iscc_fs_assign_neighbors(const scc_Vid s,
                                            const scc_Clabel new_label,
                                            const scc_Digraph* restrict const nng,
                                            bool* restrict const assigned,
                                            scc_Clabel* restrict const cluster_label) {
	assert(!assigned[s]);
	assigned[s] = true;
	cluster_label[s] = new_label;

	const scc_Vid* const s_arc_stop = nng->head + nng->tail_ptr[s + 1];
	for (const scc_Vid* s_arc = nng->head + nng->tail_ptr[s];
	        s_arc != s_arc_stop; ++s_arc) {
		assert(!assigned[*s_arc]);
		assigned[*s_arc] = true;
		cluster_label[*s_arc] = new_label;
	}
}


static inline void iscc_fs_assign_cl_labels(const scc_Vid s,
                                            const scc_Clabel new_label,
                                            const scc_Digraph* const nng,
                                            scc_Clabel* const cluster_label) {
	cluster_label[s] = new_label;

	const scc_Vid* const s_arc_stop = nng->head + nng->tail_ptr[s + 1];
	for (const scc_Vid* s_arc = nng->head + nng->tail_ptr[s];
	        s_arc != s_arc_stop; ++s_arc) {
		cluster_label[*s_arc] = new_label;
	}
}

