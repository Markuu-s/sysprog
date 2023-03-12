#include "Vector.h"

void init_vector(Vector *v, size_t sizeOfdata)
{
    v->capacity = START_SIZE;
    v->size = 0;
    v->sizeOfData = sizeOfdata;
    v->data = (void **)malloc(v->capacity * sizeof(void *));
}

void push_back(Vector *v, void *data)
{
    if (v->capacity == v->size)
    {
        v->capacity *= INCREASE_CAPACITY;
        v->data = realloc(v, sizeof(void*) * v->capacity);
    }
    v->data[v->size] = malloc(v->sizeOfData);
    memcpy(v->data[v->size++], data, v->sizeOfData);
}

void *get(Vector *v, int idx)
{
    if (idx < 0 || idx >= v->size)
    {
        return NULL;
    }
    return v->data[idx];
}

void freeVector(Vector *v)
{
    for(int i = 0; i < v->size; ++i){
        free(v->data[i]);
    }
    free(v->data);
}

void set(Vector *v, int idx, void *data)
{
    if (idx < 0 || idx >= v->size)
    {
        return;
    }
    memcpy(v->data[idx], data, v->sizeOfData);
}