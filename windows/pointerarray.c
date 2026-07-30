#include "pointerarray.h"
#include <stdlib.h>
#include <assert.h>

static void move_left(PointerArray *pointer_array, int first, int last) {
    for (int i=first; i<last; i++) {
        pointer_array->buffer[i] = pointer_array->buffer[i+1];
        pointer_array->set_index_callback(pointer_array->buffer[i], i);
    }
  }

static void move_right(PointerArray *pointer_array, int first, int last) {
    for (int i=last; i>first; i--) {
        pointer_array->buffer[i] = pointer_array->buffer[i-1];
        pointer_array->set_index_callback(pointer_array->buffer[i], i);
    }
}

void pointer_array_init(PointerArray *pointer_array, PointerArraySetIndex set_index_callback) {
    pointer_array->buffer = NULL;
    pointer_array->size = 0;
    pointer_array->capacity = 0;
    pointer_array->set_index_callback = set_index_callback;
}

void pointer_array_uninit(PointerArray *pointer_array) {
    free(pointer_array->buffer);
}

void pointer_array_clear(PointerArray *pointer_array) {
    pointer_array->size = 0;
}

int pointer_array_size(PointerArray *pointer_array) {
    return pointer_array->size;
}

void *pointer_array_get(PointerArray *pointer_array, int index) {
    assert(index >= 0 && index <= pointer_array->size);
    return pointer_array->buffer[index];
}

void pointer_array_insert(PointerArray *pointer_array, int index, void *p) {
    assert(pointer_array->size <= pointer_array->capacity && index >= 0 && index <= pointer_array->size);

    if (pointer_array->size == pointer_array->capacity) {
        if (pointer_array->capacity == 0) {
            pointer_array->capacity = 2;
            pointer_array->buffer = malloc(sizeof(void*)*pointer_array->capacity);
        } else {
            pointer_array->capacity *= 2;
            pointer_array->buffer = realloc(pointer_array->buffer, sizeof(void*)*pointer_array->capacity);
        }
    }
    move_right(pointer_array, index, pointer_array->size);
    pointer_array->buffer[index] = p;
    pointer_array->set_index_callback(pointer_array->buffer[index], index);
    pointer_array->size++;
}

void *pointer_array_remove(PointerArray *pointer_array, int index) {
    assert(pointer_array->size <= pointer_array->capacity && index >= 0 && index < pointer_array->size);
    void* p = pointer_array->buffer[index];
    move_left(pointer_array, index, pointer_array->size-1);
    pointer_array->size--;
    return p;
}

void pointer_array_exchange(PointerArray *pointer_array, int index, int new_index) {
    assert(pointer_array->size <= pointer_array->capacity &&
           index >= 0 && index < pointer_array->size &&
           new_index >= 0 && new_index < pointer_array->size);
    if (index == new_index) {
        return;
    }
    void* p = pointer_array->buffer[index];
    if (index < new_index) {
        move_left(pointer_array, index, new_index);
    } else {
        move_right(pointer_array, new_index, index);
    }
    pointer_array->buffer[new_index] = p;
    pointer_array->set_index_callback(pointer_array->buffer[new_index], new_index);
}
