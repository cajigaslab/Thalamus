#ifdef _WIN32
#define IMPORT __declspec(dllimport)
#else
#define IMPORT
#endif

IMPORT int thalamus_start(const char* config_filename, const char* target_node, int port, bool trace);
IMPORT int thalamus_stop();
IMPORT int thalamus_push(size_t num_channels, const double* samples, const size_t* counts, const size_t* sample_intervals_ns, const char** channel_names);

