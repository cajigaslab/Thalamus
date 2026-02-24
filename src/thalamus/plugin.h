#include <stdint.h>

#ifdef _WIN32
#define IMPORT __declspec(dllimport)
#else
#define IMPORT
#endif

extern "C" {
  enum Thalamus_State_Type {
    Dict,
    List,
    String,
    Int,
    Float,
    Null
  };

  struct ThalamusState {
    void* impl;
  };
  struct ThalamusIoContext {
    void* impl;
  };
  struct ThalamusNodeGraph {
    void* impl;
  };

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

  struct ThalamusAnalog;
  struct ThalamusImage;
  struct ThalamusMocap;
  struct ThalamusText;

  struct ThalamusNode {
    void* impl;
    uint64_t (*time_ns)(ThalamusNode*);
    ThalamusAnalog* analog;
    ThalamusMocap* mocap;
    ThalamusImage* image;
    ThalamusText* text;
  };

  struct ThalamusAnalog {
    ThalamusDoubleSpan (*data)(ThalamusNode* node, int channel);
    ThalamusShortSpan (*short_data)(ThalamusNode* node, int channel);
    ThalamusIntSpan (*int_data)(ThalamusNode* node, int channel);
    ThalamusULongSpan (*ulong_data)(ThalamusNode* node, int channel);
    int (*num_channels)(ThalamusNode* node);
    uint64_t (*sample_interval_ns)(ThalamusNode* node, int channel);
    char* (*name)(ThalamusNode* node, int channel);
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

  struct ThalamusImage {
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

  struct ThalamusMocap {
    const ThalamusMocapSegmentSpan (*segments)(ThalamusNode*);
    const char* (*pose_name)(ThalamusNode*);
    //void (*inject)(ThalamusNode*, const ThalamusMocapSegmentSpan);
    char (*has_motion_data)(ThalamusNode*);
  };
  struct ThalamusText {
    char* text(ThalamusNode*);
    char (*has_text_data)(ThalamusNode*);
  };

  ThalamusNode* create_node(ThalamusState*, ThalamusIoContext*, ThalamusNodeGraph*);

  struct ThalamusNodeFactory {
    char* type;
    ThalamusNode* (*create)(ThalamusState, ThalamusIoContext, ThalamusNodeGraph);
    char (*prepare)();
    void (*cleanup)();
  };

  struct ThalamusAPI {
    Thalamus_State_Type state_get_type(ThalamusState*);
    uint64_t node_get_modalities(ThalamusNode*);
  };

  ThalamusNodeFactory* get_node_factories(ThalamusAPI*);
}

IMPORT int thalamus_start(const char *config_filename, const char *target_node,
                          int port, bool trace);
IMPORT int thalamus_stop();
IMPORT int thalamus_push(size_t num_channels, const double *samples,
                         const size_t *counts,
                         const size_t *sample_intervals_ns,
                         const char **channel_names);

