#ifndef POINTERARRAY_H
#define POINTERARRAY_H

typedef void (*pointer_array_set_index)(void *p, int index);

void pointer_array_reset(pointer_array_set_index set_index_callback);

void pointer_array_clear();
int pointer_array_size();
void *pointer_array_get();
void pointer_array_insert(int index, void *p);
void *pointer_array_remove(int index);
void pointer_array_exchange(int index, int new_index);

#endif
