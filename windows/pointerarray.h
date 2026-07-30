#ifndef POINTERARRAY_H
#define POINTERARRAY_H

typedef void (*PointerArraySetIndex)(void *p, int index);

typedef struct {
    void **buffer;
    int size;
    int capacity;
    PointerArraySetIndex set_index_callback;
} PointerArray;

void pointer_array_init(PointerArray *pointer_array, PointerArraySetIndex set_index_callback);
void pointer_array_uninit(PointerArray *pointer_array);

void pointer_array_clear(PointerArray *pointer_array);
int pointer_array_size(PointerArray *pointer_array);
void *pointer_array_get(PointerArray *pointer_array, int index);
void pointer_array_insert(PointerArray *pointer_array, int index, void *p);
void *pointer_array_remove(PointerArray *pointer_array, int index);
void pointer_array_exchange(PointerArray *pointer_array, int index, int new_index);

#endif
