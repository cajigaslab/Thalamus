#pragma once

#include <stdint.h>

#ifdef _WIN32
#define IMPORT __declspec(dllimport)
#else
#define IMPORT
#endif

#define THALAMUS_OPERATION_ABORTED 995

extern "C" {
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
  typedef void (*ThalamusStateRecursiveCallback)(ThalamusState* source, ThalamusStateAction action,
                                                 ThalamusState* key, ThalamusState* value, void* data);


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

  struct ThalamusAnalogNode;
  struct ThalamusImageNode;
  struct ThalamusMocapNode;
  struct ThalamusTextNode;

  struct ThalamusNode {
    void* impl;
    uint64_t (*time_ns)(ThalamusNode*);
    ThalamusAnalogNode* analog;
    ThalamusMocapNode* mocap;
    ThalamusImageNode* image;
    ThalamusTextNode* text;
  };

  struct ThalamusAnalogNode {
    ThalamusDoubleSpan (*data)(ThalamusNode* node, int channel);
    ThalamusShortSpan (*short_data)(ThalamusNode* node, int channel);
    ThalamusIntSpan (*int_data)(ThalamusNode* node, int channel);
    ThalamusULongSpan (*ulong_data)(ThalamusNode* node, int channel);
    int (*num_channels)(ThalamusNode* node);
    uint64_t (*sample_interval_ns)(ThalamusNode* node, int channel);
    const char* (*name)(ThalamusNode* node, int channel);
    char (*has_analog_data)(ThalamusNode* node);
    char (*is_short_data)(ThalamusNode* node);
    char (*is_int_data)(ThalamusNode* node);
    char (*is_ulong_data)(ThalamusNode* node);
    char (*is_transformed)(ThalamusNode* node);
    double (*scale)(ThalamusNode* node, int channel);
    double (*offset)(ThalamusNode* node, int channel);
  };

  enum class ThalamusImageFormat {
    Gray,
    RGB,
    YUYV422,
    YUV420P,
    YUVJ420P,
  };

  struct ThalamusImageNode {
    ThalamusByteSpan (*plane)(ThalamusNode*, int channel);
    size_t (*num_planes)(ThalamusNode*);
    ThalamusImageFormat (*format)(ThalamusNode*);
    size_t (*width)(ThalamusNode*);
    size_t (*height)(ThalamusNode*);
    size_t (*frame_interval_ns)(ThalamusNode*);
    //void (*inject)(ThalamusNode*, const thalamus_grpc::Image &);
    char (*has_image_data)(ThalamusNode*);
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
    ThalamusMocapSegment* data;
    uint64_t size;
  };

  struct ThalamusMocapNode {
    const ThalamusMocapSegmentSpan (*segments)(ThalamusNode*);
    const char* (*pose_name)(ThalamusNode*);
    //void (*inject)(ThalamusNode*, const ThalamusMocapSegmentSpan);
    char (*has_motion_data)(ThalamusNode*);
  };
  struct ThalamusTextNode {
    const char* (*text)(ThalamusNode*);
    char (*has_text_data)(ThalamusNode*);
  };

  struct ThalamusNodeFactory {
    const char* type;
    ThalamusNode* (*create)(ThalamusState*, ThalamusIoContext*, ThalamusNodeGraph*);
    void (*destroy)(ThalamusNode*);
    char (*prepare)();
    void (*cleanup)();
  };

  struct ThalamusTimer;
  struct ThalamusErrorCode;
  typedef void (*ThalamusTimerCallback)(ThalamusErrorCode*, void* data);

  struct ThalamusAPI {
    char (*state_is_dict)(ThalamusState*);
    char (*state_is_list)(ThalamusState*);
    char (*state_is_string)(ThalamusState*);
    char (*state_is_int)(ThalamusState*);
    char (*state_is_float)(ThalamusState*);
    char (*state_is_null)(ThalamusState*);
    char (*state_is_bool)(ThalamusState*);

    const char* (*state_get_string)(ThalamusState*);
    int64_t (*state_get_int)(ThalamusState*);
    double (*state_get_float)(ThalamusState*);
    char (*state_get_bool)(ThalamusState*);

    ThalamusState* (*state_get_at_name)(ThalamusState*, const char*);
    ThalamusState* (*state_get_at_index)(ThalamusState*, size_t);

    void (*state_dec_ref)(ThalamusState*);
    void (*state_inc_ref)(ThalamusState*);
    ThalamusStateConnection* (*state_recursive_change_connect)(ThalamusState* state, ThalamusStateRecursiveCallback callback, void* data);
    void (*state_recursive_change_disconnect)(ThalamusStateConnection* state);

    ThalamusTimer* (*timer_create)();
    void (*timer_destroy)(ThalamusTimer*);
    void (*timer_expire_after_ns)(ThalamusTimer*, size_t);
    void (*timer_async_wait)(ThalamusTimer*, ThalamusTimerCallback, void*);

    int (*error_code_value)(ThalamusErrorCode*);

    void (*node_ready)(ThalamusNode*);
  };

  typedef ThalamusNodeFactory** (*thalamus_get_node_factories)(ThalamusAPI*);
}

IMPORT int thalamus_start(const char *config_filename, const char *target_node,
                          int port, bool trace);
IMPORT int thalamus_stop();
IMPORT int thalamus_push(size_t num_channels, const double *samples,
                         const size_t *counts,
                         const size_t *sample_intervals_ns,
                         const char **channel_names);

