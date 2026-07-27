#include <Windows.h>

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

// These are fields used in CeciNode_stop_in_pool.  Since that function runs asynchronously
// in the threadpool it can run beyond the lifetime of CeciNode and these fields are needed
// for that function to run correctly.
struct CeciNode_StopState {
  bool dropped = false;
  std::mutex stop_mutex;
  std::condition_variable stop_cv;
  bool rec_stim_running;
};

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
  std::shared_ptr<CeciNode_StopState> stop_state;
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

static void CeciNode_name(struct ThalamusCharSpan* span, ThalamusNode* raw_node, int channel) {
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

struct CeciNode_StopData {
  std::shared_ptr<CeciNode_StopState> stop_state;
  std::jthread thread;
  std::function<void()> callback;
};

static void CeciNode_stop_in_main(void* raw) {
  auto data = reinterpret_cast<CeciNode_StopData*>(raw);
  if(!data->stop_state->dropped) {
    data->callback();
  }
  delete data;
}

static void CeciNode_stop_in_pool(void* raw) {
  auto data = reinterpret_cast<CeciNode_StopData*>(raw);
  if(data->thread.joinable()) {
    // We are stopping the Rec_Stim thread.
    data->thread.join();
    {
      std::lock_guard<std::mutex> lock(data->stop_state->stop_mutex);
      data->stop_state->rec_stim_running = false;
    }
    data->stop_state->stop_cv.notify_all();
    api->io_context_post(CeciNode_stop_in_main, data);
  } else {
    // Someone else else stopping the Rec_Stim thread and we need to wait for it to stop.
    std::unique_lock<std::mutex> lock(data->stop_state->stop_mutex);
    data->stop_state->stop_cv.wait(lock, [&]{ return !data->stop_state->rec_stim_running; });
    api->io_context_post(CeciNode_stop_in_main, data);
  }
}

/* If this function was to directly join the Rec_Stim thread while Rec_Stim is publishing
 * data a deadlock would happen.  This is because Rec_Stim publishes data by posting work
 * on the main thread and waiting for it to finish, and this function is also called on the
 * main thread.  thread.join will freeze the main thread which will prevent the publish
 * from ever finishing, and the Rec_Stim and main threads become deadlocked.  To get around
 * this we perform the join from inside of the threadpool and then return to the main thread
 * and invoke a callback.
 */
static void CeciNode_stop(CeciNode* node, std::function<void()> callback) {
  if(node->thread.joinable()) {
    node->thread.request_stop();
    node->cv.notify_all();
    auto data = new CeciNode_StopData{node->stop_state, std::move(node->thread), callback};
    api->threadpool_post(CeciNode_stop_in_pool, data);
  } else {
    auto data = new CeciNode_StopData{node->stop_state, std::jthread(), callback};
    api->threadpool_post(CeciNode_stop_in_pool, data);
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
    json["dev1"] = node->dev1;
    json["dev2"] = node->dev2;
    //Stop the Rec_Stim thread if it is running
    CeciNode_stop(node, [node,json,handle] {
      node->triggered = false;

      //This function should block until either stimulation should happen or the thread needs to stop.
      auto trigger = [node] {
        std::unique_lock<std::mutex> lock(node->mutex);
        node->cv.wait(lock);
        auto result = node->triggered;
        node->triggered = false;
        return result;
      };

      auto publish = [node] (std::vector<Channel>* channels, size_t time_ns) {
        node->time_ns = time_ns;
        node->channels = channels;
        node->promise = std::promise<void>();
        api->io_context_post(post_ready, node);
        node->promise.get_future().get();
      };

      node->stop_state->rec_stim_running = true;
      node->thread = std::jthread([trigger, publish, json] (std::stop_token st) {
        SetThreadDescription(GetCurrentThread(), L"Rec_Stim");
        Rec_Stim_main(st, trigger, publish, json);
      });

      std::string empty = "{}";
      ThalamusCharSpan response_text {empty.data(), empty.size(), 0};
      auto response = api->json_from_string(&response_text);
      api->request_respond(handle, response);
      api->json_dec_ref(response);
    });
  } else {
    {
      std::unique_lock<std::mutex> lock(node->mutex);
      node->triggered = true;
    }
    node->cv.notify_all();

    std::string empty = "{}";
    ThalamusCharSpan response_text {empty.data(), empty.size(), 0};
    auto response = api->json_from_string(&response_text);
    api->request_respond(handle, response);
    api->json_dec_ref(response);
  }
}

static void CeciNode_on_change(ThalamusState* source, ThalamusStateAction action, ThalamusState* key, ThalamusState* val, void* data) {
  auto node = reinterpret_cast<CeciNode*>(data);
  if(api->state_is_string(key)) {
    ThalamusCharSpan text;
    api->state_get_string(&text, key);
    auto key_str = std::string(text.data, text.data + text.size);

    if(key_str == "Device 1") {
      api->state_get_string(&text, val);
      node->dev1 = std::string(text.data, text.data + text.size);
    } else if(key_str == "Device 2") {
      api->state_get_string(&text, val);
      node->dev2 = std::string(text.data, text.data + text.size);
    }
  }
}

static void CeciNode_predrop(ThalamusNode* raw_node) {
  auto node = reinterpret_cast<CeciNode*>(raw_node);
  CeciNode_stop(node, [raw_node] {
    api->node_predrop_ready(raw_node);
  });
}

static ThalamusNode* create_ceci_node(ThalamusNodeFactory *, ThalamusState* state, ThalamusIoContext*, ThalamusNodeGraph*) {
  printf("create_ceci_node\n");

  auto result = new CeciNode();
  result->stop_state = std::make_shared<CeciNode_StopState>();
  result->stop_state->dropped = false;

  result->base.analog = new ThalamusAnalogNode();
  memset(result->base.analog, 0, sizeof(ThalamusAnalogNode));

  result->base.analog->data = CeciNode_data;
  result->base.analog->num_channels = CeciNode_num_channels;
  result->base.analog->sample_interval_ns = CeciNode_sample_interval_ns;
  result->base.analog->name = CeciNode_name;
  result->base.analog->has_analog_data = CeciNode_has_analog_data;

  result->base.time_ns = CeciNode_time_ns;
  result->base.process = CeciNode_process;
  result->base.predrop = CeciNode_predrop;
  result->state = state;

  result->state_connection = api->state_recursive_change_connect(state, CeciNode_on_change, result);
  api->state_recap_with(state, CeciNode_on_change, result);
  
  result->triggered = false;
  result->channels = nullptr;

  return &result->base;
}

static void destroy_ceci_node(ThalamusNodeFactory *, ThalamusNode* base) {
  printf("destroy_ceci_node\n");
  auto node = reinterpret_cast<CeciNode*>(base);
  node->stop_state->dropped = true;

  api->state_recursive_change_disconnect(node->state_connection);
  delete base->analog;
  delete node;
}

static ThalamusNodeFactory ceci_node_factory = {
  ThalamusCharSpan{"EXT_CECI",8,0}, create_ceci_node, destroy_ceci_node, nullptr, nullptr, nullptr
};

static ThalamusNodeFactory* factories[] = {
  &ceci_node_factory,
  nullptr
};

extern "C" __declspec(dllexport) ThalamusNodeFactory** thalamus_get_node_factories(ThalamusAPI* _api);
extern "C" __declspec(dllexport) ThalamusNodeFactory** thalamus_get_node_factories(ThalamusAPI* _api) {
  printf("get_node_factories\n");
  api = _api;
  return factories;
}
