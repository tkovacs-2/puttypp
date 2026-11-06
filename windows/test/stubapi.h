#ifndef STUBAPI_H
#define STUBAPI_H

void stub_resource_add_icon(int id);
void stub_resource_add_bitmap(int id, int cx, int cy);

void stubwinapi_update_windows();
void stubwinapi_set_message_box_result(int result);

#endif
