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
    void (*predrop)(struct ThalamusNode*);
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
    void (*text)(struct ThalamusCharSpan*, struct ThalamusNode*);
    char (*has_text_data)(struct ThalamusNode*);
  };

  struct ThalamusNodeFactory {
    struct ThalamusCharSpan type;
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
    int32_t version;
    char (*state_is_dict)(struct ThalamusState*); // 1
    char (*state_is_list)(struct ThalamusState*); // 2
    char (*state_is_string)(struct ThalamusState*); // 3
    char (*state_is_int)(struct ThalamusState*); // 4
    char (*state_is_float)(struct ThalamusState*); // 5
    char (*state_is_null)(struct ThalamusState*); // 6
    char (*state_is_bool)(struct ThalamusState*); // 7

    void (*state_get_string)(struct ThalamusCharSpan*, struct ThalamusState*); // 8
    int64_t (*state_get_int)(struct ThalamusState*); // 9
    double (*state_get_float)(struct ThalamusState*); // 10
    char (*state_get_bool)(struct ThalamusState*); // 11

    struct ThalamusState* (*state_get_at_name)(struct ThalamusState*, const struct ThalamusCharSpan*); // 12
    struct ThalamusState* (*state_get_at_index)(struct ThalamusState*, uint64_t); // 13

    void (*state_dec_ref)(struct ThalamusState*); // 14
    void (*state_inc_ref)(struct ThalamusState*); // 15
    struct ThalamusStateConnection* (*state_recursive_change_connect)(struct ThalamusState* state, ThalamusStateRecursiveCallback callback, void* data); // 16
    void (*state_recursive_change_disconnect)(struct ThalamusStateConnection* state); // 17

    struct ThalamusTimer* (*timer_create)(); // 18
    void (*timer_destroy)(struct ThalamusTimer*); // 19
    void (*timer_expire_after_ns)(struct ThalamusTimer*, uint64_t); // 20
    void (*timer_async_wait)(struct ThalamusTimer*, ThalamusTimerCallback, void*); // 21

    int (*error_code_value)(struct ThalamusErrorCode*); // 22

    void (*node_ready)(struct ThalamusNode*); // 23

    uint64_t (*time_ns)(); // 24
    int (*error_code_operation_aborted)(); // 25
    void (*state_recap)(struct ThalamusState*); // 26

    void (*state_set_at_name_state)(struct ThalamusState*, const struct ThalamusCharSpan*, struct ThalamusState*); // 27
    void (*state_set_at_name_string)(struct ThalamusState*, const struct ThalamusCharSpan*, const struct ThalamusCharSpan*); // 28
    void (*state_set_at_name_int)(struct ThalamusState*, const struct ThalamusCharSpan*, int64_t); // 29
    void (*state_set_at_name_float)(struct ThalamusState*, const struct ThalamusCharSpan*, double); // 30
    void (*state_set_at_name_null)(struct ThalamusState*, const struct ThalamusCharSpan*); // 31
    void (*state_set_at_name_bool)(struct ThalamusState*, const struct ThalamusCharSpan*, char); // 32

    void (*state_set_at_index_state)(struct ThalamusState*, int64_t, struct ThalamusState*); // 33
    void (*state_set_at_index_string)(struct ThalamusState*, int64_t, const struct ThalamusCharSpan*); // 34
    void (*state_set_at_index_int)(struct ThalamusState*, int64_t, int64_t); // 35
    void (*state_set_at_index_float)(struct ThalamusState*, int64_t, double); // 36
    void (*state_set_at_index_null)(struct ThalamusState*, int64_t); // 37
    void (*state_set_at_index_bool)(struct ThalamusState*, int64_t, char); // 38

    void (*io_context_post)(ThalamusPostCallback, void*); // 39

    void (*trace_event_begin)(const struct ThalamusCharSpan*); // 40

    void (*trace_event_end)(); // 41

    struct ThalamusSerialPort* (*serial_port_create)(); // 42

    void (*serial_port_destroy)(struct ThalamusSerialPort*); // 43

    void (*serial_set_baud_rate)(struct ThalamusSerialPort*, uint32_t); // 44

    void (*serial_port_open)(struct ThalamusSerialPort*, const struct ThalamusCharSpan*); // 45

    struct ThalamusErrorCode* (*serial_port_error)(struct ThalamusSerialPort*); // 46

    void (*serial_port_read_until)(struct ThalamusSerialPort* port, struct ThalamusStreamBuf* buffer, const struct ThalamusCharSpan* delimiter, ThalamusIOCallback callback, void* data); // 47
    
    void (*serial_port_read_some)(struct ThalamusSerialPort* port, struct ThalamusMutableByteSpan* span, ThalamusIOCallback callback, void* data); // 48

    void (*serial_port_read)(struct ThalamusSerialPort* port, struct ThalamusMutableByteSpan* span, ThalamusIOCallback callback, void* data); // 49

    void (*serial_port_write)(struct ThalamusSerialPort* port, struct ThalamusByteSpan* span, ThalamusIOCallback callback, void* data); // 50

    struct ThalamusStreamBuf* (*streambuf_create)(); // 51
    void (*streambuf_destroy)(struct ThalamusStreamBuf* port); // 52
    void (*streambuf_to_span)(struct ThalamusCharSpan*, struct ThalamusStreamBuf* buffer); // 53
    void (*streambuf_consume)(struct ThalamusStreamBuf* buffer, uint64_t count); // 54
    uint64_t (*streambuf_size)(struct ThalamusStreamBuf* buffer); // 55
    void (*charspan_release)(struct ThalamusCharSpan* span); // 56

    void (*error_code_message)(struct ThalamusCharSpan* result, struct ThalamusErrorCode *error); // 57

    void (*json_to_string)(struct ThalamusCharSpan*, const struct ThalamusJson*); // 58
    struct ThalamusJson* (*json_from_string)(const struct ThalamusCharSpan*); // 59

    void (*request_respond)(struct ThalamusRequestHandle*, const struct ThalamusJson*); // 60

    void (*json_inc_ref)(struct ThalamusJson* input); // 61
    void (*json_dec_ref)(struct ThalamusJson* input); // 62

    ThalamusNodeGetConnection* (*node_get_node)(struct ThalamusNodeSelector*, ThalamusNodeGetCallback callback, void* data); // 63

    struct ThalamusNodeReadyConnection* (*node_ready_connect)(struct ThalamusNode*, ThalamusNodeReadyCallback callback, void* data); // 64

    void (*node_get_node_disconnect)(struct ThalamusNodeGetConnection*); // 65
    void (*node_ready_disconnect)(struct ThalamusNodeReadyConnection*); // 66

    void (*node_channels_changed)(struct ThalamusNode*); // 67

    ThalamusNodeReadyConnection* (*node_channels_changed_connect)(struct ThalamusNode*, ThalamusNodeReadyCallback callback, void* data); // 68
    void (*node_channels_changed_disconnect)(struct ThalamusNodeReadyConnection*); // 69

    void (*node_inc_ref)(struct ThalamusNode* node); // 70
    void (*node_dec_ref)(struct ThalamusNode* node); // 71

    struct ThalamusState* (*state_parent)(struct ThalamusState*); // 72

    struct ThalamusStateIter* (*state_iter_create)(struct ThalamusState*); // 73
    uint8_t (*state_iter_next)(struct ThalamusStateIter*); // 74
    struct ThalamusState* (*state_iter_key)(struct ThalamusStateIter*); // 75
    struct ThalamusState* (*state_iter_value)(struct ThalamusStateIter*); // 76
    void (*state_iter_destroy)(struct ThalamusStateIter*); // 77

    struct ThalamusState* (*state_key_of)(struct ThalamusState* parent, struct ThalamusState* child); // 78

    void (*state_recap_with)(struct ThalamusState*, ThalamusStateRecursiveCallback callback, void* data); // 79

    struct ThalamusState* (*node_get_state)(struct ThalamusNode* node); // 80

    void (*threadpool_post)(ThalamusPostCallback, void*); // 81

    struct ThalamusNodeReadyConnection* (*node_ready_multithreaded_connect)(struct ThalamusNode*, ThalamusNodeReadyCallback callback, void* data); // 82
    void (*node_ready_offmain)(struct ThalamusNode*); // 83
    void (*node_predrop_ready)(struct ThalamusNode*); // 84
  };

  typedef struct ThalamusNodeFactory** (*thalamus_get_node_factories)(struct ThalamusAPI*);
  
#ifdef __cplusplus
}
#endif
