#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <thalamus/plugin.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <cmath>
#include <future>

#include <Rec_Stim.hpp>

using namespace std::chrono_literals;

static ThalamusAPI* api = nullptr;

struct CeciNode {
  ThalamusNode base;
  ThalamusState* state;
  ThalamusStateConnection* state_connection;
  std::jthread thread;
  std::mutex mutex;
  std::condition_variable cv;
  bool triggered;
  std::vector<Channel>* channels;
  std::promise<void> promise;
  size_t time_ns;
  std::string dev1;
  std::string dev2;
};

static void CeciNode_data(ThalamusDoubleSpan* result, ThalamusNode* raw_node, int channel) {
  auto node = reinterpret_cast<CeciNode*>(raw_node);
  auto data = node->channels->at(channel).data;
  result->data = data.data();
  result->size = data.size();
}

static int CeciNode_num_channels(ThalamusNode* raw_node) {
  auto node = reinterpret_cast<CeciNode*>(raw_node);
  return node->channels->size();
}

static size_t CeciNode_sample_interval_ns(ThalamusNode* raw_node, int channel) {
  auto node = reinterpret_cast<CeciNode*>(raw_node);
  return node->channels->at(channel).sample_interval_ns;
}

static const char* CeciNode_name(ThalamusNode* node, int channel) {
  return nullptr;
}

static void CeciNode_name_span(ThalamusCharSpan* span, ThalamusNode* raw_node, int channel) {
  auto node = reinterpret_cast<CeciNode*>(raw_node);
  auto& name = node->channels->at(channel).name;
  span->data = name.data();
  span->size = name.size();
}

static char CeciNode_has_analog_data(ThalamusNode* raw_node) {
  auto node = reinterpret_cast<CeciNode*>(raw_node);
  return true;
}

static size_t CeciNode_time_ns(ThalamusNode* raw_node) {
  auto node = reinterpret_cast<CeciNode*>(raw_node);
  return node->time_ns;
}

static void CeciNode_stop(CeciNode* node) {
  if(node->thread.joinable()) {
    node->thread.request_stop();
    node->cv.notify_one();
    node->thread.join();
  }
}

static void post_ready(void* data) {
  auto node = reinterpret_cast<CeciNode*>(data);
  api->node_ready(&node->base);
  node->promise.set_value();
}

static void CeciNode_process(ThalamusNode* raw_node, ThalamusRequestHandle* handle, ThalamusJson* request) {
  auto node = reinterpret_cast<CeciNode*>(raw_node);

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
    CeciNode_stop(node);
    
    node->triggered = false;
    json["dev1"] = node->dev1;
    json["dev2"] = node->dev2;

    //This function should block until either stimulation should happen or the thread needs to stop.
    auto trigger = [node] {
      std::unique_lock<std::mutex> lock(node->mutex);
      node->cv.wait(lock);
      auto result = node->triggered;
      node->triggered = false;
      return result;
    };

    auto publish = [raw_node, node] (std::vector<Channel>* channels, size_t time_ns) {
      node->time_ns = time_ns;
      node->channels = channels;
      node->promise = std::promise<void>();
      api->io_context_post(post_ready, node);
      node->promise.get_future().get();
    };

    node->thread = std::jthread([trigger, publish, json] (std::stop_token st) {
      Rec_Stim_main(st, trigger, publish, json);
    });
  } else {
    {
      std::unique_lock<std::mutex> lock(node->mutex);
      node->triggered = true;
    }
    node->cv.notify_one();
  }

  std::string empty = "{}";
  ThalamusCharSpan response_text {empty.data(), empty.size(), 0};
  auto response = api->json_from_string(&response_text);
  api->request_respond(handle, response);
  api->json_dec_ref(response);
}

static void CeciNode_on_change(ThalamusState* source, ThalamusStateAction action, ThalamusState* key, ThalamusState* val, void* data) {
  auto node = reinterpret_cast<CeciNode*>(data);
  if(api->state_is_string(key)) {
    std::string key_str = api->state_get_string(key);

    if(key_str == "Device 1") {
      node->dev1 = api->state_get_string(val);
    } else if(key_str == "Device 2") {
      node->dev2 = api->state_get_string(val);
    }
  }
}

static ThalamusNode* create_ceci_node(ThalamusNodeFactory *, ThalamusState* state, ThalamusIoContext*, ThalamusNodeGraph*) {
  printf("create_ceci_node\n");

  auto result = new CeciNode();

  result->base.analog = new ThalamusAnalogNode();
  memset(result->base.analog, 0, sizeof(ThalamusAnalogNode));

  result->base.analog->data = CeciNode_data;
  result->base.analog->num_channels = CeciNode_num_channels;
  result->base.analog->sample_interval_ns = CeciNode_sample_interval_ns;
  result->base.analog->name = CeciNode_name;
  result->base.analog->name_span = CeciNode_name_span;
  result->base.analog->has_analog_data = CeciNode_has_analog_data;

  result->base.time_ns = CeciNode_time_ns;
  result->base.process = CeciNode_process;
  result->state = state;

  result->state_connection = api->state_recursive_change_connect(state, CeciNode_on_change, result);
  api->state_recap(state);
  
  result->triggered = false;
  result->channels = nullptr;

  return &result->base;
}

static void destroy_ceci_node(ThalamusNodeFactory *, ThalamusNode* base) {
  printf("destroy_ceci_node\n");
  auto node = reinterpret_cast<CeciNode*>(base);
  CeciNode_stop(node);

  api->state_recursive_change_disconnect(node->state_connection);
  delete base->analog;
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
