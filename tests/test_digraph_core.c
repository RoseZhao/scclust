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


#include "test_suite.h"
#include "assert_digraph.h"

#include <stddef.h>

#include "../include/config.h"
#include "../include/digraph.h"
#include "../include/digraph_debug.h"


void scc_ut_init_digraph(void** state) {
	(void) state;

	scc_Digraph my_graph1 = scc_init_digraph(0, 0);
	assert_valid_digraph(&my_graph1, 0);
	assert_int_equal(my_graph1.max_arcs, 0);
	assert_free_digraph(&my_graph1);

	scc_Digraph my_graph2 = scc_init_digraph(10, 100);
	assert_valid_digraph(&my_graph2, 10);
	assert_int_equal(my_graph2.max_arcs, 100);
	assert_free_digraph(&my_graph2);
}

void scc_ut_free_digraph(void** state) {
	(void) state;

	scc_Digraph null_graph = scc_null_digraph();

	scc_Digraph my_graph1 = scc_empty_digraph(10, 10);
	scc_free_digraph(&my_graph1);
	assert_memory_equal(&my_graph1, &null_graph, sizeof(scc_Digraph));

	scc_Digraph my_graph2 = scc_empty_digraph(10, 10);
	free(my_graph2.head);
	my_graph2.head = NULL;
	scc_free_digraph(&my_graph2);
	assert_memory_equal(&my_graph2, &null_graph, sizeof(scc_Digraph));

	scc_Digraph my_graph3 = scc_null_digraph();
	scc_free_digraph(&my_graph3);
	assert_memory_equal(&my_graph3, &null_graph, sizeof(scc_Digraph));

	scc_free_digraph(NULL);
}

void scc_ut_change_arc_storage(void** state) {
	(void) state;

	scc_Digraph my_graph1 = scc_empty_digraph(10, 100);
	assert_true(scc_change_arc_storage(&my_graph1, 100));
	assert_empty_digraph(&my_graph1, 10);
	assert_int_equal(my_graph1.max_arcs, 100);
	assert_free_digraph(&my_graph1);

	scc_Digraph my_graph2 = scc_digraph_from_string("*.../.*../..*./...*/");
	assert_false(scc_change_arc_storage(&my_graph2, 2));
	assert_sound_digraph(&my_graph2, 4);
	assert_int_equal(my_graph2.max_arcs, 4);
	assert_free_digraph(&my_graph2);

	scc_Digraph my_graph3 = scc_empty_digraph(10, 100);
	assert_true(scc_change_arc_storage(&my_graph3, 50));
	assert_empty_digraph(&my_graph3, 10);
	assert_int_equal(my_graph3.max_arcs, 50);
	assert_free_digraph(&my_graph3);

	scc_Digraph my_graph4 = scc_empty_digraph(10, 100);
	assert_true(scc_change_arc_storage(&my_graph4, 200));
	assert_empty_digraph(&my_graph4, 10);
	assert_int_equal(my_graph4.max_arcs, 200);
	assert_free_digraph(&my_graph4);

	scc_Digraph my_graph5 = scc_empty_digraph(0, 100);
	assert_true(scc_change_arc_storage(&my_graph5, 0));
	assert_empty_digraph(&my_graph5, 0);
	assert_int_equal(my_graph5.max_arcs, 0);
	assert_free_digraph(&my_graph5);
}

void scc_ut_empty_digraph(void** state) {
	(void) state;

	scc_Digraph my_graph1 = scc_empty_digraph(0, 0);
	assert_empty_digraph(&my_graph1, 0);
	assert_int_equal(my_graph1.max_arcs, 0);
	assert_free_digraph(&my_graph1);

	scc_Digraph my_graph2 = scc_empty_digraph(10, 100);
	assert_empty_digraph(&my_graph2, 10);
	assert_int_equal(my_graph2.max_arcs, 100);
	assert_free_digraph(&my_graph2);
}

void scc_ut_balanced_digraph(void** state) {
	(void) state;

	scc_Vid* heads1 = NULL;
	scc_Digraph my_graph1 = scc_balanced_digraph(0, 0, heads1);
	assert_balanced_digraph(&my_graph1, 0, 0);
	assert_int_equal(my_graph1.max_arcs, 0);
	assert_free_digraph(&my_graph1);

	scc_Vid* heads2 = malloc(sizeof(scc_Vid[10 * 4]));
	for (size_t i = 0; i < 40; ++i) heads2[i] = i % 10;
	scc_Digraph my_graph2 = scc_balanced_digraph(10, 4, heads2);
	assert_balanced_digraph(&my_graph2, 10, 4);
	assert_int_equal(my_graph2.max_arcs, 40);
	for (size_t i = 0; i < 40; ++i) assert_int_equal(my_graph2.head[i], i % 10);
	assert_free_digraph(&my_graph2);
}

void scc_ut_copy_digraph(void** state) {
	(void) state;

	scc_Digraph dg1 = scc_digraph_from_string("****/..*./****/*.../");
	scc_Digraph dg2 = scc_empty_digraph(0, 0);
	scc_Digraph dg3 = scc_null_digraph();

	scc_Digraph res1 = scc_copy_digraph(&dg1);
	scc_Digraph res2 = scc_copy_digraph(&dg2);
	scc_Digraph res3 = scc_copy_digraph(&dg3);
	scc_Digraph res4 = scc_copy_digraph(NULL);

	assert_sound_digraph(&res1, 4);
	assert_sound_digraph(&res2, 0);

	assert_equal_digraph(&res1, &dg1);
	assert_equal_digraph(&res2, &dg2);
	assert_equal_digraph(&res3, &dg3);
	assert_equal_digraph(&res4, &dg3);

	assert_free_digraph(&dg1);
	assert_free_digraph(&dg2);
	assert_free_digraph(&res1);
	assert_free_digraph(&res2);
}


int main(void) {
	const struct CMUnitTest test_core[] = {
		cmocka_unit_test(scc_ut_init_digraph),
		cmocka_unit_test(scc_ut_free_digraph),
		cmocka_unit_test(scc_ut_change_arc_storage),
		cmocka_unit_test(scc_ut_empty_digraph),
		cmocka_unit_test(scc_ut_balanced_digraph),
		cmocka_unit_test(scc_ut_copy_digraph),
	};
	
	return cmocka_run_group_tests_name("core module", test_core, NULL, NULL);
}
