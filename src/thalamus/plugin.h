#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define IMPORT __declspec(dllimport)
#else
#define IMPORT
#endif

#define THALAMUS_OPERATION_ABORTED 995

#ifdef __cplusplus
extern "C" {
#endif
  enum ThalamusStateType {
    Dict,
    List,
    String,
    Int,
    Float,
    Null
  };

  enum ThalamusStateAction {
    Set,
    Delete
  };

  struct ThalamusNode;
  struct ThalamusStateConnection;

  struct ThalamusState;
  struct ThalamusStateIter;
  typedef void (*ThalamusStateRecursiveCallback)(struct ThalamusState* source, enum ThalamusStateAction action,
                                                 struct ThalamusState* key, struct ThalamusState* value, void* data);


  struct ThalamusIoContext;
  struct ThalamusNodeGraph;

  struct ThalamusDoubleSpan {
    const double* data;
    uint64_t size;
  };
  struct ThalamusShortSpan {
    const short* data;
    uint64_t size;
  };
  struct ThalamusIntSpan {
    const int* data;
    uint64_t size;
  };
  struct ThalamusULongSpan {
    const uint64_t* data;
    uint64_t size;
  };
  struct ThalamusByteSpan {
    const uint8_t* data;
    uint64_t size;
  };
  struct ThalamusMutableByteSpan {
    uint8_t* data;
    uint64_t size;
  };
  struct ThalamusCharSpan {
    const char* data;
    uint64_t size;
    char owns_data;
  };

  struct ThalamusNodeSelector {
    ThalamusCharSpan name;
    ThalamusCharSpan type;
  };

  struct ThalamusRequestHandle;

  struct ThalamusJson;
  struct ThalamusAnalogNode;
  struct ThalamusImageNode;
  struct ThalamusMocapNode;
  struct ThalamusTextNode;

  struct ThalamusNode {
    void* impl;
    uint64_t (*time_ns)(struct ThalamusNode*);
    struct ThalamusAnalogNode* analog;
    struct ThalamusMocapNode* mocap;
    struct ThalamusImageNode* image;
    struct ThalamusTextNode* text;
    void* plugin_impl;
    void (*process)(struct ThalamusNode*, struct ThalamusRequestHandle*, struct ThalamusJson*);
  };

  struct ThalamusAnalogNode {
    void (*data)(struct ThalamusDoubleSpan*, struct ThalamusNode* node, int channel);
    void (*short_data)(struct ThalamusShortSpan*, struct ThalamusNode* node, int channel);
    void (*int_data)(struct ThalamusIntSpan*, struct ThalamusNode* node, int channel);
    void (*ulong_data)(struct ThalamusULongSpan*, struct ThalamusNode* node, int channel);
    int (*num_channels)(struct ThalamusNode* node);
    uint64_t (*sample_interval_ns)(struct ThalamusNode* node, int channel);
    char (*has_analog_data)(struct ThalamusNode* node);
    char (*is_short_data)(struct ThalamusNode* node);
    char (*is_int_data)(struct ThalamusNode* node);
    char (*is_ulong_data)(struct ThalamusNode* node);
    char (*is_transformed)(struct ThalamusNode* node);
    double (*scale)(struct ThalamusNode* node, int channel);
    double (*offset)(struct ThalamusNode* node, int channel);
    void (*name)(struct ThalamusCharSpan*, struct ThalamusNode* node, int channel);
  };

  enum ThalamusImageFormat {
    Gray = 0,
    RGB = 1,
    YUYV422 = 2,
    YUV420P = 3,
    YUVJ420P = 4,
  };

  struct ThalamusImageNode {
    void (*plane)(struct ThalamusByteSpan*, struct ThalamusNode*, int channel);
    uint64_t (*num_planes)(struct ThalamusNode*);
    enum ThalamusImageFormat (*format)(struct ThalamusNode*);
    uint64_t (*width)(struct ThalamusNode*);
    uint64_t (*height)(struct ThalamusNode*);
    uint64_t (*frame_interval_ns)(struct ThalamusNode*);
    char (*has_image_data)(struct ThalamusNode*);
  };
  
  struct ThalamusMocapSegment {
    unsigned int frame;
    unsigned int segment_id;
    unsigned int time;
    float position[3];
    float rotation[4];
    unsigned char actor;
  };
  struct ThalamusMocapSegmentSpan {
    const struct ThalamusMocapSegment* data;
    uint64_t size;
  };

  struct ThalamusMocapNode {
    void (*segments)(struct ThalamusMocapSegmentSpan*, struct ThalamusNode*);
    void (*pose_name)(struct ThalamusCharSpan*, struct ThalamusNode*);
    char (*has_motion_data)(struct ThalamusNode*);
  };
  struct ThalamusTextNode {
    const char* (*text)(struct ThalamusNode*);
    char (*has_text_data)(struct ThalamusNode*);
  };

  struct ThalamusNodeFactory {
    const char* type;
    struct ThalamusNode* (*create)(struct ThalamusNodeFactory*, struct ThalamusState*, struct ThalamusIoContext*, struct ThalamusNodeGraph*);
    void (*destroy)(struct ThalamusNodeFactory*, struct ThalamusNode*);
    char (*prepare)(struct ThalamusNodeFactory*);
    void (*cleanup)(struct ThalamusNodeFactory*);
    void* plugin_impl;
  };

  struct ThalamusTimer;
  struct ThalamusErrorCode;
  typedef void (*ThalamusTimerCallback)(struct ThalamusErrorCode*, void* data);
  typedef void (*ThalamusPostCallback)(void* data);

  struct ThalamusSerialPort;
  struct ThalamusStreamBuf;
  struct ThalamusNodeGetConnection;
  struct ThalamusNodeReadyConnection;

  typedef void (*ThalamusIOCallback)(struct ThalamusErrorCode*, uint64_t, void* data);
  typedef void (*ThalamusNodeGetCallback)(struct ThalamusNode*, void* data);
  typedef void (*ThalamusNodeReadyCallback)(struct ThalamusNode*, void* data);

  struct ThalamusAPI {
    char (*state_is_dict)(struct ThalamusState*);
    char (*state_is_list)(struct ThalamusState*);
    char (*state_is_string)(struct ThalamusState*);
    char (*state_is_int)(struct ThalamusState*);
    char (*state_is_float)(struct ThalamusState*);
    char (*state_is_null)(struct ThalamusState*);
    char (*state_is_bool)(struct ThalamusState*);

    const char* (*state_get_string)(struct ThalamusState*);
    int64_t (*state_get_int)(struct ThalamusState*);
    double (*state_get_float)(struct ThalamusState*);
    char (*state_get_bool)(struct ThalamusState*);

    struct ThalamusState* (*state_get_at_name)(struct ThalamusState*, const char*);
    struct ThalamusState* (*state_get_at_index)(struct ThalamusState*, uint64_t);

    void (*state_dec_ref)(struct ThalamusState*);
    void (*state_inc_ref)(struct ThalamusState*);
    struct ThalamusStateConnection* (*state_recursive_change_connect)(struct ThalamusState* state, ThalamusStateRecursiveCallback callback, void* data);
    void (*state_recursive_change_disconnect)(struct ThalamusStateConnection* state);

    struct ThalamusTimer* (*timer_create)();
    void (*timer_destroy)(struct ThalamusTimer*);
    void (*timer_expire_after_ns)(struct ThalamusTimer*, uint64_t);
    void (*timer_async_wait)(struct ThalamusTimer*, ThalamusTimerCallback, void*);

    int (*error_code_value)(struct ThalamusErrorCode*);

    void (*node_ready)(struct ThalamusNode*);

    uint64_t (*time_ns)();
    int (*error_code_operation_aborted)();
    void (*state_recap)(struct ThalamusState*);

    void (*state_set_at_name_state)(struct ThalamusState*, const char*, struct ThalamusState*);
    void (*state_set_at_name_string)(struct ThalamusState*, const char*, const char*);
    void (*state_set_at_name_int)(struct ThalamusState*, const char*, int64_t);
    void (*state_set_at_name_float)(struct ThalamusState*, const char*, double);
    void (*state_set_at_name_null)(struct ThalamusState*, const char*);
    void (*state_set_at_name_bool)(struct ThalamusState*, const char*, char);

    void (*state_set_at_index_state)(struct ThalamusState*, int64_t, struct ThalamusState*);
    void (*state_set_at_index_string)(struct ThalamusState*, int64_t, const char*);
    void (*state_set_at_index_int)(struct ThalamusState*, int64_t, int64_t);
    void (*state_set_at_index_float)(struct ThalamusState*, int64_t, double);
    void (*state_set_at_index_null)(struct ThalamusState*, int64_t);
    void (*state_set_at_index_bool)(struct ThalamusState*, int64_t, char);

    void (*io_context_post)(ThalamusPostCallback, void*);

    void (*trace_event_begin)(const char*);

    void (*trace_event_begin_span)(const char*, uint64_t);

    void (*trace_event_end)();

    struct ThalamusSerialPort* (*serial_port_create)();

    void (*serial_port_destroy)(struct ThalamusSerialPort*);

    void (*serial_set_baud_rate)(struct ThalamusSerialPort*, uint32_t);

    void (*serial_port_open)(struct ThalamusSerialPort*, const char*);

    struct ThalamusErrorCode* (*serial_port_error)(struct ThalamusSerialPort*);

    void (*serial_port_read_until)(struct ThalamusSerialPort* port, struct ThalamusStreamBuf* buffer, char* delimiter, uint64_t delimiter_len, ThalamusIOCallback callback, void* data);
    
    void (*serial_port_read_some)(struct ThalamusSerialPort* port, struct ThalamusMutableByteSpan* span, ThalamusIOCallback callback, void* data);

    void (*serial_port_read)(struct ThalamusSerialPort* port, struct ThalamusMutableByteSpan* span, ThalamusIOCallback callback, void* data);

    void (*serial_port_write)(struct ThalamusSerialPort* port, struct ThalamusByteSpan* span, ThalamusIOCallback callback, void* data);

    struct ThalamusStreamBuf* (*streambuf_create)();
    void (*streambuf_destroy)(struct ThalamusStreamBuf* port);
    void (*streambuf_to_span)(struct ThalamusCharSpan*, struct ThalamusStreamBuf* buffer);
    void (*streambuf_consume)(struct ThalamusStreamBuf* buffer, uint64_t count);
    uint64_t (*streambuf_size)(struct ThalamusStreamBuf* buffer);
    void (*charspan_release)(struct ThalamusCharSpan* span);

    void (*error_code_message)(struct ThalamusCharSpan* result, struct ThalamusErrorCode *error);

    void (*json_to_string)(struct ThalamusCharSpan*, const struct ThalamusJson*);
    struct ThalamusJson* (*json_from_string)(const struct ThalamusCharSpan*);

    void (*request_respond)(struct ThalamusRequestHandle*, const struct ThalamusJson*);

    void (*json_inc_ref)(struct ThalamusJson* input);
    void (*json_dec_ref)(struct ThalamusJson* input);

    ThalamusNodeGetConnection* (*node_get_node)(struct ThalamusNodeSelector*, ThalamusNodeGetCallback callback, void* data);

    struct ThalamusNodeReadyConnection* (*node_ready_connect)(struct ThalamusNode*, ThalamusNodeReadyCallback callback, void* data);

    void (*node_get_node_disconnect)(struct ThalamusNodeGetConnection*);
    void (*node_ready_disconnect)(struct ThalamusNodeReadyConnection*);

    void (*node_channels_changed)(struct ThalamusNode*);

    ThalamusNodeReadyConnection* (*node_channels_changed_connect)(struct ThalamusNode*, ThalamusNodeReadyCallback callback, void* data);
    void (*node_channels_changed_disconnect)(struct ThalamusNodeReadyConnection*);

    void (*node_inc_ref)(struct ThalamusNode* node);
    void (*node_dec_ref)(struct ThalamusNode* node);

    struct ThalamusState* (*state_parent)(struct ThalamusState*);

    struct ThalamusStateIter* (*state_iter_create)(struct ThalamusState*);
    uint8_t (*state_iter_next)(struct ThalamusStateIter*);
    struct ThalamusState* (*state_iter_key)(struct ThalamusStateIter*);
    struct ThalamusState* (*state_iter_value)(struct ThalamusStateIter*);
    void (*state_iter_destroy)(struct ThalamusStateIter*);

    struct ThalamusState* (*state_key_of)(struct ThalamusState* parent, struct ThalamusState* child);

    void (*state_recap_with)(struct ThalamusState*, ThalamusStateRecursiveCallback callback, void* data);

    struct ThalamusState* (*node_get_state)(struct ThalamusNode* node);

    void (*threadpool_post)(ThalamusPostCallback, void*);
  };

  typedef struct ThalamusNodeFactory** (*thalamus_get_node_factories)(struct ThalamusAPI*);
  
#ifdef __cplusplus
}
#endif

//IMPORT int thalamus_start(const char *config_filename, const char *target_node,
//                          int port, bool trace);
//IMPORT int thalamus_stop();
//IMPORT int thalamus_push(size_t num_channels, const double *samples,
//                         const size_t *counts,
//                         const size_t *sample_intervals_ns,
//                         const char **channel_names);

