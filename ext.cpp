#include <stdlib.h>
#include <thalamus/plugin.h>

ThalamusAPI* thalamus;

struct DemoNode {
  ThalamusNode base;
  ThalamusState* state
}

ThalamusDoubleSpan DemoNode_data(ThalamusNode* node, int channel) {
  auto impl = (DemoNode*)node;
  return ThalamusDoubleSpan{nullptr, 0};
}

int DemoNode_num_channels(ThalamusNode* node) {
  return 1;
}

size_t DemoNode_sample_interval_ns(ThalamusNode* node, int channel) {
  return 16'000'000;
}

const char* DemoNode_name(ThalamusNode* node, int channel) {
  return "data";
}

char DemoNode_has_analog_data(ThalamusNode* node) {
  return true;
}

void DemoNode_on_change(ThalamusNode* node, ThalamusState*) {

}

ThalamusNode* create_node(ThalamusState* state, ThalamusIoContext, ThalamusNodeGraph) {
  auto raw_node = (ThalamusNode*)malloc(sizeof(DemoNode));
  auto result = (DemoNode*)raw_node;
  memset(result, 0, sizeof(DemoNode));
  result->base.analog = (ThalamusAnalogNode*)malloc(sizeof(DemoNode));
  result->base.analog->data = DemoNode_data;
  result->base.analog->num_channels = DemoNode_num_channels;
  result->base.analog->sample_interval_ns = DemoNode_sample_interval_ns;
  result->base.analog->name = DemoNode_name;
  result->base.analog->has_analog_data = DemoNode_has_analog_data;
  rsult->state = state;

  thalamus->state_recursive_change_connect(state, DemoNode_on_change, result);

  return result;
}

ThalamusNodeFactory demo_node_factory = {
  "EXT_DEMO", create_node, nullptr, nullptr
};

ThalamusNodeFactory* factories[] = {
  &demo_node_factory,
  nullptr
};

ThalamusNodeFactory* get_node_factories(ThalamusAPI* api) {
  thalamus = api;
  return factories;
}