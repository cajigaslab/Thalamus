#pragma once
#include <perfetto.h>

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("thalamus")
        .SetDescription("General Thalamus events"),
    perfetto::Category("intan")
        .SetDescription("Intan node events"));

namespace thalamus {
unsigned long long get_unique_id();
}
