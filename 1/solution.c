#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include <time.h>
#include "Vector.h"

struct Files{
    char **fileNames;
    int count;
    int current;
} files;

double *total_time;
int global_counter = 0;
Vector *vectors;
int global_counter_vector = 0;

/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

/**
 * A function, called from inside of coroutines recursively. Just to demonstrate
 * the example. You can split your code into multiple functions, that usually
 * helps to keep the individual code blocks simple.
 */
static void
other_function(const char *name, int depth)
{
	struct coro *this = coro_this();
	printf("%s: entered function, depth = %d\n", name, depth);
	coro_yield();
	if (depth < 3)
		other_function(name, depth + 1);
}

int min(int a, int b) {
    return a < b ? a : b;
}

void merge(Vector *vector, int left, int mid, int right) {
    int it1 = 0;
    int it2 = 0;

    Vector result;
    init_vector(&result);
    result.capacity = vector->capacity;
    result.data = realloc(result.data, sizeof(int) * result.capacity);

    while (left + it1 < mid && mid + it2 < right) {
        if (vector->data[left + it1] < vector->data[mid + it2]) {
            result.data[it1 + it2] = vector->data[left + it1];
            it1 += 1;
        }
        else {
            result.data[it1 + it2] = vector->data[mid + it2];
            it2 += 1;
        }
    }

    while (left + it1 < mid) {
        result.data[it1 + it2] = vector->data[left + it1];
        it1 += 1;
    }

    while (mid + it2 < right) {
        result.data[it1 + it2] = vector->data[mid + it2];
        it2 += 1;
    }

    for (int i = 0; i < it1 + it2; ++i) {
        vector->data[left + i] = result.data[i];
    }
    freeVector(&result);
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_func_f(void *context)
{
    char *name = context;
    printf("Started coroutine %s\n", name);
    free(name);

    clock_t tic = clock();

    int my_counter = global_counter++;
    while(files.current < files.count) {
        char *fileName = files.fileNames[files.current];
        files.current++;

        FILE *myFile = fopen(fileName, "r");
        Vector vector;
        init_vector(&vector);

        int temp;
        while (fscanf(myFile, "%d", &temp) != EOF) {
            push_back(&vector, temp);
        }
        fclose(myFile);

        for (int i = 1; i < vector.size; i *= 2) {
            for (int j = 0; j < vector.size - i; j += 2 * i) {
                merge(&vector, j, j + i, min(j + 2 * i, vector.size));
            }
            clock_t toc = clock();
            total_time[my_counter] += (double)(toc - tic) / CLOCKS_PER_SEC;
            coro_yield();
            tic = clock();
        }

        init_vector(&vectors[global_counter_vector]);
        for (int i = 0; i < vector.size; ++i) {
            push_back(&vectors[global_counter_vector], vector.data[i]);
        }
        global_counter_vector++;
        freeVector(&vector);

    }
	/* This will be returned from coro_status(). */
	return 0;
}

int
main(int argc, char **argv)
{
    int count_coroutines = atoi(argv[1]);

    clock_t tic = clock();

    files.current = 0;
    files.count = argc - 2;
    files.fileNames = calloc(files.count, sizeof(char*));

    total_time = calloc(count_coroutines, sizeof(double));
    vectors = calloc(files.count, sizeof(Vector));

    int k = 0;
    for(int i = 2; i < argc; ++i) {
        files.fileNames[k] = argv[i];
        ++k;
    }

	coro_sched_init();
	for (int i = 0; i < count_coroutines; ++i) {
		char name[16];
		sprintf(name, "coro_%d", i);
		coro_new(coroutine_func_f, strdup(name));
	}
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		printf("Finished %d\n", coro_status(c));
		coro_delete(c);
	}

    Vector vector;
    init_vector(&vector);

    for(int i = 0; i < vectors[0].size; ++i) {
        push_back(&vector, vectors[0].data[i]);
    }
    freeVector(&vectors[0]);

    for(int i = 1; i < files.count; ++i) {
        int last_sz = vector.size;

        for(int j = 0; j < vectors[i].size; ++j) {
            push_back(&vector, vectors[i].data[j]);
        }
        freeVector(&vectors[i]);

        merge(&vector, 0, last_sz - 1, vector.size);
    }

    FILE *writeFile = fopen("result.txt", "w");
    for (int i = 0; i < vector.size; ++i) {
        fprintf(writeFile, "%d ", vector.data[i]);
    }

    fclose(writeFile);
    freeVector(&vector);
    free(vectors);
    free(files.fileNames);

    for(int i = 0; i < global_counter; ++i) {
        printf("Coroutine #%d time is %f seconds\n", global_counter - i - 1, total_time[i]);
    }

    free(total_time);
    clock_t toc = clock();
    printf("Elapsed: %f seconds\n", (double)(toc - tic) / CLOCKS_PER_SEC);

    return 0;
}
