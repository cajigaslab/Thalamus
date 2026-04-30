#pragma once

#include <stop_token>
#include <functional>
#include <nlohmann/json.hpp>

int Rec_Stim_main(std::stop_token st, std::function<bool()> trigger, nlohmann::json config)