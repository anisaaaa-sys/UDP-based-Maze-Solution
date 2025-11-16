#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "maze.h"

// Helper functions
int is_valid(int x, int y, int n) {
    return x >= 0 && x < n && y >= 0 && y < n;
}

int dfs(struct Maze* maze, int x, int y, uint8_t* visited) {
    // Check for NULL pointers
    if (maze == NULL || visited == NULL) {
        fprintf(stderr, "Error: NULL pointer passed to dfs function.\n");
        return 0;
    }
    // Define the directions
    int n = maze->edgeLen;
    int endX = maze->endX;
    int endY = maze->endY;
    int idx = y * n + x;

    // Define the walls
    if (idx < 0 || idx >= n * n) {
        fprintf(stderr, "Error: Index out of bounds in dfs function.\n");
        return 0;
    }

    if (x == endX && y == endY) {
        maze->maze[idx] |= mark; // Mark the end point
        return 1;
    }

    // Mark the current cell as visited
    visited[idx] = 1;
    uint8_t cell = maze->maze[idx];

    // Check the right direction
    if ((cell & right) && is_valid(x + 1, y, n)) {
        int next = y * n + (x + 1);
        if (!visited[next] && (maze->maze[next] & left)) {
            if (dfs(maze, x + 1, y, visited)) {
                maze->maze[idx] |= mark; // Mark the path
                return 1;
            }
        }
    }

    // Check the left direction
    if ((cell & left) && is_valid(x - 1, y, n)) {
        int next = y * n + (x - 1);
        if (!visited[next] && (maze->maze[next] & right)) {
            if (dfs(maze, x - 1, y, visited)) {
                maze->maze[idx] |= mark; // Mark the path
                return 1;
            }
        }
    }

    // Check the down direction
    if ((cell & down) && is_valid(x, y + 1, n)) {
        int next = (y + 1) * n + x;
        if (!visited[next] && (maze->maze[next] & up)) {
            if (dfs(maze, x, y + 1, visited)) {
                maze->maze[idx] |= mark; // Mark the path
                return 1;
            }
        }
    }

    // Check the up direction
    if ((cell & up) && is_valid(x, y - 1, n)) {
        int next = (y - 1) * n + x;
        if (!visited[next] && (maze->maze[next] & down)) {
            if (dfs(maze, x, y - 1, visited)) {
                maze->maze[idx] |= mark; // Mark the path
                return 1;
            }
        }
    }

    return 0;
}

// Main function to solve the maze
void mazeSolve( struct Maze* maze ) {
    int n = maze->edgeLen;
    uint8_t* visited = calloc(n * n, sizeof(uint8_t));
    if (visited == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for visited array.\n");
        return;
    }

    int startX = maze->startX;
    int startY = maze->startY;

    // Check for valid start position
    if (!dfs(maze, startX, startY, visited)) {
        fprintf(stderr, "No path found from (%d, %d) to (%d, %d)\n", startX, startY, maze->endX, maze->endY);
    }
    
    if (visited != NULL) {
        free(visited);
    }
}

