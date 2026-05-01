#pragma once

#include <stop_token>
#include <functional>
#include <nlohmann/json.hpp>

struct Channel {
  std::span<double> data;
  std::string name;
  size_t sample_interval_ns;
};

int Rec_Stim_main(std::stop_token st, std::function<bool()> trigger,
                  std::function<void(std::vector<Channel>*, size_t)> publish, nlohmann::json config);