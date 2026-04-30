#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <thalamus/plugin.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <vector>
#include <cmath>

#include <Rec_Stim.hpp>

using namespace std::chrono_literals;

static ThalamusAPI* api = nullptr;

struct CeciNode {
  ThalamusNode base;
  ThalamusState* state;
  ThalamusStateConnection* state_connection;
  std::vector<double> samples;
  std::jthread thread;
  std::mutex mutex;
  std::condition_variable cv;
};

static ThalamusDoubleSpan CeciNode_data(ThalamusNode* node, int) {
  return ThalamusDoubleSpan{nullptr, 0};
}

static int CeciNode_num_channels(ThalamusNode*) {
  return 0;
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

static size_t CeciNode_time_ns(ThalamusNode* raw_node) {
  std::chrono::steady_clock::now().count();
}

static size_t CeciNode_process(ThalamusNode* raw_node, ThalamusRequestHandle* handle, ThalamusJson* request) {
  auto node = static_cast<CeciNode*>(raw_node);

  //Currently all you can do with a ThalamusJson is convert it to a string and parse it with a JSON library.
  ThalamusCharSpan span;
  api->json_to_string(&span, request);
  auto json = nlohmann::json::parse(std::string_view(span.data, span.size));
  api->charspan_release(&span);

  //An empty json object should be interpreted as a trigger.
  std::string type = "trigger";
  if(json.find("type") != json.end()) {
    type = json["type"];
  }

  if(type == "config") {
    //Stop the Rec_Stim thread if it is running
    if(node->thread.joinable()) {
      node->thread.request_stop();
      cv.notify_one();
      node->thread.join();
    }

    //This function should block until either stimulation should happen or the thread needs to stop.
    auto trigger = [node] {
      std::unique_lock<std::mutex> lock(node->mutex);
      node->cv.wait(lock);
      auto result = node->triggered;
      triggered = false;
      return result;
    };

    node->thread = std::jthread([trigger, json] (std::stop_token st) {
      Rec_Stim_main(st, trigger, json);
    });
  } else {
    {
      std::unique_lock<std::mutex> lock(node->mutex);
      node->triggered = true;
    }
    node->cv.notify_one();
  }

  ThalamusCharSpan response_text {"{}", 2, 0};
  auto response = api->json_from_string(&response_text);
  handle->respond(response);
  api->json_dec_ref(response);
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
  }
}

static ThalamusNode* create_ceci_node(ThalamusState* state, ThalamusIoContext*, ThalamusNodeGraph*) {
  printf("create_ceci_node\n");

  auto result = new CeciNode();
  memset(&result->base, 0, sizeof(ThalamusNode));

  result->base.analog = new ThalamusAnalogNode();
  memset(result->base.analog, 0, sizeof(ThalamusAnalogNode));

  result->base.analog->data = CeciNode_data;
  result->base.analog->num_channels = CeciNode_num_channels;
  result->base.analog->sample_interval_ns = CeciNode_sample_interval_ns;
  result->base.analog->name = CeciNode_name;
  result->base.analog->has_analog_data = CeciNode_has_analog_data;

  result->base.time_ns = CeciNode_time_ns;
  result->base.process = CeciNode_process;
  result->state = state;

  result->state_connection = api->state_recursive_change_connect(state, CeciNode_on_change, result);

  return &result->base;
}

static void destroy_ceci_node(ThalamusNode* node) {
  printf("destroy_ceci_node\n");
  api->state_recursive_change_disconnect(node->state_connection);
  delete node->base.analog;
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
