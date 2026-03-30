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
  typedef void (*ThalamusStateRecursiveCallback)(struct ThalamusState* source, enum ThalamusStateAction action,
                                                 struct ThalamusState* key, struct ThalamusState* value, void* data);


  struct ThalamusIoContext;
  struct ThalamusNodeGraph;

  struct ThalamusDoubleSpan {
    double* data;
    size_t size;
  };
  struct ThalamusShortSpan {
    short* data;
    size_t size;
  };
  struct ThalamusIntSpan {
    int* data;
    size_t size;
  };
  struct ThalamusULongSpan {
    uint64_t* data;
    size_t size;
  };
  struct ThalamusByteSpan {
    uint8_t* data;
    uint64_t size;
  };
  struct ThalamusCharSpan {
    char* data;
    uint64_t size;
    char owns_data;
  };

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
  };

  struct ThalamusAnalogNode {
    void (*data)(struct ThalamusDoubleSpan*, struct ThalamusNode* node, int channel);
    void (*short_data)(struct ThalamusShortSpan*, struct ThalamusNode* node, int channel);
    void (*int_data)(struct ThalamusIntSpan*, struct ThalamusNode* node, int channel);
    void (*ulong_data)(struct ThalamusULongSpan*, struct ThalamusNode* node, int channel);
    int (*num_channels)(struct ThalamusNode* node);
    uint64_t (*sample_interval_ns)(struct ThalamusNode* node, int channel);
    const char* (*name)(struct ThalamusNode* node, int channel);
    char (*has_analog_data)(struct ThalamusNode* node);
    char (*is_short_data)(struct ThalamusNode* node);
    char (*is_int_data)(struct ThalamusNode* node);
    char (*is_ulong_data)(struct ThalamusNode* node);
    char (*is_transformed)(struct ThalamusNode* node);
    double (*scale)(struct ThalamusNode* node, int channel);
    double (*offset)(struct ThalamusNode* node, int channel);
    void (*name_span)(struct ThalamusCharSpan*, struct ThalamusNode* node, int channel);
  };

  enum ThalamusImageFormat {
    Gray,
    RGB,
    YUYV422,
    YUV420P,
    YUVJ420P,
  };

  struct ThalamusImageNode {
    void (*plane)(struct ThalamusByteSpan*, struct ThalamusNode*, int channel);
    size_t (*num_planes)(struct ThalamusNode*);
    enum ThalamusImageFormat (*format)(struct ThalamusNode*);
    size_t (*width)(struct ThalamusNode*);
    size_t (*height)(struct ThalamusNode*);
    size_t (*frame_interval_ns)(struct ThalamusNode*);
    //void (*inject)(ThalamusNode*, const thalamus_grpc::Image &);
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
    struct ThalamusMocapSegment* data;
    uint64_t size;
  };

  struct ThalamusMocapNode {
    void (*segments)(struct ThalamusMocapSegmentSpan*, struct ThalamusNode*);
    const char* (*pose_name)(struct ThalamusNode*);
    //void (*inject)(ThalamusNode*, const ThalamusMocapSegmentSpan);
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
  };

  struct ThalamusTimer;
  struct ThalamusErrorCode;
  typedef void (*ThalamusTimerCallback)(struct ThalamusErrorCode*, void* data);
  typedef void (*ThalamusPostCallback)(void* data);

  struct ThalamusSerialPort;
  struct ThalamusStreamBuf;

  typedef void (*ThalamusIOCallback)(ThalamusErrorCode*, size_t, void* data);

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
    struct ThalamusState* (*state_get_at_index)(struct ThalamusState*, size_t);

    void (*state_dec_ref)(struct ThalamusState*);
    void (*state_inc_ref)(struct ThalamusState*);
    struct ThalamusStateConnection* (*state_recursive_change_connect)(struct ThalamusState* state, ThalamusStateRecursiveCallback callback, void* data);
    void (*state_recursive_change_disconnect)(struct ThalamusStateConnection* state);

    struct ThalamusTimer* (*timer_create)();
    void (*timer_destroy)(struct ThalamusTimer*);
    void (*timer_expire_after_ns)(struct ThalamusTimer*, size_t);
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

    void (*trace_event_begin_span)(const char*, size_t);

    void (*trace_event_end)();

    ThalamusSerialPort* (*serial_port_create)();

    void (*serial_port_destroy)(ThalamusSerialPort*);

    void (*serial_set_baud_rate)(ThalamusSerialPort*, uint32_t);

    void (*serial_port_open)(ThalamusSerialPort*, const char*);

    ThalamusErrorCode* (*serial_port_error)(ThalamusSerialPort*);

    void (*serial_port_read_until)(ThalamusSerialPort* port, ThalamusStreamBuf* buffer, char* delimiter, size_t delimiter_len, ThalamusIOCallback callback, void* data);
    
    void (*serial_port_read_some)(ThalamusSerialPort* port, struct ThalamusByteSpan* span, ThalamusIOCallback callback, void* data);

    void (*serial_port_read)(ThalamusSerialPort* port, struct ThalamusByteSpan* span, ThalamusIOCallback callback, void* data);

    void (*serial_port_write)(ThalamusSerialPort* port, struct ThalamusByteSpan* span, ThalamusIOCallback callback, void* data);

    ThalamusStreamBuf* (*streambuf_create)();
    void (*streambuf_destroy)(ThalamusStreamBuf* port);
    void (*streambuf_to_span)(struct ThalamusCharSpan*, ThalamusStreamBuf* buffer);
    void (*streambuf_consume)(ThalamusStreamBuf* buffer, size_t count);
    size_t (*streambuf_size)(ThalamusStreamBuf* buffer);
    void (*charspan_destroy)(ThalamusCharSpan* span);
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

