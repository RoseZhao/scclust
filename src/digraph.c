/* Copyright 2015 Fredrik Savje

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
==============================================================================*/


#include "../include/digraph.h"

#include <stdbool.h>
#include <stdlib.h>

#include "../include/config.h"


tbg_Digraph tbg_init_digraph(const tbg_Vid vertices, const tbg_Arcref max_arcs) {
	tbg_Digraph dg = {
		.vertices = vertices,
		.max_arcs = max_arcs,
		.head = NULL,
		.tail_ptr = malloc(sizeof(tbg_Arcref[vertices + 1])),
	};

	if (!dg.tail_ptr) return tbg_null_digraph();

	if (max_arcs > 0) {
		dg.head = malloc(sizeof(tbg_Vid[max_arcs]));
		if (!dg.head) tbg_free_digraph(&dg);
	}

	return dg;
}


void tbg_free_digraph(tbg_Digraph* const dg) {
	if (dg) {
		free(dg->head);
		free(dg->tail_ptr);
		*dg = tbg_null_digraph();
	}
}


bool tbg_change_arc_storage(tbg_Digraph* const dg, const tbg_Arcref new_max_arcs) {
	if (!dg || !dg->tail_ptr) return false;
	if (dg->max_arcs == new_max_arcs) return true;
	if (dg->tail_ptr[dg->vertices] > new_max_arcs) return false;

	if (new_max_arcs == 0) {
		free(dg->head);
		dg->head = NULL;
		dg->max_arcs = 0;
	} else {
		tbg_Vid* const tmp_ptr = realloc(dg->head, sizeof(tbg_Vid[new_max_arcs]));
		if (!tmp_ptr) return false;
		dg->head = tmp_ptr;
		dg->max_arcs = new_max_arcs;
	}

	return true;
}


tbg_Digraph tbg_empty_digraph(const tbg_Vid vertices, const tbg_Arcref max_arcs) {
	tbg_Digraph dg = tbg_init_digraph(vertices, max_arcs);
	if (!dg.tail_ptr) return dg;
	
	for (tbg_Vid v = 0; v <= vertices; ++v) dg.tail_ptr[v] = 0;

	return dg;
}


tbg_Digraph tbg_identity_digraph(const tbg_Vid vertices) {
	tbg_Digraph dg = tbg_init_digraph(vertices, vertices);
	if (!dg.tail_ptr) return dg;
	
	for (tbg_Vid v = 0; v < vertices; ++v) {
		dg.tail_ptr[v] = v;
		dg.head[v] = v;
	}
	dg.tail_ptr[vertices] = vertices;

	return dg;
}


tbg_Digraph tbg_balanced_digraph(const tbg_Vid vertices, const tbg_Vid arcs_per_vertex, tbg_Vid* const heads) {
	tbg_Digraph dg = tbg_init_digraph(vertices, 0);
	if (!dg.tail_ptr) return dg;

	dg.max_arcs = vertices * arcs_per_vertex;
	dg.head = heads;
	for (tbg_Vid v = 0; v <= vertices; ++v) {
		dg.tail_ptr[v] = v * arcs_per_vertex;
	}
	
	return dg;
}


tbg_Digraph tbg_copy_digraph(const tbg_Digraph* const dg) {
	if (!dg || !dg->tail_ptr) return tbg_null_digraph();
	if (dg->vertices == 0) return tbg_empty_digraph(0, 0);

	tbg_Digraph dg_out = tbg_init_digraph(dg->vertices, dg->tail_ptr[dg->vertices]);
	if (!dg_out.tail_ptr) return dg_out;

	for (tbg_Vid v = 0; v <= dg->vertices; ++v) dg_out.tail_ptr[v] = dg->tail_ptr[v];
	for (tbg_Arcref a = 0; a < dg->tail_ptr[dg->vertices]; ++a) dg_out.head[a] = dg->head[a];

	return dg_out;
}



static inline tbg_Arcref itbg_do_union(const tbg_Vid vertices,
									   const size_t num_dgs,
									   const tbg_Digraph* const * const dgs,
									   tbg_Vid* restrict const row_markers,
									   const bool write,
									   tbg_Arcref* restrict const out_tail_ptr,
									   tbg_Vid* restrict const out_head) {
	tbg_Arcref counter = 0;
	if (write) out_tail_ptr[0] = 0;
	for (tbg_Vid v = 0; v < vertices; ++v) row_markers[v] = TBG_VID_MAX;

	for (tbg_Vid v = 0; v < vertices; ++v) {
		for (size_t i = 0; i < num_dgs; ++i) {
			for (const tbg_Vid* arc_i = dgs[i]->head + dgs[i]->tail_ptr[v];
					arc_i != dgs[i]->head + dgs[i]->tail_ptr[v + 1];
					++arc_i) {
				if (row_markers[*arc_i] != v) {
					row_markers[*arc_i] = v;
					if (write) out_head[counter] = *arc_i;
					++counter;
				}
			}
		}

		if (write) out_tail_ptr[v + 1] = counter;
	}

	return counter;
}

tbg_Digraph tbg_digraph_union(const size_t num_dgs, const tbg_Digraph* const dgs[const static num_dgs]) {
	if (num_dgs == 0) return tbg_empty_digraph(0, 0);
	if (!dgs || !dgs[0]) return tbg_null_digraph();

	const tbg_Vid vertices = dgs[0]->vertices;

	tbg_Vid* const row_markers = malloc(sizeof(tbg_Vid[vertices]));
	if (!row_markers) return tbg_null_digraph();

	tbg_Arcref out_arcs_write = 0;

	// Try greedy memory count first
	for (size_t i = 0; i < num_dgs; ++i) {
		if (!dgs[i] || !dgs[i]->tail_ptr || dgs[i]->vertices != vertices) return tbg_null_digraph();
		out_arcs_write += dgs[i]->tail_ptr[vertices];
	}

	tbg_Digraph dg_out = tbg_init_digraph(vertices, out_arcs_write);
	if (!dg_out.tail_ptr) {
		// Could not allocate digraph with `out_arcs_write' arcs.
		// Do correct (but slow) memory count by doing
		// doing union without writing.
		out_arcs_write = itbg_do_union(vertices,
									   num_dgs, dgs,
									   row_markers,
									   false, NULL, NULL);

		// Try again. If fail, give up.
		dg_out = tbg_init_digraph(vertices, out_arcs_write);
		if (!dg_out.tail_ptr) {
			free(row_markers);
			return dg_out;
		}
	}

	out_arcs_write = itbg_do_union(vertices,
								   num_dgs, dgs,
								   row_markers,
								   true, dg_out.tail_ptr, dg_out.head);

	free(row_markers);

	tbg_change_arc_storage(&dg_out, out_arcs_write);

	return dg_out;
}


tbg_Digraph tbg_digraph_transpose(const tbg_Digraph* const dg) {
	if (!dg || !dg->tail_ptr) return tbg_null_digraph();
	if (dg->vertices == 0) return tbg_empty_digraph(0, 0);

	tbg_Vid* const row_count = calloc(dg->vertices + 1, sizeof(tbg_Arcref));
	if (!row_count) return tbg_null_digraph();

	tbg_Digraph dg_out = tbg_init_digraph(dg->vertices, dg->tail_ptr[dg->vertices]);
	if (!dg_out.tail_ptr) {
		free(row_count);
		return dg_out;
	}

	for (const tbg_Vid* arc = dg->head;
			arc != dg->head + dg->tail_ptr[dg->vertices];
			++arc) {
		++row_count[*arc + 1];
	}

	dg_out.tail_ptr[0] = 0;
	for (tbg_Vid v = 1; v <= dg->vertices; ++v) {
		row_count[v] += row_count[v - 1];
		dg_out.tail_ptr[v] = row_count[v];
	}

	for (tbg_Vid v = 0; v < dg->vertices; ++v) {
		for (const tbg_Vid* arc = dg->head + dg->tail_ptr[v];
				arc != dg->head + dg->tail_ptr[v + 1];
				++arc) {
			dg_out.head[row_count[*arc]] = v;
			++row_count[*arc];
		}
	}

	free(row_count);

	return dg_out;
}


static inline tbg_Arcref itbg_do_adjacency_product(const tbg_Vid vertices,
												   const tbg_Arcref* const dg_a_tail_ptr,
												   const tbg_Vid* const dg_a_head,
												   const tbg_Arcref* const dg_b_tail_ptr,
												   const tbg_Vid* const dg_b_head,
												   tbg_Vid* restrict const row_markers,
												   const bool force_diagonal,
												   const bool ignore_diagonal,
												   const bool write,
												   tbg_Arcref* restrict const out_tail_ptr,
												   tbg_Vid* restrict const out_head) {
	tbg_Arcref counter = 0;
	if (write) out_tail_ptr[0] = 0;
	for (tbg_Vid v = 0; v < vertices; ++v) row_markers[v] = TBG_VID_MAX;

	for (tbg_Vid v = 0; v < vertices; ++v) {
		if (force_diagonal) {
			for (const tbg_Vid* arc_b = dg_b_head + dg_b_tail_ptr[v];
					arc_b != dg_b_head + dg_b_tail_ptr[v + 1];
					++arc_b) {
				if (row_markers[*arc_b] != v) {
					row_markers[*arc_b] = v;
					if (write) out_head[counter] = *arc_b;
					++counter;
				}
			}
		}
		for (const tbg_Vid* arc_a = dg_a_head + dg_a_tail_ptr[v];
				arc_a != dg_a_head + dg_a_tail_ptr[v + 1];
				++arc_a) {
			if (*arc_a == v && (force_diagonal || ignore_diagonal)) continue;
			for (const tbg_Vid* arc_b = dg_b_head + dg_b_tail_ptr[*arc_a];
					arc_b != dg_b_head + dg_b_tail_ptr[*arc_a + 1];
					++arc_b) {
				if (row_markers[*arc_b] != v) {
					row_markers[*arc_b] = v;
					if (write) out_head[counter] = *arc_b;
					++counter;
				}
			}
		}

		if (write) out_tail_ptr[v + 1] = counter;
	}

	return counter;
}

tbg_Digraph tbg_adjacency_product(const tbg_Digraph* const dg_a, const tbg_Digraph* const dg_b, const bool force_diagonal, const bool ignore_diagonal) {
	if (force_diagonal && ignore_diagonal) return tbg_null_digraph();
	if (!dg_a || !dg_b || !dg_a->tail_ptr || !dg_b->tail_ptr) return tbg_null_digraph();
	if (dg_a->vertices != dg_b->vertices) return tbg_null_digraph();
	if (dg_a->vertices == 0) return tbg_empty_digraph(0, 0);

	const tbg_Vid vertices = dg_a->vertices;

	tbg_Vid* const row_markers = malloc(sizeof(tbg_Vid[vertices]));
	if (!row_markers) return tbg_null_digraph();

	tbg_Arcref out_arcs_write = 0;

	// Try greedy memory count first
	for (tbg_Vid v = 0; v < vertices; ++v) {
		if (force_diagonal) {
			out_arcs_write += dg_b->tail_ptr[v + 1] - dg_b->tail_ptr[v];
		}
		for (const tbg_Vid* arc_a = dg_a->head + dg_a->tail_ptr[v];
				arc_a != dg_a->head + dg_a->tail_ptr[v + 1];
				++arc_a) {
			if (*arc_a == v && (force_diagonal || ignore_diagonal)) continue;
			out_arcs_write += dg_b->tail_ptr[*arc_a + 1] - dg_b->tail_ptr[*arc_a];
		}
	}

	tbg_Digraph dg_out = tbg_init_digraph(vertices, out_arcs_write);
	if (!dg_out.tail_ptr) {
		// Could not allocate digraph with `out_arcs_write' arcs.
		// Do correct (but slow) memory count by doing
		// doing product without writing.
		out_arcs_write = itbg_do_adjacency_product(vertices,
												   dg_a->tail_ptr, dg_a->head,
												   dg_b->tail_ptr, dg_b->head,
												   row_markers,
												   force_diagonal, ignore_diagonal,
												   false, NULL, NULL);

		// Try again. If fail, give up.
		dg_out = tbg_init_digraph(vertices, out_arcs_write);
		if (!dg_out.tail_ptr) {
			free(row_markers);
			return dg_out;
		}
	}

	out_arcs_write = itbg_do_adjacency_product(vertices,
											   dg_a->tail_ptr, dg_a->head,
											   dg_b->tail_ptr, dg_b->head,
											   row_markers,
											   force_diagonal, ignore_diagonal,
											   true, dg_out.tail_ptr, dg_out.head);

	free(row_markers);

	tbg_change_arc_storage(&dg_out, out_arcs_write);

	return dg_out;
}