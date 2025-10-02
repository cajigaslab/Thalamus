#include <thalamus/tracing.hpp>
#include <ceci_node.hpp>
#include <thalamus/nidaqmx.hpp>
#include <modalities_util.hpp>
#include <vector>
#include "hdf5.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/spirit/include/qi.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define DAQmxErrChk(functionCall) if( DAQmxFailed(error=(functionCall)) ) goto Error; else
#define MAX_DEVICES 4
#define SAMPS_PER_CHAN 12500
#define TOTAL_CHANS 4
#define BUFFER_SIZE (SAMPS_PER_CHAN*TOTAL_CHANS)

typedef struct {
    const char *devName;
    TaskHandle aiHandle;
    TaskHandle aoHandle;
    TaskHandle doHandle;
    TaskHandle stimDOHandle;
    int ao_enable[TOTAL_CHANS];
    int mux_addr[4];
    double *stimData;
    uInt32 *doStimData;
    int32 stimSamps;
    uInt32 doEnableMask;
    uInt32 doStimMask;
    hid_t file_id;
    hid_t dataset_id;
} DeviceConfig;
typedef struct {
    double amp_uA;          // current amplitude in uA
    double pw_us;           // pulse width in us
    double freq_hz;         // pulse train frequency in Hz
    double ipd_ms;          // inter-phase delay in ms
    int    num_pulses;      // pulses per train
    double stim_dur_s;      // stimulation duration in seconds
    bool   is_biphasic;     // true for biphasic, false for monophasic
    int    polarity;        // 1 = cathode-leading, -1 = anode-leading
    double dis_dur_s;       // discharge duration in seconds
} StimParams;

static int32 GetTerminalNameWithDevPrefix(TaskHandle taskHandle, const char terminalName[], char triggerName[]);

static int32 setupAITask(DeviceConfig *dev, const char physicalChannels[], float64 sampleRate, float64 input_range, uInt64 sampsPerChan);
static int32 CVICALLBACK EveryNCallback(TaskHandle taskHandle, int32 everyNsamplesEventType, uInt32 nSamples, void *callbackData);
static int32 CVICALLBACK DoneCallback(TaskHandle taskHandle, int32 status, void *callbackData);

#define PI 3.14159265358979323846
static int32 setupAOTask(DeviceConfig *dev, const char physicalChannels[], float64 sampleRate, float64 input_range, uInt64 sampsPerChan);
static double square_wave(double t, double freq_hz, double duty_cycle);
static double* createStimWave(const StimParams *stim, double sampleRate, const char *ao_chan, int32 *sampsPerChan);

static int32 setupDOTask(DeviceConfig *dev, const char physicalChannels[], int32 sampsPerChan, bool32 autoStart, uInt32 *data, int32 *sampsWritten, float64 sampleRate, bool isStimTask);

static void save_to_hdf5(hid_t dataset_name, double *data, int numSamps, int numChans);

static DeviceConfig devices[2] = {
    { .devName = "PXI1Slot4", .ao_enable = {1, 1, 1, 1}, .mux_addr = {0, 0, 0, 0} },  // list primary device first
    { .devName = "PXI1Slot4_2", .ao_enable = {1, 1, 1, 1}, .mux_addr = {0, 0, 0, 0} }
};
static size_t num_devices = sizeof(devices) / sizeof(devices[0]);

static char	    AItrigName[256],AOtrigName[256],AOSampClkName[256];
static int32       error=0;
static char        errBuff[2048]={'\0'};
static float64	    sampleRate = 125000.0; // SPS
static float64	    input_range = 10.0; // +/- input range in V

static uInt32      do_enable = (1 << 8); // Set DO line 8 high to enable external power
static uInt32      do_disable = 0; // Set DO line 8 low to disable external power
static uInt32      do_stimD1 = (1 << 7); // Set DO line 7 high to enable stimulation on D1
static uInt32      do_stimD2 = (1 << 2); // Set DO line 2 high to enable stimulation on D2
static uInt32      do_stimD3 = (1 << 1); // Set DO line 1 high to enable stimulation on D3
static uInt32      do_stimD4 = (1 << 6); // Set DO line 6 high to enable stimulation on D4
static uInt32      do_stimD5 = (1 << 4); // Set DO line 4 high to enable discharging on D5
static uInt32	    do_stimD6 = (1 << 5); // Set DO line 5 high to enable discharging on D6
static uInt32      do_mux0 = (1 << 20); // Set DO line 20 high to enable MUX channel 0
static uInt32      do_mux1 = (1 << 29); // Set DO line 29 high to enable MUX channel 1
static uInt32      do_mux2 = (1 << 19); // Set DO line 19 high to enable MUX channel 2
static uInt32      do_mux3 = (1 << 26); // Set DO line 26 high to enable MUX channel 3

namespace thalamus {
struct CeciNode::Impl {
  DAQmxAPI* nidaqmx;
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  std::map<std::string, boost::signals2::scoped_connection> sources_connections;
  boost::signals2::scoped_connection channels_connection;
  size_t buffer_size;
  // double sample_rate;
  size_t counter = 0;
  int address[6];
  std::string buffer;
  std::string pose_name;
  size_t buffer_offset;
  unsigned int message_size;
  unsigned int num_props = 0;
  bool is_running = false;
  bool has_analog_data = false;
  bool has_motion_data = false;
  unsigned int frame_count;
  CeciNode *outer;
  std::string address_str;
  bool is_connected = false;
  double amplitude;
  double value;
  std::chrono::milliseconds duration;
  ObservableListPtr channels;
  NodeGraph *graph;
  std::vector<std::weak_ptr<AnalogNode>> sources;
  calculator::parser<std::string::const_iterator> parser; // Our grammar

  std::vector<std::string> names;
  std::vector<std::span<const double>> spans;
  std::chrono::nanoseconds time;
  int firstStim = 1;
public:
  Impl(ObservableDictPtr _state, boost::asio::io_context &, NodeGraph *_graph,
       CeciNode *_outer)
      : state(_state), outer(_outer), graph(_graph), nidaqmx(DAQmxAPI::get_singleton()) {

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  boost::signals2::scoped_connection source_connection;
  AnalogNode *source = nullptr;
  std::optional<calculator::program> program; // Our program (AST)
  calculator::eval eval;
  std::vector<std::vector<double>> data;

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Source") {
      auto value_str = std::get<std::string>(v);
      absl::StripAsciiWhitespace(&value_str);
      graph->get_node(value_str, [&](auto node) {
        auto locked = node.lock();
        if (!locked) {
          return;
        }
        source = std::dynamic_pointer_cast<AnalogNode>(locked).get();
        if (!source) {
          return;
        }
        source_connection = locked->ready.connect([&](auto) {
          if (!source->has_analog_data()) {
            return;
          }
          if (data.size() < static_cast<size_t>(source->num_channels())) {
            data.resize(size_t(source->num_channels()));
          }
          for (auto i = 0; i < source->num_channels(); ++i) {
            auto span = source->data(i);
            auto &transformed = data.at(size_t(i));
            transformed.assign(span.begin(), span.end());
            if (!program) {
              continue;
            }
            for (auto j = 0ull; j < transformed.size(); ++j) {
              eval.symbols["X"] = transformed.at(j);
              eval.symbols["x"] = transformed.at(j);
              auto result = eval(*program);
              if (std::holds_alternative<double>(result)) {
                transformed.at(j) = std::get<double>(result);
              } else {
                transformed.at(j) = double(std::get<long long>(result));
              }
            }
          }
          outer->ready(outer);
        });
      });
    } else if (key_str == "Equation") {
      auto value_str = std::get<std::string>(v);
      boost::spirit::ascii::space_type space;
      auto iter = value_str.cbegin();
      auto success =
          phrase_parse(iter, value_str.cend(), parser, space, program);
      (*state)["Parser Error"].assign(!success);
    }
  }

  static float64* data[MAX_DEVICES] = {0};
  static int32    sampsRead[MAX_DEVICES] = {0};

  int32 on_data(TaskHandle task_handle, int32, uInt32) {
	int32           error=0;
	char            errBuff[2048]={'\0'};

    static int32    new_sampsRead[MAX_DEVICES] = {0};
    static int32    totalRead[MAX_DEVICES] = {0};
    static float64* new_data[MAX_DEVICES] = {0};
    auto now = std::chrono::steady_clock::now().time_since_epoch();

    for (int i = 0; i < num_devices; i++) {
        new_data[i] = new float64[BUFFER_SIZE];
        new_sampsRead[i] = 0;

        DAQmxErrChk (api->DAQmxReadAnalogF64(devices[i].aiHandle,
                                        SAMPS_PER_CHAN,
                                        10.0,
                                        DAQmx_Val_GroupByScanNumber,
                                        new_data,
                                        BUFFER_SIZE,
                                        &new_sampsRead[i],
                                        NULL));
    }

    boost::asio::post(io_context, [&,new_data,sampsRead,now] {
        time = now;
        spans.clear();
        for (int i = 0; i < num_devices; i++) {
            auto offset = 0;
            if(data[i] != nullptr) {
                delete data[i];
                data[i] = new_data[i];
                sampsRead[i] = new_sampsRead[i];
                spans.push_back(std::span<const double>(new_data[i] + offset, new_data[i] + offset + sampsRead[i]));
                outer->ready(outer);
            }
        }
    });

    //printf("\r");
    //for (int i = 0; i < num_devices; i++) {
    //    printf("Device %s: %d (%d)   ", devices[i].devName, sampsRead[i], totalRead[i]);
    //}
	//fflush(stdout);

Error:
	if( DAQmxFailed(error) ) {
		api->DAQmxGetExtendedErrorInfo(errBuff,2048);
        THALAMUS_ASSERT(false, "DAQmx Error in EveryNCallback: %s", errBuff);
	}
	return 0;
  }

  static int32 CVICALLBACK NidaqCallback(TaskHandle task_handle, int32 a, uInt32 b,
                                         void *callbackData) {
    auto now = std::chrono::steady_clock::now();
    auto self = static_cast<Impl*>(callbackData);
    return self->on_data(task_handle, a, b);
  }

  void setup_stim(const StimParams& stim) {
	const char  *ao_chan_names[TOTAL_CHANS] = {"AO0", "AO1", "AO2", "AO3"};
    int32       stim_samps_per_chan = (int32)(stim.stim_dur_s * sampleRate);
	int32		do_dis_samps_per_chan = (int32)(stim.dis_dur_s * sampleRate);
    firstStim = 1;
    names.clear();

    for (size_t i = 0; i < num_devices; i++) {
        printf("Setting up device %s...\n", devices[i].devName);
        // Add MUX address to DO enable mask
        devices[i].doEnableMask = do_enable; // baseline: enable power
        if (devices[i].mux_addr[0]) devices[i].doEnableMask |= do_mux0; // MUX address line 0
        if (devices[i].mux_addr[1]) devices[i].doEnableMask |= do_mux1; // MUX address line 1
        if (devices[i].mux_addr[2]) devices[i].doEnableMask |= do_mux2; // MUX address line 2
        if (devices[i].mux_addr[3]) devices[i].doEnableMask |= do_mux3; // MUX address line 3

        char ai_channels[256], ao_channels[64], do_channels[64];
        snprintf(ai_channels, sizeof(ai_channels),
                 "%s/ai17,%s/ai16,%s/ai7,%s/ai6",
                 devices[i].devName, devices[i].devName, devices[i].devName, devices[i].devName);
        snprintf(ao_channels, sizeof(ao_channels),"%s/ao0:3", devices[i].devName);
        snprintf(do_channels, sizeof(do_channels),"%s/port0/line0:31", devices[i].devName);

        auto channel_tokens = absl::StrSplit(ai_channels, ',');
        names.insert(names.end(), channel_tokens.begin(), channel_tokens.end());

        // Setup AI Task
        DAQmxErrChk (setupAITask(&devices[i], ai_channels, sampleRate, input_range, SAMPS_PER_CHAN));
        if (i == 0) { // only set callbacks on primary device and use get AI trigger for other devices
            DAQmxErrChk (api->DAQmxRegisterEveryNSamplesEvent(devices[i].aiHandle,
                                                        DAQmx_Val_Acquired_Into_Buffer,
                                                        SAMPS_PER_CHAN,
                                                        0,
                                                        EveryNCallback,
                                                        NULL));
            DAQmxErrChk (api->DAQmxRegisterDoneEvent(devices[i].aiHandle,
                                                0,
                                                DoneCallback,
                                                NULL));
            // Configure AI Start Trigger
            DAQmxErrChk (GetTerminalNameWithDevPrefix(devices[i].aiHandle,"ai/StartTrigger",AItrigName));
        } else {  // use AI trigger from primary device for other devices
            DAQmxErrChk (api->DAQmxCfgDigEdgeStartTrig(devices[i].aiHandle,
                                                  AItrigName,
                                                  DAQmx_Val_Rising));
        }

        // Setup AO Task
        DAQmxErrChk (setupAOTask(&devices[i], ao_channels, sampleRate, input_range, stim_samps_per_chan));
        if (i == 0) { // only get AO trigger and sample clock names from primary device
            // Configure AO Start Trigger
            DAQmxErrChk (GetTerminalNameWithDevPrefix(devices[i].aoHandle,"ao/StartTrigger",AOtrigName));
            // Get AO Sample Clock name for syncing DO stim task
            DAQmxErrChk (GetTerminalNameWithDevPrefix(devices[i].aoHandle,"ao/SampleClock",AOSampClkName));
        } else {  // use AO trigger from primary device for other devices
            DAQmxErrChk (api->DAQmxCfgDigEdgeStartTrig(devices[i].aoHandle,
                                                  AOtrigName,
                                                  DAQmx_Val_Rising));
        }

        // Generate AO stim data
        float64 *stim_data = (float64*)calloc(TOTAL_CHANS * stim_samps_per_chan, sizeof(float64));  // allocate and fill stim_data array
        if (!stim_data) {
            printf("Error allocating primary stim_data array\n");
            goto Error;
        }
        double *ref_stim_wave = NULL;   // reference waveform for creating DO stim data
        int ref_samps = 0;
        for (int ch = 0; ch < TOTAL_CHANS; ch++) {   // generate stimulation waveform
            if (devices[i].ao_enable[ch]) {
                double *stim_wave = createStimWave(&stim, sampleRate, ao_chan_names[ch], &stim_samps_per_chan);
                if (!stim_wave) {
                    printf("Error creating primary stimulation waveform\n");
                    goto Error;
                }
                for (int i = 0; i < stim_samps_per_chan; i++) {
                    stim_data[i * TOTAL_CHANS + ch] = stim_wave[i];
                }
                if (ref_stim_wave == NULL) {
                    ref_stim_wave = stim_wave; // save reference waveform for creating DO stim data
                    ref_samps = stim_samps_per_chan;
                }
                else {
                    free(stim_wave); // free non-reference waveforms
                }
            }
        }
        devices[i].stimData = stim_data;
        devices[i].stimSamps = stim_samps_per_chan;

        // Generate DO stim data
        devices[i].doStimMask = devices[i].doEnableMask; // start with DO enable mask
        // Add stim DO lines depending on AO enable flags
        if (devices[i].ao_enable[0]) devices[i].doStimMask |= do_stimD4; // AO0 -> D4
        if (devices[i].ao_enable[1]) devices[i].doStimMask |= do_stimD1; // AO1 -> D1
        if (devices[i].ao_enable[2]) devices[i].doStimMask |= do_stimD3; // AO2 -> D3
        if (devices[i].ao_enable[3]) devices[i].doStimMask |= do_stimD2; // AO3 -> D2
        // Create DO stim data array
        uInt32 *do_stim_data = (uInt32*)calloc(ref_samps, sizeof(uInt32));
        if (!do_stim_data) {
            printf("Error allocating DO stim_data array\n");
            goto Error;
        }
        for (int d = 0; d < ref_samps; d++) {
            if (ref_stim_wave[d] != 0.0) {
                do_stim_data[d] = devices[i].doStimMask; // Set stim DO lines high during AO pulse
            }
            else {
                do_stim_data[d] = devices[i].doEnableMask; // Set stim DO lines low otherwise to avoid interference
            }
        }
        free(ref_stim_wave); // free reference waveform after creating DO stim data
        devices[i].doStimData = do_stim_data;

        // Setup and write baseline DO Task
        int32 doSampsWritten;
        DAQmxErrChk (setupDOTask(&devices[i], do_channels, 1, 1, &devices[i].doEnableMask, &doSampsWritten, sampleRate, false));
        DAQmxErrChk (api->DAQmxStopTask(devices[i].doHandle)); // Stop task 
        DAQmxErrChk (api->DAQmxClearTask(devices[i].doHandle)); // Clear task to release lines

        // Setup stim DO Task
        DAQmxErrChk (setupDOTask(&devices[i], do_channels, devices[i].stimSamps, 0, devices[i].doStimData, &doSampsWritten, sampleRate, true));

        // Write AO waveform to buffer
        int32 aoSampsWritten;
        DAQmxErrChk (api->DAQmxWriteAnalogF64(devices[i].aoHandle,
                                         devices[i].stimSamps,
                                         0,
                                         10.0,
                                         DAQmx_Val_GroupByScanNumber,
                                         devices[i].stimData,
                                         &aoSampsWritten,
                                         NULL));
        
        // HDF5 Setup Code
        // Create new HDF5 files using default properties
        char filename[64];
        snprintf(filename, sizeof(filename), "data_%s.h5", devices[i].devName);
        devices[i].file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

        // Create the dataspace for the datasets
        hsize_t dims[2] = {0, TOTAL_CHANS}; // initial size
        hsize_t maxdims[2] = {H5S_UNLIMITED, TOTAL_CHANS}; // maximum size
        hsize_t chunk_dims[2] = {SAMPS_PER_CHAN, TOTAL_CHANS}; // chunk size
        hid_t plist = H5Pcreate(H5P_DATASET_CREATE); // property list
        H5Pset_chunk(plist, 2, chunk_dims);

        // Create the datasets
        hid_t space = H5Screate_simple(2, dims, maxdims);
        char dataset_name[32];
        snprintf(dataset_name, sizeof(dataset_name), "%s_AI", devices[i].devName);
        devices[i].dataset_id = H5Dcreate(devices[i].file_id, dataset_name, H5T_NATIVE_DOUBLE, space,
                                    H5P_DEFAULT, plist, H5P_DEFAULT);

        H5Sclose(space);
        H5Pclose(plist);

        // Start dependent AI tasks
        if (i != 0) { // start secondary device AI task first
            DAQmxErrChk (api->DAQmxStartTask(devices[i].aiHandle));
        }
    }
    // Start primary device AI task
    DAQmxErrChk (api->DAQmxStartTask(devices[0].aiHandle));
  }

  void deliver_stim() {
      if(!firstStim) {
          // Reset DO stim tasks for next stimulation
          for (size_t i = 0; i < num_devices; i++) {
              DAQmxErrChk (api->DAQmxStopTask(devices[i].stimDOHandle));
              DAQmxErrChk (api->DAQmxStopTask(devices[i].aoHandle));

              // Load stim DO data for next stimulation
              int32 doSampsWritten = 0;
              DAQmxErrChk (api->DAQmxWriteDigitalU32(devices[i].stimDOHandle,
                                                devices[i].stimSamps,  
                                                0,  		// autostart = false
                                                10.0,
                                                DAQmx_Val_GroupByChannel,
                                                devices[i].doStimData,
                                                &doSampsWritten,
                                                NULL));
          }
      } else {
          firstStim = 0;  // after first stimulation, AO + DO tasks need to be reset before restarting
      }
      // Start AO tasks
      for (int i = (int)num_devices - 1; i >= 0; i--) { // start secondary device AO task first
          DAQmxErrChk (api->DAQmxStartTask(devices[i].stimDOHandle));
          DAQmxErrChk (api->DAQmxStartTask(devices[i].aoHandle));
      }
  }

  void teardown_stim() {
    for (size_t i = 0; i < num_devices; i++) {
        // Stop and clear AI tasks
        if(devices[i].aiHandle) {
            api->DAQmxStopTask(devices[i].aiHandle);
            api->DAQmxClearTask(devices[i].aiHandle);
            devices[i].aiHandle = 0;
        }
        // Stop and clear AO tasks
        if(devices[i].aoHandle) {
            api->DAQmxStopTask(devices[i].aoHandle);
            api->DAQmxClearTask(devices[i].aoHandle);
            devices[i].aoHandle = 0;
        }
        // Stop and clear stim DO tasks
        if(devices[i].stimDOHandle) {
            api->DAQmxStopTask(devices[i].stimDOHandle);
            api->DAQmxClearTask(devices[i].stimDOHandle);
            devices[i].stimDOHandle = 0;
        }
        // Stop and clear baseline DO tasks
        if(devices[i].doHandle) {
            api->DAQmxStopTask(devices[i].doHandle);
            api->DAQmxClearTask(devices[i].doHandle);
            devices[i].doHandle = 0;
        }
        // Force DO lines low before exiting
        int32 doSampsWritten;
        char do_channels[64];
        snprintf(do_channels, sizeof(do_channels),"%s/port0/line0:31", devices[i].devName);
        DAQmxErrChk (setupDOTask(&devices[i], do_channels, 1, 1, &do_disable, &doSampsWritten, sampleRate, false));
        DAQmxErrChk (api->DAQmxStopTask(devices[i].doHandle)); // Stop task
        DAQmxErrChk (api->DAQmxClearTask(devices[i].doHandle)); // Clear task to release lines
        // Close HDF5 file
        if (devices[i].dataset_id >= 0) {
            H5Dclose(devices[i].dataset_id);
            devices[i].dataset_id = -1;
        }
        if (devices[i].file_id >= 0) {
            H5Fclose(devices[i].file_id);
            devices[i].file_id = -1;
        }
        // Free allocated memory
        if (devices[i].stimData) {
            free(devices[i].stimData);
            devices[i].stimData = NULL;
        }
        if (devices[i].doStimData) {
            free(devices[i].doStimData);
            devices[i].doStimData = NULL;
        }
    }
  }
};

CeciNode::CeciNode(ObservableDictPtr state,
                         boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

CeciNode::~CeciNode() {}

std::string CeciNode::type_name() { return "CECI"; }

std::chrono::nanoseconds CeciNode::time() const {
  return impl->time();
}

std::span<const double> CeciNode::data(int channel) const {
  auto &data = impl->data.at(size_t(channel));
  return std::span<const double>(data.begin(), data.end());
}

int CeciNode::num_channels() const { return int(impl->data.size()); }

std::string_view CeciNode::name(int channel) const {
  return impl->source->name(channel);
}

std::span<const std::string> CeciNode::get_recommended_channels() const {
  return impl->source ? impl->source->get_recommended_channels()
                      : std::span<const std::string>();
}

std::chrono::nanoseconds CeciNode::sample_interval(int channel) const {
  return impl->source->sample_interval(channel);
}

void CeciNode::inject(const thalamus::vector<std::span<double const>> &,
                         const thalamus::vector<std::chrono::nanoseconds> &,
                         const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

bool CeciNode::has_analog_data() const { return true; }

size_t CeciNode::modalities() const {
  return infer_modalities<CeciNode>();
}

boost::json::value CeciNode::process(const boost::json::value & request) {
  auto type = request["type"].as_string();
  if(type == "teardown") {
    impl->teardown_stim();
  } else if (type == "setup") {
    StimParams stim = {
        .amp_uA = request["amp_uA"].as_double(),         // current amplitude in uA
        .pw_us = request["pw_us"].as_double(),          // pulse width in us
        .freq_hz = request["freq_hz"].as_double(),        // pulse train frequency in Hz
        .ipd_ms = request["ipd_ms"].as_double(),         // inter-phase delay in ms
        .num_pulses = request["num_pulses"].as_int(),         // pulses per train
        .stim_dur_s = request["stim_dur_s"].as_double(),       // stimulation duration in seconds
        .is_biphasic = request["is_biphasic"].as_bool(),     // true for biphasic, false for monophasic
        .polarity = request["polarity"].as_int(),           // 1 = cathode-leading, -1 = anode-leading
        .dis_dur_s = request["dis_dur_s"].as_double()       // discharge duration in seconds
    };
    impl->setup_stim(stim);
  } else if (type == "stim") {
    impl->deliver_stim();
  }
  return boost::json::object();
}

bool CeciNode::prepare() {
  auto api = DAQmxAPI::get_singleton();
}

} // namespace thalamus

double square_wave(double t, double freq_hz, double duty_cycle) {
	// Generate a square wave with given frequency and duty cycle
    double period = 1.0f / freq_hz;
    double time_in_period = fmod(t, period);
    return (time_in_period < (duty_cycle * period)) ? 1.0f : -1.0f;
}

double* createStimWave(const StimParams *stim,
                      double sampleRate,     // sampling rate in Hz
                      const char *ao_chan,  // e.g., "AO0", "AO1"
                      int32 *sampsPerChan)    // output length
{
    double dc_off = 0.0f;
    // if (strncmp(ao_chan, "AO3", 3) == 0)
    //     dc_off = 13e-4f;
    // else if (strncmp(ao_chan, "AO2", 3) == 0)
    //     dc_off = 30e-4f;
    // else if (strncmp(ao_chan, "AO1", 3) == 0)
    //     dc_off = 10e-4f;
    // else if (strncmp(ao_chan, "AO0", 3) == 0)
    //     dc_off = 20e-4f;

    double amp_V = stim->amp_uA * 1e-6f * 10000.0f; // Convert uA to V with 10kOhm load
    double pw_s = stim->pw_us * 1e-6f; // Convert pulse width from us to s
    double ipd_s = stim->ipd_ms * 1e-3f; // Convert interphase delay from ms to s
    double cycle_period_s = 1.0f / stim->freq_hz; // Period of one stim train in seconds
    int pulse_samples = (int)(pw_s * sampleRate); // Samples per pulse
    int ipd_samples = (int)(ipd_s * sampleRate); // Samples per interphase delay
    int phases_per_pulse = stim->is_biphasic ? 2 : 1; // Number of phases per pulse
    double pulse_freq_hz = 1.0f / (pw_s * phases_per_pulse); // Frequency of pulses within train
    double duty_cycle = stim->num_pulses * phases_per_pulse * pw_s * stim->freq_hz; // Duty cycle for accomplishing num_pulses per cycle
    int samps_per_cycle = (int)(cycle_period_s * sampleRate); // Samples per stim train
    int repetitions = (int)(stim->stim_dur_s / cycle_period_s);  // Number of stim train repetitions to fill stim_dur_s
    int total_samps = repetitions * samps_per_cycle;  // Total samples in final waveform

	// Allocate output waveform
    double *out_wave = (double*)calloc(total_samps, sizeof(double));
    if (!out_wave) return NULL;

    // Generate one stim train cycle
    for (int i = 0; i < samps_per_cycle; i++) {
        double t = i / sampleRate;

        if (stim->is_biphasic) {
            double duty = 0.5f * (square_wave(t, stim->freq_hz, duty_cycle) + 1.0f);
            double stim_wave = stim->polarity * amp_V - dc_off;
            double pulse = square_wave(t, pulse_freq_hz, 0.5f);
            out_wave[i] = duty * stim_wave * pulse;
        } else {
            double stim_wave = 0.5f * (stim->polarity * amp_V - dc_off) * (square_wave(t, stim->freq_hz, duty_cycle) + 1.0f);
            out_wave[i] = stim_wave;
        }
    }

    // Apply interphase delay if needed
    if (ipd_samples > 0) {
        int total_phases = stim->num_pulses * phases_per_pulse;
        int num_ipds = total_phases - 1;
        int new_samps_per_cycle = samps_per_cycle + num_ipds * ipd_samples;
        double *new_wave = (double*)calloc(new_samps_per_cycle, sizeof(double));
        if (!new_wave) { free(out_wave); return NULL; }

        int read_idx = 0, write_idx = 0;
        for (int p = 0; p < num_ipds; p++) {
            for (int s = 0; s < pulse_samples + 1; s++)
                new_wave[write_idx++] = out_wave[read_idx++];
            for (int s = 0; s < ipd_samples; s++)
                new_wave[write_idx++] = 0.0f;
        }
        // Copy last pulse
        while (read_idx < samps_per_cycle)
            new_wave[write_idx++] = out_wave[read_idx++];
        
        free(out_wave);

		// Remove extra samples added by inserting IPDs
		int samps_to_trim = new_samps_per_cycle - samps_per_cycle;
		double *trimmed_wave = (double*)malloc(sizeof(double) * samps_per_cycle);
		if (!trimmed_wave) { free(new_wave); return NULL; }
		memcpy(trimmed_wave, new_wave, sizeof(double) * samps_per_cycle);
		free(new_wave);
		out_wave = trimmed_wave;
    }

    // Repeat stim train for total stim duration
    *sampsPerChan = repetitions * samps_per_cycle;
    double *final_wave = (double*)calloc(*sampsPerChan, sizeof(double));
    if (!final_wave) { free(out_wave); return NULL; }

    for (int r = 0; r < repetitions; r++) {
        memcpy(&final_wave[r * samps_per_cycle], out_wave, samps_per_cycle * sizeof(double));
    }
    free(out_wave);

    return final_wave;
}

static int32 setupAOTask(DeviceConfig *dev, const char physicalChannels[], float64 sampleRate, float64 input_range, uInt64 sampsPerChan)
{
    int32 error = 0;

    DAQmxErrChk(api->DAQmxCreateTask("", &dev->aoHandle));
    DAQmxErrChk(api->DAQmxCreateAOVoltageChan(dev->aoHandle,
                                         physicalChannels,
                                         "",
                                         -input_range, input_range,
                                         DAQmx_Val_Volts,
                                         NULL));
    DAQmxErrChk(api->DAQmxSetRefClkSrc(dev->aoHandle, "PXI_Clk10"));
    DAQmxErrChk(api->DAQmxSetRefClkRate(dev->aoHandle, 10000000.0));
    DAQmxErrChk(api->DAQmxCfgSampClkTiming(dev->aoHandle,
                                      "",
                                      sampleRate,
                                      DAQmx_Val_Rising,
                                      DAQmx_Val_FiniteSamps,
                                      sampsPerChan));
    DAQmxErrChk(api->DAQmxSetWriteRegenMode(dev->aoHandle, DAQmx_Val_AllowRegen));
Error:
    return error;
}

static int32 setupDOTask(DeviceConfig *dev,
						 const char physicalChannels[],
						 int32 sampsPerChan,
						 bool32 autoStart,
						 uInt32 *data,
						 int32 *sampsWritten,
						 float64 sampleRate,
                        bool isStimTask)
{
    int32 error = 0;
    TaskHandle *taskHandle = isStimTask ? &dev->stimDOHandle : &dev->doHandle;

    DAQmxErrChk(api->DAQmxCreateTask("", taskHandle));
    DAQmxErrChk(api->DAQmxCreateDOChan(*taskHandle,
                                   physicalChannels,
                                   "",
                                   DAQmx_Val_ChanForAllLines));

	// If more than one sample, set up sample clock
	if (isStimTask && sampsPerChan > 1) {
		DAQmxErrChk(api->DAQmxCfgSampClkTiming(*taskHandle,
										  AOSampClkName,
										  sampleRate,
										  DAQmx_Val_Rising,
										  DAQmx_Val_FiniteSamps,
										  sampsPerChan));
	}

    DAQmxErrChk(api->DAQmxWriteDigitalU32(*taskHandle,
                                     sampsPerChan,
                                     autoStart,
                                     10.0,
                                     DAQmx_Val_GroupByChannel,
                                     data,
                                     sampsWritten,
                                     NULL));
Error:
    return error;
}

void save_to_hdf5(hid_t dataset_name, double *data, int numSamps, int numChans) {
	if (numSamps <= 0 || numChans <= 0) return;

	// Get current dataset size
	hid_t filespace = H5Dget_space(dataset_name);
	hsize_t curr_dims[2];
	H5Sget_simple_extent_dims(filespace, curr_dims, NULL);
	H5Sclose(filespace);

	// Extend dataset to accommodate new data
	hsize_t new_dims[2] = {curr_dims[0] + numSamps, hsize_t(numChans)};
	H5Dset_extent(dataset_name, new_dims);

	// Select hyperslab in extended portion
	filespace = H5Dget_space(dataset_name);
	hsize_t start[2] = {curr_dims[0], 0};
	hsize_t count[2] = {hsize_t(numSamps), hsize_t(numChans)};
	H5Sselect_hyperslab(filespace, H5S_SELECT_SET, start, NULL, count, NULL);

	// Define memory space for incoming data
	hid_t memspace = H5Screate_simple(2, count, NULL);

	// Write data to extended portion of dataset
	H5Dwrite(dataset_name, H5T_NATIVE_DOUBLE, memspace, filespace, H5P_DEFAULT, data);

	H5Sclose(memspace);
	H5Sclose(filespace);
}
