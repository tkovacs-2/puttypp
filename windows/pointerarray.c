#include "pointerarray.h"
#include <stdlib.h>
#include <assert.h>

static void dummy_set_index(void *, int) {}

static struct PointerArray {
    void **buffer;
    int size;
    int capacity;
    pointer_array_set_index set_index_callback;
} pointer_array = {NULL, 0, 0, dummy_set_index};

static void move_left(int first, int last) {
    for (int i=first; i<last; i++) {
        pointer_array.buffer[i] = pointer_array.buffer[i+1];
        pointer_array.set_index_callback(pointer_array.buffer[i], i);
    }
  }

static void move_right(int first, int last) {
    for (int i=last; i>first; i--) {
        pointer_array.buffer[i] = pointer_array.buffer[i-1];
        pointer_array.set_index_callback(pointer_array.buffer[i], i);
    }
}

void pointer_array_reset(pointer_array_set_index set_index_callback) {
    free(pointer_array.buffer);
    pointer_array.size = 0;
    pointer_array.capacity = 0;
    pointer_array.set_index_callback = (set_index_callback ? set_index_callback : dummy_set_index);
}

void pointer_array_clear() {
    pointer_array.size = 0;
}

int pointer_array_size() {
    return pointer_array.size;
}

void *pointer_array_get(int index) {
    assert(index >= 0 && index <= pointer_array.size);
    return pointer_array.buffer[index];
}

void pointer_array_insert(int index, void *p) {
    assert(pointer_array.size <= pointer_array.capacity && index >= 0 && index <= pointer_array.size);

    if (pointer_array.size == pointer_array.capacity) {
        if (pointer_array.capacity == 0) {
            pointer_array.capacity = 2;
            pointer_array.buffer = malloc(sizeof(void*)*pointer_array.capacity);
        } else {
            pointer_array.capacity *= 2;
            pointer_array.buffer = realloc(pointer_array.buffer, sizeof(void*)*pointer_array.capacity);
        }
    }
    move_right(index, pointer_array.size);
    pointer_array.buffer[index] = p;
    pointer_array.set_index_callback(pointer_array.buffer[index], index);
    pointer_array.size++;
}

void *pointer_array_remove(int index) {
    assert(pointer_array.size <= pointer_array.capacity && index >= 0 && index < pointer_array.size);
    void* p = pointer_array.buffer[index];
    move_left(index, pointer_array.size-1);
    pointer_array.size--;
    return p;
}

void pointer_array_exchange(int index, int new_index) {
    assert(pointer_array.size <= pointer_array.capacity &&
           index >= 0 && index < pointer_array.size &&
           new_index >= 0 && new_index < pointer_array.size);
    if (index == new_index) {
        return;
    }
    void* p = pointer_array.buffer[index];
    if (index < new_index) {
        move_left(index, new_index);
    } else {
        move_right(new_index, index);
    }
    pointer_array.buffer[new_index] = p;
    pointer_array.set_index_callback(pointer_array.buffer[new_index], new_index);
}
