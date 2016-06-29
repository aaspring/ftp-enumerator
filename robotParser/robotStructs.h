#ifndef ROBOTSTRUCTS_H
#define ROBOTSTRUCTS_H

Record_t* create_record(FieldType_e type, char* value);
void destroy_record(Record_t* record);

RecordQueue_t* record_queue_init();
void record_queue_destroy(RecordQueue_t* queue);
void record_enqueue(RecordQueue_t* queue, Record_t* record);
Record_t* record_dequeue(RecordQueue_t* queue);

void record_queue_iter_begin(RecordQueue_t* queue);
bool record_queue_iter_has_next(RecordQueue_t* queue);
Record_t* record_queue_iter_next(RecordQueue_t* queue);
void record_queue_iter_end(RecordQueue_t* queue);

#endif
