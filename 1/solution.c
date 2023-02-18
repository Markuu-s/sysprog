#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include <time.h>

struct Files{
    char **fileNames;
    int count;
    int current;
} files;

typedef struct
{
    int *data;
    int size;
    int capacity;
} Vector;

double *total_time;

void init_vector(Vector *v)
{
    v->capacity = 8;
    v->size = 0;
    v->data = malloc(v->capacity * sizeof(int));
}

/**
 * @brief Add to end to vector of data
 *
 * @param v is pointer to Vector
 * @param data element to add in back of vector
 */
void push_back(Vector *v, int data)
{
    if (v->capacity == v->size)
    {
        v->capacity *= 2;
        v->data = realloc(v->data, sizeof(int) * v->capacity);
    }
    v->data[v->size++] = data;
}


/**
 * @brief Free memory of struct Vector
 *
 * @param v is pointer to Vector
 */
void freeVector(Vector *v)
{
    free(v->data);
}

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

void merge(Vector vector, int left, int mid, int right) {
    int it1 = 0;
    int it2 = 0;

    Vector result;
    init_vector(&result);
    result.capacity = vector.capacity;
    result.data = realloc(result.data, sizeof(int) * result.capacity);

    while (left + it1 < mid && mid + it2 < right) {
        if (vector.data[left + it1] < vector.data[mid + it2]) {
            result.data[it1 + it2] = vector.data[left + it1];
            it1 += 1;
        }
        else {
            result.data[it1 + it2] = vector.data[mid + it2];
            it2 += 1;
        }
    }

    while (left + it1 < mid) {
        result.data[it1 + it2] = vector.data[left + it1];
        it1 += 1;
    }

    while (mid + it2 < right) {
        result.data[it1 + it2] = vector.data[mid + it2];
        it2 += 1;
    }

    for (int i = 0; i < it1 + it2; ++i) {
        vector.data[left + i] = result.data[i];
    }
    freeVector(&result);
}

int global_counter = 0;
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
                merge(vector, j, j + i, min(j + 2 * i, vector.size));
            }
            coro_yield();
        }

        char tempFileName[256];
        sprintf(tempFileName, "%d", my_counter);
        strcat(tempFileName, "_temp.txt");
        FILE *writeFile = fopen(tempFileName, "w");
        for (int i = 0; i < vector.size; ++i) {
            fprintf(writeFile, "%d ", vector.data[i]);
        }
        fclose(writeFile);
        freeVector(&vector);

    }
	/* This will be returned from coro_status(). */
	return 0;
}

int
main(int argc, char **argv)
{
    clock_t tic = clock();

    files.current = 0;
    files.count = argc - 1;
    files.fileNames = calloc(argc - 1, sizeof(char*));

    total_time = calloc(argc - 1, sizeof(double));

    int k = 0;
    for(int i = 1; i < argc; ++i) {
        files.fileNames[k] = argv[i];
        ++k;
    }

	/* Initialize our coroutine global cooperative scheduler. */
	coro_sched_init();
	/* Start several coroutines. */
	for (int i = 0; i < 3; ++i) {
		/*
		 * The coroutines can take any 'void *' interpretation of which
		 * depends on what you want. Here as an example I give them
		 * some names.
		 */
		char name[16];
		sprintf(name, "coro_%d", i);
		/*
		 * I have to copy the name. Otherwise, all the coroutines would
		 * have the same name when they finally start.
		 */
		coro_new(coroutine_func_f, strdup(name));
	}
	/* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		/*
		 * Each 'wait' returns a finished coroutine with which you can
		 * do anything you want. Like check its exit status, for
		 * example. Don't forget to free the coroutine afterwards.
		 */
		printf("Finished %d\n", coro_status(c));
		coro_delete(c);
	}
	/* All coroutines have finished. */

    char tempFileName[256];
    sprintf(tempFileName, "%d", 0);
    strcat(tempFileName, "_temp.txt");

    FILE *myFile = fopen(tempFileName, "r");
    Vector vector;
    init_vector(&vector);

    int temp;
    while (fscanf(myFile, "%d", &temp) != EOF) {
        push_back(&vector, temp);
    }
    fclose(myFile);

    for(int i = 1; i < files.count; ++i) {
        sprintf(tempFileName, "%d", i);
        strcat(tempFileName, "_temp.txt");

        int last_sz = vector.size;

        myFile = fopen(files.fileNames[i], "r");

        while (fscanf(myFile, "%d", &temp) != EOF) {
            push_back(&vector, temp);
        }
        fclose(myFile);

        merge(vector, 0, last_sz - 1, vector.size);
    }

    FILE *writeFile = fopen("result.txt", "w");
    for (int i = 0; i < vector.size; ++i) {
        fprintf(writeFile, "%d ", vector.data[i]);
    }
    fclose(writeFile);
    freeVector(&vector);

    clock_t toc = clock();
    printf("Elapsed: %f seconds\n", (double)(toc - tic) / CLOCKS_PER_SEC);

    return 0;
}
