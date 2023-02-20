#ifndef INC_1_VECTOR_H
#define INC_1_VECTOR_H
typedef struct
{
    int *data;
    int size;
    int capacity;
} Vector;

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

#endif //INC_1_VECTOR_H
