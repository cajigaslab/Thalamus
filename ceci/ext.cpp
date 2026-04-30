#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <thalamus/plugin.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <vector>
#include <cmath>

using namespace std::chrono_literals;

static ThalamusAPI* api = nullptr;

struct CeciNode {
  ThalamusNode base;
  ThalamusState* state;
  ThalamusStateConnection* state_connection;
  ThalamusTimer* timer;
  char running;
  std::chrono::nanoseconds start_time;
  std::chrono::nanoseconds last_time;
  double frequency;
  double amplitude;
  std::vector<double> samples;
};

static ThalamusDoubleSpan CeciNode_data(ThalamusNode* node, int) {
  auto impl = (CeciNode*)node;
  return ThalamusDoubleSpan{impl->samples.data(), impl->samples.size()};
}

static int CeciNode_num_channels(ThalamusNode*) {
  return 1;
}

static size_t CeciNode_sample_interval_ns(ThalamusNode*, int) {
  return 1'000'000;
}

static const char* CeciNode_name(ThalamusNode*, int) {
  return "data";
}

static char CeciNode_has_analog_data(ThalamusNode*) {
  return true;
}

static void CeciNode_on_timer(ThalamusErrorCode* error, void* data) {
  if(api->error_code_value(error) == THALAMUS_OPERATION_ABORTED) {
    return;
  }

  auto node = static_cast<CeciNode*>(data);
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  node->samples.clear();
  while(node->last_time < now) {
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(node->last_time - node->start_time).count();
    auto elapsed_s = double(elapsed_ms)/1e3;
    node->samples.push_back(node->amplitude*std::sin(2*3.14*node->frequency*elapsed_s));
    node->last_time += 1ms;
  }

  api->node_ready(&node->base);

  if(node->running) {
    api->timer_expire_after_ns(node->timer, 16'000'000);
    api->timer_async_wait(node->timer, CeciNode_on_timer, node);
  }
}

static size_t CeciNode_time_ns(ThalamusNode* raw_node) {
  auto node = reinterpret_cast<CeciNode*>(raw_node);
  return size_t((node->last_time - 1ms).count());
}

static size_t CeciNode_process(ThalamusNode* raw_node, ThalamusRequestHandle* handle, ThalamusJson* request) {
  ThalamusCharSpan span;
  api->json_to_string(&span, request);
  auto j = nlohmann::json::parse(std::string_view(span.data, span.size));
  api->charspan_destroy(&span);

  auto node = reinterpret_cast<CeciNode*>(raw_node);
  return size_t((node->last_time - 1ms).count());
}


static void CeciNode_on_change(ThalamusState* source, ThalamusStateAction action, ThalamusState* key, ThalamusState* val, void* data) {
  auto node = static_cast<CeciNode*>(data);
  if(api->state_is_string(key)) {
    auto key_str = api->state_get_string(key);

    if(api->state_is_string(val)) {
      auto val_str = api->state_get_string(val);
      printf("%s = %s\n", key_str, val_str);
    } else if (api->state_is_bool(val)) {
      auto val_bool = api->state_get_bool(val);
      printf("%s = %d\n", key_str, val_bool);
    } else if (api->state_is_float(val)) {
      auto val_float = api->state_get_float(val);
      printf("%s = %f\n", key_str, val_float);
    }

    if(strcmp(key_str, "Running") == 0) {
      node->running = api->state_get_bool(val);
      if (node->running) {
        node->start_time = std::chrono::steady_clock::now().time_since_epoch();
        node->last_time = node->start_time;
        api->timer_expire_after_ns(node->timer, 16'000'000);
        api->timer_async_wait(node->timer, CeciNode_on_timer, node);
      }
    } else if (strcmp(key_str, "Amplitude") == 0) {
      node->amplitude = api->state_get_float(val);
    } else if (strcmp(key_str, "Frequency") == 0) {
      node->frequency = api->state_get_float(val);
    }
  }
}

static ThalamusNode* create_ceci_node(ThalamusState* state, ThalamusIoContext*, ThalamusNodeGraph*) {
  printf("create_ceci_node\n");

  auto result = new CeciNode();
  memset(&result->base, 0, sizeof(ThalamusNode));

  result->base.time_ns = CeciNode_time_ns;
  result->base.process = CeciNode_process;
  result->state = state;
  result->frequency = 1;
  result->amplitude = 1;

  result->state_connection = api->state_recursive_change_connect(state, CeciNode_on_change, result);

  result->timer = api->timer_create();

  return &result->base;
}

static void destroy_ceci_node(ThalamusNode* node) {
  printf("destroy_ceci_node\n");
  delete node;
}

static ThalamusNodeFactory demo_node_factory = {
  "EXT_CECI", create_ceci_node, destroy_ceci_node, nullptr, nullptr
};

static ThalamusNodeFactory* factories[] = {
  &demo_node_factory,
  nullptr
};

extern "C" __declspec(dllexport) ThalamusNodeFactory** get_node_factories(ThalamusAPI* _api);
extern "C" __declspec(dllexport) ThalamusNodeFactory** get_node_factories(ThalamusAPI* _api) {
  printf("get_node_factories\n");
  api = _api;
  return factories;
}
