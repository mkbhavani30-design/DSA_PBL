#include <stdio.h>
#include <stdlib.h>
#include "campus.h"
#include "campus_router.h"
#include "segment_tree.h"

int main() {
    printf("Initializing Campus Navigation & Room Booking System...\n\n");

    // 1. Initialize Graph for sample nodes
    Graph* campus_graph = create_graph(50);
    
    // Add sample nodes (matching blueprint coordinates & floors)
    add_node(campus_graph, 101, 1, 5.0, 10.0, "F1_STAIR_01");
    add_node(campus_graph, 102, 1, 10.0, 10.0, "F1_HALL_01");
    add_node(campus_graph, 103, 1, 10.0, 5.0,  "Quiet Pod 101");
    
    add_node(campus_graph, 201, 2, 5.0, 10.0, "F2_STAIR_01");
    add_node(campus_graph, 202, 2, 10.0, 10.0, "F2_HALL_01");
    add_node(campus_graph, 203, 2, 10.0, 5.0,  "Group Room 201");

    // Add edges with weights
    add_edge(campus_graph, 101, 102, 4.0); // Stair to Hall F1
    add_edge(campus_graph, 102, 103, 2.0); // Hall to Room 101
    add_edge(campus_graph, 101, 201, 5.0); // Vertical transition (Stairs F1 to F2)
    add_edge(campus_graph, 201, 202, 4.0); // Stair to Hall F2
    add_edge(campus_graph, 202, 203, 2.0); // Hall to Room 201

    // 2. Initialize Segment Tree for 15 study rooms
    int total_study_rooms = 15;
    SegmentTree* room_st = create_segment_tree(total_study_rooms);
    printf("Segment Tree created for %d study rooms.\n", total_study_rooms);

    // 3. User workflow: Find first available consecutive study rooms (e.g., req length = 2, starting from index 0)
    int req_len = 2;
    int search_from = 0;
    int booked_index = st_find_first_free(room_st, req_len, search_from);

    if (booked_index != -1) {
        printf("Found available study room block starting at index: %d (Length: %d)\n", booked_index, req_len);
        
        // Book the range in the Segment Tree
        st_book(room_st, booked_index, booked_index + req_len - 1);
        printf("Successfully booked rooms [%d to %d] via Segment Tree.\n\n", booked_index, booked_index + req_len - 1);
    } else {
        printf("No available study rooms found matching criteria.\n\n");
    }

    // 4. Run A* Pathfinding from Entrance (Stairs F1: 101) to Study Room (203)
    int start_node = 101;
    int target_node = 203;
    int path_length = 0;
    
    printf("Computing A* path from node %d to node %d...\n", start_node, target_node);
    int* path = cg_astar(campus_graph, start_node, target_node, NULL, 0, &path_length);

    if (path && path_length > 0) {
        printf("Path found! Route sequence: ");
        for (int i = 0; i < path_length; i++) {
            printf("%d%s", path[i], (i == path_length - 1) ? "" : " -> ");
        }
        printf("\nTotal path nodes: %d\n", path_length);
        free(path);
    } else {
        printf("Pathfinding failed: Destination unreachable or blocked.\n");
    }

    // 5. Cleanup resources
    free_graph(campus_graph);
    free_segment_tree(room_st);
    
    printf("\nSystem execution completed cleanly.\n");
    return 0;
}