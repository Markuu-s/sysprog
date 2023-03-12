#ifndef VECTOR_H
#define VECTOR_H

#include <stdlib.h>
#include <memory.h>

#define START_SIZE 4
#define INCREASE_CAPACITY 2

/**
 * @brief Data storage of any type
 * @param data is storage of data
 * @param size is actually size of storage
 * @param capacity is reserved memory
 * @param sizeOfData is sizeof(type)
 */
typedef struct
{
    void **data;
    int size;
    int capacity;
    size_t sizeOfData;
} Vector;

/**
 * @brief First initialization of Vector
 * 
 * @param v is pointer to Vector
 * @param sizeOfdata is sizeof(type)
 */
void init_vector(Vector *v, size_t sizeOfdata);

/**
 * @brief Add to end to vector of data
 * 
 * @param v is pointer to Vector
 * @param data element to add in back of vector
 */
void push_back(Vector *v, void *data);

/**
 * @brief Get element with index of Vector
 * 
 * @param v is pointer to Vector
 * @param idx is index of get object
 * @return void* 
 */
void *get(Vector *v, int idx);

/**
 * @brief Free memory of struct Vector
 * 
 * @param v is pointer to Vector
 */
void freeVector(Vector *v);

/**
 * @brief Change value of Vector by index
 * 
 * @param v is pointer to Vector
 * @param idx is index of variable value
 * @param data is value to set
 */
void set(Vector *v, int idx, void *data);

#endif // VECTOR_H