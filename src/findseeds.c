/* scclust -- A C library for size constrained clustering
 * https://github.com/fsavje/scclust
 *
 * Copyright (C) 2015  Fredrik Savje -- http://fredriksavje.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
==============================================================================*/


#include "findseeds.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include "../include/config.h"
#include "../include/digraph.h"
#include "../include/nng_clustering.h"

#ifdef SCC_STABLE_CLUSTERING
 	#include "findseeds_debug.h"
#endif

// ==============================================================================
// Internal structs
// ==============================================================================


typedef struct iscc_fs_SortResult iscc_fs_SortResult;
struct iscc_fs_SortResult {
	scc_Vid* inwards_count;
	scc_Vid* sorted_vertices;
	scc_Vid** vertex_index;
	scc_Vid** bucket_index;
};


// ==============================================================================
// Internal function prototypes
// ==============================================================================


static inline bool iscc_fs_check_candidate_vertex(scc_Vid cv, const scc_Digraph* nng, const bool* assigned);

static inline bool iscc_fs_add_seed(scc_Vid s, scc_Clustering* cl);

static inline void iscc_fs_assign_neighbors(scc_Vid s,
                                            scc_Clulab new_label,
                                            const scc_Digraph* restrict nng,
                                            bool* restrict assigned,
                                            scc_Clulab* restrict cluster_label);

static inline void iscc_fs_assign_cl_labels(scc_Vid s,
                                            scc_Clulab new_label,
                                            const scc_Digraph* nng,
                                            scc_Clulab* cluster_label);

static void iscc_fs_shrink_seeds_array(scc_Clustering* cl);

static scc_Digraph iscc_exclusion_graph(const scc_Digraph* nng);

static iscc_fs_SortResult iscc_fs_sort_by_inwards(const scc_Digraph* nng, bool make_indices);

static void iscc_fs_free_SortResult(iscc_fs_SortResult* sr);

static inline void iscc_fs_decrease_v_in_sort(scc_Vid v_to_decrease,
                                              scc_Vid* restrict inwards_count,
                                              scc_Vid** restrict vertex_index,
                                              scc_Vid** restrict bucket_index,
                                              scc_Vid* current_pos);


// ==============================================================================
// External function implementations
// ==============================================================================


scc_Clustering iscc_findseeds_lexical(const scc_Digraph* const nng, const scc_Vid seed_init_capacity) {
	if (!nng || !nng->tail_ptr) return scc_null_clustering();

	scc_Clustering cl = {
		.vertices = nng->vertices,
		.seed_capacity = seed_init_capacity,
		.num_clusters = 0,
		.assigned = calloc(nng->vertices, sizeof(bool)),
		.seeds = malloc(sizeof(scc_Vid[seed_init_capacity])),
		.cluster_label = malloc(sizeof(scc_Clulab[nng->vertices])),
	};

	if (!cl.assigned || !cl.seeds || !cl.cluster_label) {
		scc_free_Clustering(&cl);
		return cl;
	}

	for (scc_Vid cv = 0; cv < nng->vertices; ++cv) {
		if (iscc_fs_check_candidate_vertex(cv, nng, cl.assigned)) {
			iscc_fs_assign_neighbors(cv, cl.num_clusters, nng, cl.assigned, cl.cluster_label);
			if (!iscc_fs_add_seed(cv, &cl)) {
				scc_free_Clustering(&cl);
				return cl;
			}
		}
	}

	iscc_fs_shrink_seeds_array(&cl);

	return cl;
}

scc_Clustering iscc_findseeds_inwards(const scc_Digraph* const nng, const scc_Vid seed_init_capacity, const bool updating) {
	if (!nng || !nng->tail_ptr) return scc_null_clustering();

	iscc_fs_SortResult sort = iscc_fs_sort_by_inwards(nng, updating);

	scc_Clustering cl = {
		.vertices = nng->vertices,
		.seed_capacity = seed_init_capacity,
		.num_clusters = 0,
		.assigned = calloc(nng->vertices, sizeof(bool)),
		.seeds = malloc(sizeof(scc_Vid[seed_init_capacity])),
		.cluster_label = NULL,
	};

	if (!cl.assigned || !cl.seeds || !sort.sorted_vertices) {
		iscc_fs_free_SortResult(&sort);
		scc_free_Clustering(&cl);
		return cl;
	}

	const scc_Vid* const sorted_v_stop = sort.sorted_vertices + nng->vertices;
	for (scc_Vid* sorted_v = sort.sorted_vertices;
	        sorted_v != sorted_v_stop; ++sorted_v) {

		#ifdef SCC_STABLE_CLUSTERING
			iscc_fs_debug_check_sort(sorted_v, sorted_v_stop - 1, sort.inwards_count);
		#endif

		if (iscc_fs_check_candidate_vertex(*sorted_v, nng, cl.assigned)) {
			assert(!cl.assigned[*sorted_v]);
			cl.assigned[*sorted_v] = true;
			if (!iscc_fs_add_seed(*sorted_v, &cl)) {
				iscc_fs_free_SortResult(&sort);
				scc_free_Clustering(&cl);
				return cl;
			}

			const scc_Vid* const v_arc_stop = nng->head + nng->tail_ptr[*sorted_v + 1];
			for (const scc_Vid* v_arc = nng->head + nng->tail_ptr[*sorted_v];
			        v_arc != v_arc_stop; ++v_arc) {
				assert(!cl.assigned[*v_arc]);
				cl.assigned[*v_arc] = true;
			}

			if (updating) {
				for (const scc_Vid* v_arc = nng->head + nng->tail_ptr[*sorted_v];
				        v_arc != v_arc_stop; ++v_arc) {
					const scc_Vid* const v_arc_arc_stop = nng->head + nng->tail_ptr[*v_arc + 1];
					for (scc_Vid* v_arc_arc = nng->head + nng->tail_ptr[*v_arc];
					        v_arc_arc != v_arc_arc_stop; ++v_arc_arc) {
						// Only decrease if vertex can be seed (i.e., not already assigned and not already considered)
						if (!cl.assigned[*v_arc_arc] && sorted_v < sort.vertex_index[*v_arc_arc]) {
							iscc_fs_decrease_v_in_sort(*v_arc_arc, sort.inwards_count, sort.vertex_index, sort.bucket_index, sorted_v);
						}
					}
				}
			}
		}
	}

	iscc_fs_free_SortResult(&sort);

	iscc_fs_shrink_seeds_array(&cl);

	cl.cluster_label = malloc(sizeof(scc_Clulab[nng->vertices]));
	if (!cl.cluster_label) {
		scc_free_Clustering(&cl);
		return cl;
	}

	for (scc_Vid icl = 0; icl < cl.num_clusters; ++icl) {
		iscc_fs_assign_cl_labels(cl.seeds[icl], icl, nng, cl.cluster_label);
	}

	return cl;
}

scc_Clustering iscc_findseeds_exclusion(const scc_Digraph* const nng, const scc_Vid seed_init_capacity, const bool updating) {
	if (!nng || !nng->tail_ptr) return scc_null_clustering();

	scc_Digraph exclusion_graph = iscc_exclusion_graph(nng);

	bool* const excluded = calloc(nng->vertices, sizeof(bool));

	if (!exclusion_graph.tail_ptr || !excluded) {
		scc_free_digraph(&exclusion_graph);
		free(excluded);
		return scc_null_clustering();
	}

	// Remove edges to vertices that cannot be seeds
	for (scc_Vid v = 0; v < vertices; ++v) {
		if (nng->tail_ptr[v] == nng->tail_ptr[v + 1]) {
			excluded[v] = true;
			const scc_Vid* const ex_arc_stop = exclusion_graph.head + exclusion_graph.tail_ptr[v + 1];
			for (const scc_Vid* ex_arc = exclusion_graph.head + exclusion_graph.tail_ptr[v];
			        ex_arc != ex_arc_stop; ++ex_arc) {
				*ex_arc = SCC_VID_MAX;
			}
		}
	}

	iscc_fs_SortResult sort = iscc_fs_sort_by_inwards(&exclusion_graph, updating);

	scc_Clustering cl = {
		.vertices = nng->vertices,
		.seed_capacity = seed_init_capacity,
		.num_clusters = 0,
		.assigned = NULL,
		.seeds = malloc(sizeof(scc_Vid[seed_init_capacity])),
		.cluster_label = NULL,
	};

	if (!cl.seeds || !sort.sorted_vertices) {
		scc_free_digraph(&exclusion_graph);
		free(excluded);
		iscc_fs_free_SortResult(&sort);
		scc_free_Clustering(&cl);
		return cl;
	}

	const scc_Vid* const sorted_v_stop = sort.sorted_vertices + nng->vertices;
	for (scc_Vid* sorted_v = sort.sorted_vertices;
	        sorted_v != sorted_v_stop; ++sorted_v) {

		#ifdef SCC_STABLE_CLUSTERING
			iscc_fs_debug_check_sort(sorted_v, sorted_v_stop - 1, sort.inwards_count);
		#endif

		if (!excluded[*sorted_v]) {
			excluded[*sorted_v] = true;
			if (!iscc_fs_add_seed(*sorted_v, &cl)) {
				scc_free_digraph(&exclusion_graph);
				free(excluded);
				iscc_fs_free_SortResult(&sort);
				scc_free_Clustering(&cl);
				return cl;
			}

			const scc_Vid* const ex_arc_stop = exclusion_graph.head + exclusion_graph.tail_ptr[*sorted_v + 1];
			for (const scc_Vid* ex_arc = exclusion_graph.head + exclusion_graph.tail_ptr[*sorted_v];
			        ex_arc != ex_arc_stop; ++ex_arc) {
				assert(*ex_arc != SCC_VID_MAX);
				if (!excluded[*ex_arc]) {
					excluded[*ex_arc] = true;

					if (updating) {
						const scc_Vid* const ex_arc_arc_stop = exclusion_graph.head + exclusion_graph.tail_ptr[*ex_arc + 1];
						for (scc_Vid* ex_arc_arc = exclusion_graph.head + exclusion_graph.tail_ptr[*ex_arc];
						        ex_arc_arc != ex_arc_arc_stop; ++ex_arc_arc) {
							assert(*ex_arc_arc != SCC_VID_MAX);
							if (!excluded[*ex_arc_arc]) {
								iscc_fs_decrease_v_in_sort(*ex_arc_arc, sort.inwards_count, sort.vertex_index, sort.bucket_index, sorted_v);
							}
						}
					}
				}
			}
		}
	}

	scc_free_digraph(&exclusion_graph);
	iscc_fs_free_SortResult(&sort);
	free(excluded);

	iscc_fs_shrink_seeds_array(&cl);

	cl.assigned = calloc(nng->vertices, sizeof(bool));
	cl.cluster_label = malloc(sizeof(scc_Clulab[nng->vertices]));
	if (!cl.assigned || !cl.cluster_label) {
		scc_free_Clustering(&cl);
		return cl;
	}

	for (scc_Vid icl = 0; icl < cl.num_clusters; ++icl) {
		iscc_fs_assign_neighbors(cl.seeds[icl], icl, nng, cl.assigned, cl.cluster_label);
	}

	return cl;
}


/*

Exclusion graph does not give one arc optimality

     *            *
     |            |
     v            v
  *->*->*->*<->*<-*<-*<-*
     ^            ^
     |            |
     *            *

bool iscc_findseeds_onearc_updating(const scc_Digraph* const nng, ...) {
	//Among those with 0 inwards arcs, sort on exclusion graph 
}
*/


// ==============================================================================
// Internal function implementations 
// ==============================================================================

static inline bool iscc_fs_check_candidate_vertex(const scc_Vid cv, const scc_Digraph* const nng, const bool* const assigned) {
	if (assigned[cv]) return false;

	const scc_Vid* cv_arc = nng->head + nng->tail_ptr[cv];
	const scc_Vid* const cv_arc_stop = nng->head + nng->tail_ptr[cv + 1];
	if (cv_arc == cv_arc_stop) return false;

	for (; cv_arc != cv_arc_stop; ++cv_arc) { 
		if (assigned[*cv_arc]) return false;
	}

	return true;
}

static inline bool iscc_fs_add_seed(const scc_Vid s, scc_Clustering* const cl) {
	assert(cl->num_clusters <= cl->seed_capacity);
	if (cl->num_clusters == cl->seed_capacity) {
		cl->seed_capacity *= 2;
		if (cl->seed_capacity > cl->vertices) cl->seed_capacity = cl->vertices;
		scc_Vid* const tmp_ptr = realloc(cl->seeds, sizeof(scc_Vid[cl->seed_capacity]));
		if (!tmp_ptr) return false;
		cl->seeds = tmp_ptr;
	}
	cl->seeds[cl->num_clusters] = s;
	++(cl->num_clusters);
	return true;
}

static inline void iscc_fs_assign_neighbors(const scc_Vid s,
                                            const scc_Clulab new_label,
                                            const scc_Digraph* restrict const nng,
                                            bool* restrict const assigned,
                                            scc_Clulab* restrict const cluster_label) {
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
                                            const scc_Clulab new_label,
                                            const scc_Digraph* const nng,
                                            scc_Clulab* const cluster_label) {
	cluster_label[s] = new_label;

	const scc_Vid* const s_arc_stop = nng->head + nng->tail_ptr[s + 1];
	for (const scc_Vid* s_arc = nng->head + nng->tail_ptr[s];
	        s_arc != s_arc_stop; ++s_arc) {
		cluster_label[*s_arc] = new_label;
	}
}

static void iscc_fs_shrink_seeds_array(scc_Clustering* const cl) {
	if (cl && cl->seeds && (cl->seed_capacity > cl->num_clusters)) {
		scc_Vid* const tmp_ptr = realloc(cl->seeds, sizeof(scc_Vid[cl->num_clusters]));
		if (tmp_ptr) {
			cl->seeds = tmp_ptr;
			cl->seed_capacity = cl->num_clusters;
		}
	}
}


static scc_Digraph iscc_exclusion_graph(const scc_Digraph* const nng) {
	if (!nng || !nng->tail_ptr) return scc_null_digraph();

	scc_Digraph nng_transpose = scc_digraph_transpose(nng);
	if (!nng_transpose.tail_ptr) return scc_null_digraph();

	scc_Digraph nng_nng_transpose = scc_adjacency_product(nng, &nng_transpose, true, false);
	scc_free_digraph(&nng_transpose);
	if (!nng_nng_transpose.tail_ptr) return scc_null_digraph();

	const scc_Digraph* nng_sum[2] = {nng, &nng_nng_transpose};
	scc_Digraph exclusion_graph = scc_digraph_union(2, nng_sum);
	scc_free_digraph(&nng_nng_transpose);
	if (!exclusion_graph.tail_ptr) return scc_null_digraph();

	return exclusion_graph;
}

static iscc_fs_SortResult iscc_fs_sort_by_inwards(const scc_Digraph* const nng, const bool make_indices) {

	const scc_Vid vertices = nng->vertices;

	iscc_fs_SortResult res = {
		.inwards_count = calloc(vertices, sizeof(scc_Vid)),
		.sorted_vertices = malloc(sizeof(scc_Vid[vertices])),
		.vertex_index = NULL,
		.bucket_index = NULL,
	};

	if (!res.inwards_count || !res.sorted_vertices) {
		iscc_fs_free_SortResult(&res);
		return res;
	}

	if (make_indices) {
		res.vertex_index = malloc(sizeof(scc_Vid*[vertices]));
		if (!res.vertex_index) {
			iscc_fs_free_SortResult(&res);
			return res;
		}
	}

	const scc_Vid* const arc_stop = nng->head + nng->tail_ptr[vertices];
	for (const scc_Vid* arc = nng->head; arc != arc_stop; ++arc) {
		if (*arc != SCC_VID_MAX) ++res.inwards_count[*arc];
	}

	// Dynamic alloc is slightly faster but more error-prone
	// Add if turns out to be bottleneck
	scc_Vid max_inwards = 0;
	for (scc_Vid v = 0; v < vertices; ++v) {
		if (max_inwards < res.inwards_count[v]) max_inwards = res.inwards_count[v];
	}

	scc_Vid* bucket_count = calloc(max_inwards + 1, sizeof(scc_Vid));
	res.bucket_index = malloc(sizeof(scc_Vid*[max_inwards + 1]));
	if (!bucket_count || !res.bucket_index) {
		free(bucket_count);
		iscc_fs_free_SortResult(&res);
		return res;
	}

	for (scc_Vid v = 0; v < vertices; ++v) {
		++bucket_count[res.inwards_count[v]];
	}

	scc_Vid bucket_cumsum = 0;
	for (scc_Vid b = 0; b <= max_inwards; ++b) {
		bucket_cumsum += bucket_count[b];
		res.bucket_index[b] = res.sorted_vertices + bucket_cumsum;
	}
	free(bucket_count);

	if (make_indices) {
		for (scc_Vid v = vertices; v > 0; ) {
			--v;
			--res.bucket_index[res.inwards_count[v]];
			*res.bucket_index[res.inwards_count[v]] = v;
			res.vertex_index[v] = res.bucket_index[res.inwards_count[v]];
		}
	} else {
		for (scc_Vid v = vertices; v > 0; ) {
			--v;
			--(res.bucket_index[res.inwards_count[v]]);
			*res.bucket_index[res.inwards_count[v]] = v;
		}

		free(res.inwards_count);
		free(res.bucket_index);
		res.inwards_count = NULL;
		res.bucket_index = NULL;
	}

	return res;
}

static void iscc_fs_free_SortResult(iscc_fs_SortResult* const sr) {
	if (sr) {
		free(sr->inwards_count);
		free(sr->sorted_vertices);
		free(sr->vertex_index);
		free(sr->bucket_index);
		*sr = (iscc_fs_SortResult) { NULL, NULL, NULL, NULL };
	}
}

static inline void iscc_fs_decrease_v_in_sort(const scc_Vid v_to_decrease,
                                              scc_Vid* restrict const inwards_count,
                                              scc_Vid** restrict const vertex_index,
                                              scc_Vid** restrict const bucket_index,
                                              scc_Vid* const current_pos) {
	// Assert that vertex index is correct
	assert(v_to_decrease == *vertex_index[v_to_decrease]);

	// Find vertices to move
	scc_Vid* const move_from = vertex_index[v_to_decrease];
	scc_Vid* move_to = bucket_index[inwards_count[v_to_decrease]];
	if (move_to <= current_pos) {
		move_to = current_pos + 1;
		bucket_index[inwards_count[v_to_decrease] - 1] = move_to;
	} 

	// Assert that swap vertices have the same count
	assert(inwards_count[*move_from] == inwards_count[*move_to]);

	// Update bucket index
	bucket_index[inwards_count[v_to_decrease]] = move_to + 1;

	// Decrease count on vertex
	--inwards_count[v_to_decrease];

	// Check so list not already sorted
	if (move_from != move_to) {
		// Do swap
		*move_from = *move_to;
		*move_to = v_to_decrease;

		// Update vertex index
		vertex_index[*move_to] = move_to;
		vertex_index[*move_from] = move_from;

		#ifdef SCC_STABLE_CLUSTERING
			iscc_fs_debug_bucket_sort(move_to + 1, move_from, inwards_count, vertex_index);
		#endif
	}

	#ifdef SCC_STABLE_CLUSTERING
		if (bucket_index[inwards_count[v_to_decrease]] <= current_pos) {
			bucket_index[inwards_count[v_to_decrease]] = current_pos + 1;
		}
		iscc_fs_debug_bucket_sort(bucket_index[inwards_count[v_to_decrease]],
		                          move_to, inwards_count, vertex_index);
	#endif
}
