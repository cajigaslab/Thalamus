//------------------------------------------------------------------------------
// 1. Include this file with executable projects in order to dynamically connect to the camera SDK.
// 2. Start with a call to tl_camera_sdk_dll_initialize to load the shared libraries and look up function pointers.
// 3. Invoke camera APIs through the provided function pointers.
// 4. After closing any open cameras and closing the SDK, call tl_camera_sdk_dll_terminate to unload the shared libraries.
//
// See the documentation and example programs for details.
//------------------------------------------------------------------------------

#include <stdio.h>
#include "tl_camera_sdk.h"

#ifndef THORLABS_TSI_BUILD_DLL

#ifdef _WIN32
#include "Windows.h"
static const char* DLL_NAME = "thorlabs_tsi_camera_sdk.dll";
static HMODULE thorlabs_tsi_camera_sdk_shared_library = NULL;
#else
#include <unistd.h>
#include "dlfcn.h"
static const char* DLL_NAME = "libthorlabs_tsi_camera_sdk.so";
static void* thorlabs_tsi_camera_sdk_shared_library = 0;
#endif

_INTERNAL_COMMAND _internal_command;

TL_CAMERA_ARM tl_camera_arm;

TL_CAMERA_CLOSE_CAMERA tl_camera_close_camera;

TL_CAMERA_CLOSE_SDK tl_camera_close_sdk;

TL_CAMERA_CONVERT_DECIBELS_TO_GAIN tl_camera_convert_decibels_to_gain;

TL_CAMERA_CONVERT_GAIN_TO_DECIBELS tl_camera_convert_gain_to_decibels;

TL_CAMERA_DISARM tl_camera_disarm;

TL_CAMERA_DISCOVER_AVAILABLE_CAMERAS tl_camera_discover_available_cameras;

TL_CAMERA_GET_BINX tl_camera_get_binx;

TL_CAMERA_GET_BINX_RANGE tl_camera_get_binx_range;

TL_CAMERA_GET_BINY tl_camera_get_biny;

TL_CAMERA_GET_BIT_DEPTH tl_camera_get_bit_depth;

TL_CAMERA_GET_BLACK_LEVEL tl_camera_get_black_level;

TL_CAMERA_GET_BLACK_LEVEL_RANGE tl_camera_get_black_level_range;

TL_CAMERA_GET_BINY_RANGE tl_camera_get_biny_range;

TL_CAMERA_GET_CAMERA_COLOR_CORRECTION_MATRIX_OUTPUT_COLOR_SPACE tl_camera_get_camera_color_correction_matrix_output_color_space;

TL_CAMERA_GET_CAMERA_SENSOR_TYPE tl_camera_get_camera_sensor_type;

TL_CAMERA_GET_COLOR_CORRECTION_MATRIX tl_camera_get_color_correction_matrix;

TL_CAMERA_GET_COLOR_FILTER_ARRAY_PHASE tl_camera_get_color_filter_array_phase;

TL_CAMERA_GET_COMMUNICATION_INTERFACE tl_camera_get_communication_interface;

TL_CAMERA_GET_DATA_RATE tl_camera_get_data_rate;

TL_CAMERA_GET_DEFAULT_WHITE_BALANCE_MATRIX tl_camera_get_default_white_balance_matrix;

TL_CAMERA_GET_EEP_STATUS tl_camera_get_eep_status;

TL_CAMERA_GET_EXPOSURE_TIME tl_camera_get_exposure_time;

TL_CAMERA_GET_EXPOSURE_TIME_RANGE tl_camera_get_exposure_time_range;

TL_CAMERA_GET_FIRMWARE_VERSION tl_camera_get_firmware_version;

TL_CAMERA_GET_FRAME_RATE_CONTROL_VALUE tl_camera_get_frame_rate_control_value;

TL_CAMERA_GET_FRAME_RATE_CONTROL_VALUE_RANGE tl_camera_get_frame_rate_control_value_range;

TL_CAMERA_GET_FRAME_TIME tl_camera_get_frame_time;

TL_CAMERA_GET_FRAMES_PER_TRIGGER_RANGE tl_camera_get_frames_per_trigger_range;

TL_CAMERA_GET_FRAMES_PER_TRIGGER_ZERO_FOR_UNLIMITED tl_camera_get_frames_per_trigger_zero_for_unlimited;

TL_CAMERA_GET_GAIN tl_camera_get_gain;

TL_CAMERA_GET_GAIN_RANGE tl_camera_get_gain_range;

TL_CAMERA_GET_HOT_PIXEL_CORRECTION_THRESHOLD tl_camera_get_hot_pixel_correction_threshold;

TL_CAMERA_GET_HOT_PIXEL_CORRECTION_THRESHOLD_RANGE tl_camera_get_hot_pixel_correction_threshold_range;

TL_CAMERA_GET_IMAGE_HEIGHT tl_camera_get_image_height;

TL_CAMERA_GET_IMAGE_HEIGHT_RANGE tl_camera_get_image_height_range;

TL_CAMERA_GET_IMAGE_POLL_TIMEOUT tl_camera_get_image_poll_timeout;

TL_CAMERA_GET_IMAGE_WIDTH tl_camera_get_image_width;

TL_CAMERA_GET_IMAGE_WIDTH_RANGE tl_camera_get_image_width_range;

TL_CAMERA_GET_IS_ARMED tl_camera_get_is_armed;

TL_CAMERA_GET_IS_COOLING_SUPPORTED tl_camera_get_is_cooling_supported;

TL_CAMERA_GET_IS_DATA_RATE_SUPPORTED tl_camera_get_is_data_rate_supported;

TL_CAMERA_GET_IS_EEP_SUPPORTED tl_camera_get_is_eep_supported;

TL_CAMERA_GET_IS_FRAME_RATE_CONTROL_ENABLED tl_camera_get_is_frame_rate_control_enabled;

TL_CAMERA_GET_IS_HOT_PIXEL_CORRECTION_ENABLED tl_camera_get_is_hot_pixel_correction_enabled;

TL_CAMERA_GET_IS_LED_ON tl_camera_get_is_led_on;

TL_CAMERA_GET_IS_LED_SUPPORTED tl_camera_get_is_led_supported;

TL_CAMERA_GET_IS_NIR_BOOST_SUPPORTED tl_camera_get_is_nir_boost_supported;

TL_CAMERA_GET_IS_OPERATION_MODE_SUPPORTED tl_camera_get_is_operation_mode_supported;

TL_CAMERA_GET_IS_TAPS_SUPPORTED tl_camera_get_is_taps_supported;

TL_CAMERA_GET_LAST_ERROR tl_camera_get_last_error;

TL_CAMERA_GET_MEASURED_FRAME_RATE tl_camera_get_measured_frame_rate;

TL_CAMERA_GET_MODEL tl_camera_get_model;

TL_CAMERA_GET_MODEL_STRING_LENGTH_RANGE tl_camera_get_model_string_length_range;

TL_CAMERA_GET_NAME tl_camera_get_name;

TL_CAMERA_GET_NAME_STRING_LENGTH_RANGE tl_camera_get_name_string_length_range;

TL_CAMERA_GET_NIR_BOOST_ENABLE tl_camera_get_nir_boost_enable;

TL_CAMERA_GET_OPERATION_MODE tl_camera_get_operation_mode;

TL_CAMERA_GET_PENDING_FRAME_OR_NULL tl_camera_get_pending_frame_or_null;

TL_CAMERA_GET_POLAR_PHASE tl_camera_get_polar_phase;

TL_CAMERA_GET_ROI tl_camera_get_roi;

TL_CAMERA_GET_ROI_RANGE tl_camera_get_roi_range;

TL_CAMERA_GET_SENSOR_HEIGHT tl_camera_get_sensor_height;

TL_CAMERA_GET_SENSOR_PIXEL_HEIGHT tl_camera_get_sensor_pixel_height;

TL_CAMERA_GET_SENSOR_PIXEL_SIZE_BYTES tl_camera_get_sensor_pixel_size_bytes;

TL_CAMERA_GET_SENSOR_PIXEL_WIDTH tl_camera_get_sensor_pixel_width;

TL_CAMERA_GET_SENSOR_READOUT_TIME tl_camera_get_sensor_readout_time;

TL_CAMERA_GET_SENSOR_WIDTH tl_camera_get_sensor_width;

TL_CAMERA_GET_SERIAL_NUMBER tl_camera_get_serial_number;

TL_CAMERA_GET_SERIAL_NUMBER_STRING_LENGTH_RANGE tl_camera_get_serial_number_string_length_range;

TL_CAMERA_GET_TAP_BALANCE_ENABLE tl_camera_get_tap_balance_enable;

TL_CAMERA_GET_TAPS tl_camera_get_taps;

TL_CAMERA_GET_TIMESTAMP_CLOCK_FREQUENCY tl_camera_get_timestamp_clock_frequency;

TL_CAMERA_GET_TRIGGER_POLARITY tl_camera_get_trigger_polarity;

TL_CAMERA_GET_USB_PORT_TYPE tl_camera_get_usb_port_type;

TL_CAMERA_GET_USER_MEMORY tl_camera_get_user_memory;

TL_CAMERA_GET_USER_MEMORY_MAXIMUM_SIZE tl_camera_get_user_memory_maximum_size;

TL_CAMERA_ISSUE_SOFTWARE_TRIGGER tl_camera_issue_software_trigger;

TL_CAMERA_OPEN_CAMERA tl_camera_open_camera;

TL_CAMERA_OPEN_SDK tl_camera_open_sdk;

TL_CAMERA_SET_BINX tl_camera_set_binx;

TL_CAMERA_SET_BINY tl_camera_set_biny;

TL_CAMERA_SET_BLACK_LEVEL tl_camera_set_black_level;

TL_CAMERA_SET_CAMERA_CONNECT_CALLBACK tl_camera_set_camera_connect_callback;

TL_CAMERA_SET_CAMERA_DISCONNECT_CALLBACK tl_camera_set_camera_disconnect_callback;

TL_CAMERA_GET_IS_COOLING_ENABLED tl_camera_get_is_cooling_enabled;

TL_CAMERA_SET_DATA_RATE tl_camera_set_data_rate;

TL_CAMERA_SET_EXPOSURE_TIME tl_camera_set_exposure_time;

TL_CAMERA_SET_FRAME_AVAILABLE_CALLBACK tl_camera_set_frame_available_callback;

TL_CAMERA_SET_FRAME_RATE_CONTROL_VALUE tl_camera_set_frame_rate_control_value;

TL_CAMERA_SET_FRAMES_PER_TRIGGER_ZERO_FOR_UNLIMITED tl_camera_set_frames_per_trigger_zero_for_unlimited;

TL_CAMERA_SET_GAIN tl_camera_set_gain;

TL_CAMERA_SET_HOT_PIXEL_CORRECTION_THRESHOLD tl_camera_set_hot_pixel_correction_threshold;

TL_CAMERA_SET_IMAGE_POLL_TIMEOUT tl_camera_set_image_poll_timeout;

TL_CAMERA_SET_IS_EEP_ENABLED tl_camera_set_is_eep_enabled;

TL_CAMERA_SET_IS_FRAME_RATE_CONTROL_ENABLED tl_camera_set_is_frame_rate_control_enabled;

TL_CAMERA_SET_IS_HOT_PIXEL_CORRECTION_ENABLED tl_camera_set_is_hot_pixel_correction_enabled;

TL_CAMERA_SET_IS_LED_ON tl_camera_set_is_led_on;

TL_CAMERA_SET_NAME tl_camera_set_name;

TL_CAMERA_SET_NIR_BOOST_ENABLE tl_camera_set_nir_boost_enable;

TL_CAMERA_SET_OPERATION_MODE tl_camera_set_operation_mode;

TL_CAMERA_SET_ROI tl_camera_set_roi;

TL_CAMERA_SET_TAPS tl_camera_set_taps;

TL_CAMERA_SET_TAP_BALANCE_ENABLE tl_camera_set_tap_balance_enable;

TL_CAMERA_SET_TRIGGER_POLARITY tl_camera_set_trigger_polarity;

TL_CAMERA_SET_USER_MEMORY tl_camera_set_user_memory;

typedef void* (*GET_FUNCTION)(void*, char*);

typedef void (*TL_MODULE_INITIALIZE)(void* device_module_manager_param);

typedef void (*TL_MODULE_UNINITIALIZE)(void* device_module_manager_param);

typedef void* (*GET_MODULE_FUNCTION)(void*, const char*);

static GET_FUNCTION get_function_from_shared_library__or_nullptr = 0;

/// <summary>
///     Initializes the camera sdk function pointers to 0.
/// </summary>
static void set_camera_functions_to_null()
{
    _internal_command = 0;
    tl_camera_arm = 0;
    tl_camera_close_camera = 0;
    tl_camera_close_sdk = 0;
    tl_camera_convert_decibels_to_gain = 0;
    tl_camera_convert_gain_to_decibels = 0;
    tl_camera_disarm = 0;
    tl_camera_discover_available_cameras = 0;
    tl_camera_get_binx = 0;
    tl_camera_get_binx_range = 0;
    tl_camera_get_biny = 0;
    tl_camera_get_bit_depth = 0;
    tl_camera_get_black_level = 0;
    tl_camera_get_black_level_range = 0;
    tl_camera_get_biny_range = 0;
    tl_camera_get_camera_color_correction_matrix_output_color_space = 0;
    tl_camera_get_camera_sensor_type = 0;
    tl_camera_get_color_correction_matrix = 0;
    tl_camera_get_color_filter_array_phase = 0;
    tl_camera_get_is_cooling_enabled = 0;
    tl_camera_get_communication_interface = 0;
    tl_camera_get_data_rate = 0;
    tl_camera_get_default_white_balance_matrix = 0;
    tl_camera_get_eep_status = 0;
    tl_camera_get_exposure_time = 0;
    tl_camera_get_exposure_time_range = 0;
    tl_camera_get_firmware_version = 0;
    tl_camera_get_frame_rate_control_value = 0;
    tl_camera_get_frame_rate_control_value_range = 0;
    tl_camera_get_frame_time = 0;
    tl_camera_get_frames_per_trigger_range = 0;
    tl_camera_get_frames_per_trigger_zero_for_unlimited = 0;
    tl_camera_get_gain = 0;
    tl_camera_get_gain_range = 0;
    tl_camera_get_hot_pixel_correction_threshold = 0;
    tl_camera_get_hot_pixel_correction_threshold_range = 0;
    tl_camera_get_image_height = 0;
    tl_camera_get_image_height_range = 0;
    tl_camera_get_image_poll_timeout = 0;
    tl_camera_get_image_width = 0;
    tl_camera_get_image_width_range = 0;
    tl_camera_get_is_armed = 0;
    tl_camera_get_is_cooling_supported = 0;
    tl_camera_get_is_data_rate_supported = 0;
    tl_camera_get_is_eep_supported = 0;
    tl_camera_get_is_frame_rate_control_enabled = 0;
    tl_camera_get_is_hot_pixel_correction_enabled = 0;
    tl_camera_get_is_led_on = 0;
    tl_camera_get_is_led_supported = 0;
    tl_camera_get_is_nir_boost_supported = 0;
    tl_camera_get_is_operation_mode_supported = 0;
    tl_camera_get_is_taps_supported = 0;
    tl_camera_get_last_error = 0;
    tl_camera_get_measured_frame_rate = 0;
    tl_camera_get_model = 0;
    tl_camera_get_model_string_length_range = 0;
    tl_camera_get_name = 0;
    tl_camera_get_name_string_length_range = 0;
    tl_camera_get_nir_boost_enable = 0;
    tl_camera_get_operation_mode = 0;
    tl_camera_get_pending_frame_or_null = 0;
    tl_camera_get_polar_phase = 0;
    tl_camera_get_roi = 0;
    tl_camera_get_roi_range = 0;
    tl_camera_get_sensor_height = 0;
    tl_camera_get_sensor_pixel_height = 0;
    tl_camera_get_sensor_pixel_size_bytes = 0;
    tl_camera_get_sensor_pixel_width = 0;
    tl_camera_get_sensor_readout_time = 0;
    tl_camera_get_sensor_width = 0;
    tl_camera_get_serial_number = 0;
    tl_camera_get_serial_number_string_length_range = 0;
    tl_camera_get_tap_balance_enable = 0;
    tl_camera_get_taps = 0;
    tl_camera_get_timestamp_clock_frequency = 0;
    tl_camera_get_trigger_polarity = 0;
    tl_camera_get_usb_port_type = 0;
    tl_camera_get_user_memory = 0;
    tl_camera_get_user_memory_maximum_size = 0;
    tl_camera_issue_software_trigger = 0;
    tl_camera_open_camera = 0;
    tl_camera_open_sdk = 0;
    tl_camera_set_binx = 0;
    tl_camera_set_biny = 0;
    tl_camera_set_black_level = 0;
    tl_camera_set_camera_connect_callback = 0;
    tl_camera_set_camera_disconnect_callback = 0;
    tl_camera_set_data_rate = 0;
    tl_camera_set_exposure_time = 0;
    tl_camera_set_frame_available_callback = 0;
    tl_camera_set_frame_rate_control_value = 0;
    tl_camera_set_frames_per_trigger_zero_for_unlimited = 0;
    tl_camera_set_gain = 0;
    tl_camera_set_hot_pixel_correction_threshold = 0;
    tl_camera_set_image_poll_timeout = 0;
    tl_camera_set_is_eep_enabled = 0;
    tl_camera_set_is_frame_rate_control_enabled = 0;
    tl_camera_set_is_hot_pixel_correction_enabled = 0;
    tl_camera_set_is_led_on = 0;
    tl_camera_set_name = 0;
    tl_camera_set_nir_boost_enable = 0;
    tl_camera_set_operation_mode = 0;
    tl_camera_set_roi = 0;
    tl_camera_set_taps = 0;
    tl_camera_set_tap_balance_enable = 0;
    tl_camera_set_trigger_polarity = 0;
    tl_camera_set_user_memory = 0;
}

static int init_error_cleanup()
{
    if (thorlabs_tsi_camera_sdk_shared_library != NULL)
    {
#ifdef _WIN32
        FreeLibrary(thorlabs_tsi_camera_sdk_shared_library);
#else
        dlclose(thorlabs_tsi_camera_sdk_shared_library);
#endif
    }

    thorlabs_tsi_camera_sdk_shared_library = 0;
    get_function_from_shared_library__or_nullptr = 0;

    set_camera_functions_to_null();

    return 1; // 1 for error
}

/// <summary>
///     Loads the shared libraries and maps all the functions so that they can be called directly.
/// </summary>
/// <returns>
///     1 for error, 0 for success
/// </returns>
int tl_camera_sdk_dll_initialize(void)
{
    set_camera_functions_to_null();

    // Platform specific code to get a handle to the SDK kernel module.
#ifdef _WIN32
    thorlabs_tsi_camera_sdk_shared_library = LoadLibraryA(DLL_NAME);
    if (!thorlabs_tsi_camera_sdk_shared_library)
    {
        return init_error_cleanup();
    }

    get_function_from_shared_library__or_nullptr = (GET_FUNCTION)GetProcAddress(thorlabs_tsi_camera_sdk_shared_library, "get_function_from_shared_library__or_nullptr");  // NOLINT(clang-diagnostic-cast-function-type)
    if (!get_function_from_shared_library__or_nullptr)
    {
        return init_error_cleanup();
    }
#else
    // First look in the current folder for the .so entry dll, then in the path (/usr/local/lib most likely).
    char local_path_to_library[2048];
    sprintf(local_path_to_library, "./%s", DLL_NAME);

    thorlabs_tsi_camera_sdk_shared_library = dlopen(local_path_to_library, RTLD_LAZY);
    if (!thorlabs_tsi_camera_sdk_shared_library)
    {
        thorlabs_tsi_camera_sdk_shared_library = dlopen(DLL_NAME, RTLD_LAZY);
    }

    if (!thorlabs_tsi_camera_sdk_shared_library)
    {
        return (init_error_cleanup());
    }

    get_function_from_shared_library__or_nullptr = (GET_FUNCTION)(dlsym(thorlabs_tsi_camera_sdk_shared_library, (char*)"get_function_from_shared_library__or_nullptr"));
    if (!get_function_from_shared_library__or_nullptr)
    {
        return (init_error_cleanup());
    }
#endif

    tl_camera_set_frame_available_callback = (TL_CAMERA_SET_FRAME_AVAILABLE_CALLBACK)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_frame_available_callback");
    if (!tl_camera_set_frame_available_callback)
    {
        return init_error_cleanup();
    }

    tl_camera_set_camera_connect_callback = (TL_CAMERA_SET_CAMERA_CONNECT_CALLBACK)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_camera_connect_callback");
    if (!tl_camera_set_camera_connect_callback)
    {
        return init_error_cleanup();
    }

    tl_camera_set_camera_disconnect_callback = (TL_CAMERA_SET_CAMERA_DISCONNECT_CALLBACK)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_camera_disconnect_callback");
    if (!tl_camera_set_camera_disconnect_callback)
    {
        return init_error_cleanup();
    }

    tl_camera_open_sdk = (TL_CAMERA_OPEN_SDK)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_open_sdk");
    if (!tl_camera_open_sdk)
    {
        return init_error_cleanup();
    }

    tl_camera_close_sdk = (TL_CAMERA_CLOSE_SDK)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_close_sdk");
    if (!tl_camera_close_sdk)
    {
        return init_error_cleanup();
    }

    tl_camera_get_last_error = (TL_CAMERA_GET_LAST_ERROR)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_last_error");
    if (!tl_camera_get_last_error)
    {
        return init_error_cleanup();
    }

    tl_camera_discover_available_cameras = (TL_CAMERA_DISCOVER_AVAILABLE_CAMERAS)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_discover_available_cameras");
    if (!tl_camera_discover_available_cameras)
    {
        return init_error_cleanup();
    }

    _internal_command = (_INTERNAL_COMMAND)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "_internal_command");
    if (!_internal_command)
    {
        return init_error_cleanup();
    }

    tl_camera_get_exposure_time = (TL_CAMERA_GET_EXPOSURE_TIME)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_exposure_time");
    if (!tl_camera_get_exposure_time)
    {
        return init_error_cleanup();
    }

    tl_camera_set_exposure_time = (TL_CAMERA_SET_EXPOSURE_TIME)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_exposure_time");
    if (!tl_camera_set_exposure_time)
    {
        return init_error_cleanup();
    }

    tl_camera_get_image_poll_timeout = (TL_CAMERA_GET_IMAGE_POLL_TIMEOUT)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_image_poll_timeout");
    if (!tl_camera_get_image_poll_timeout)
    {
        return init_error_cleanup();
    }

    tl_camera_set_image_poll_timeout = (TL_CAMERA_SET_IMAGE_POLL_TIMEOUT)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_image_poll_timeout");
    if (!tl_camera_set_image_poll_timeout)
    {
        return init_error_cleanup();
    }

    tl_camera_get_pending_frame_or_null = (TL_CAMERA_GET_PENDING_FRAME_OR_NULL)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_pending_frame_or_null");
    if (!tl_camera_get_pending_frame_or_null)
    {
        return init_error_cleanup();
    }

    tl_camera_get_exposure_time_range = (TL_CAMERA_GET_EXPOSURE_TIME_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_exposure_time_range");
    if (!tl_camera_get_exposure_time_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_firmware_version = (TL_CAMERA_GET_FIRMWARE_VERSION)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_firmware_version");
    if (!tl_camera_get_firmware_version)
    {
        return init_error_cleanup();
    }

    tl_camera_get_frame_time = (TL_CAMERA_GET_FRAME_TIME)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_frame_time");
    if (!tl_camera_get_frame_time)
    {
        return init_error_cleanup();
    }

    tl_camera_get_measured_frame_rate = (TL_CAMERA_GET_MEASURED_FRAME_RATE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_measured_frame_rate");
    if (!tl_camera_get_measured_frame_rate)
    {
        return init_error_cleanup();
    }

    tl_camera_get_trigger_polarity = (TL_CAMERA_GET_TRIGGER_POLARITY)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_trigger_polarity");
    if (!tl_camera_get_trigger_polarity)
    {
        return init_error_cleanup();
    }

    tl_camera_set_trigger_polarity = (TL_CAMERA_SET_TRIGGER_POLARITY)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_trigger_polarity");
    if (!tl_camera_set_trigger_polarity)
    {
        return init_error_cleanup();
    }

    tl_camera_get_polar_phase = (TL_CAMERA_GET_POLAR_PHASE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_polar_phase");
    if (!tl_camera_get_polar_phase)
    {
        return init_error_cleanup();
    }

    tl_camera_get_binx = (TL_CAMERA_GET_BINX)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_binx");
    if (!tl_camera_get_binx)
    {
        return init_error_cleanup();
    }

    tl_camera_set_binx = (TL_CAMERA_SET_BINX)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_binx");
    if (!tl_camera_set_binx)
    {
        return init_error_cleanup();
    }

    tl_camera_get_binx_range = (TL_CAMERA_GET_BINX_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_binx_range");
    if (!tl_camera_get_binx_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_hot_pixel_correction_enabled = (TL_CAMERA_GET_IS_HOT_PIXEL_CORRECTION_ENABLED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_hot_pixel_correction_enabled");
    if (!tl_camera_get_is_hot_pixel_correction_enabled)
    {
        return init_error_cleanup();
    }

    tl_camera_set_is_hot_pixel_correction_enabled = (TL_CAMERA_SET_IS_HOT_PIXEL_CORRECTION_ENABLED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_is_hot_pixel_correction_enabled");
    if (!tl_camera_set_is_hot_pixel_correction_enabled)
    {
        return init_error_cleanup();
    }

    tl_camera_get_hot_pixel_correction_threshold = (TL_CAMERA_GET_HOT_PIXEL_CORRECTION_THRESHOLD)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_hot_pixel_correction_threshold");
    if (!tl_camera_get_hot_pixel_correction_threshold)
    {
        return init_error_cleanup();
    }

    tl_camera_set_hot_pixel_correction_threshold = (TL_CAMERA_SET_HOT_PIXEL_CORRECTION_THRESHOLD)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_hot_pixel_correction_threshold");
    if (!tl_camera_set_hot_pixel_correction_threshold)
    {
        return init_error_cleanup();
    }

    tl_camera_get_hot_pixel_correction_threshold_range = (TL_CAMERA_GET_HOT_PIXEL_CORRECTION_THRESHOLD_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_hot_pixel_correction_threshold_range");
    if (!tl_camera_get_hot_pixel_correction_threshold_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_sensor_width = (TL_CAMERA_GET_SENSOR_WIDTH)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_sensor_width");
    if (!tl_camera_get_sensor_width)
    {
        return init_error_cleanup();
    }

    tl_camera_get_gain_range = (TL_CAMERA_GET_GAIN_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_gain_range");
    if (!tl_camera_get_gain_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_sensor_height = (TL_CAMERA_GET_SENSOR_HEIGHT)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_sensor_height");
    if (!tl_camera_get_sensor_height)
    {
        return init_error_cleanup();
    }

    tl_camera_get_model = (TL_CAMERA_GET_MODEL)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_model");
    if (!tl_camera_get_model)
    {
        return init_error_cleanup();
    }

    tl_camera_get_model_string_length_range = (TL_CAMERA_GET_MODEL_STRING_LENGTH_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_model_string_length_range");
    if (!tl_camera_get_model_string_length_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_name = (TL_CAMERA_GET_NAME)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_name");
    if (!tl_camera_get_name)
    {
        return init_error_cleanup();
    }

    tl_camera_set_name = (TL_CAMERA_SET_NAME)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_name");
    if (!tl_camera_set_name)
    {
        return init_error_cleanup();
    }

    tl_camera_get_name_string_length_range = (TL_CAMERA_GET_NAME_STRING_LENGTH_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_name_string_length_range");
    if (!tl_camera_get_name_string_length_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_frames_per_trigger_zero_for_unlimited = (TL_CAMERA_GET_FRAMES_PER_TRIGGER_ZERO_FOR_UNLIMITED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_frames_per_trigger_zero_for_unlimited");
    if (!tl_camera_get_frames_per_trigger_zero_for_unlimited)
    {
        return init_error_cleanup();
    }

    tl_camera_set_frames_per_trigger_zero_for_unlimited = (TL_CAMERA_SET_FRAMES_PER_TRIGGER_ZERO_FOR_UNLIMITED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_frames_per_trigger_zero_for_unlimited");
    if (!tl_camera_set_frames_per_trigger_zero_for_unlimited)
    {
        return init_error_cleanup();
    }

    tl_camera_get_frames_per_trigger_range = (TL_CAMERA_GET_FRAMES_PER_TRIGGER_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_frames_per_trigger_range");
    if (!tl_camera_get_frames_per_trigger_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_camera_sensor_type = (TL_CAMERA_GET_CAMERA_SENSOR_TYPE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_camera_sensor_type");
    if (!tl_camera_get_camera_sensor_type)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_operation_mode_supported = (TL_CAMERA_GET_IS_OPERATION_MODE_SUPPORTED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_operation_mode_supported");
    if (!tl_camera_get_is_operation_mode_supported)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_eep_supported = (TL_CAMERA_GET_IS_EEP_SUPPORTED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_eep_supported");
    if (!tl_camera_get_is_eep_supported)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_led_supported = (TL_CAMERA_GET_IS_LED_SUPPORTED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_led_supported");
    if (!tl_camera_get_is_led_supported)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_data_rate_supported = (TL_CAMERA_GET_IS_DATA_RATE_SUPPORTED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_data_rate_supported");
    if (!tl_camera_get_is_data_rate_supported)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_cooling_supported = (TL_CAMERA_GET_IS_COOLING_SUPPORTED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_cooling_supported");
    if (!tl_camera_get_is_cooling_supported)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_taps_supported = (TL_CAMERA_GET_IS_TAPS_SUPPORTED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_taps_supported");
    if (!tl_camera_get_is_taps_supported)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_nir_boost_supported = (TL_CAMERA_GET_IS_NIR_BOOST_SUPPORTED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_nir_boost_supported");
    if (!tl_camera_get_is_nir_boost_supported)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_armed = (TL_CAMERA_GET_IS_ARMED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_armed");
    if (!tl_camera_get_is_armed)
    {
        return init_error_cleanup();
    }

    tl_camera_get_operation_mode = (TL_CAMERA_GET_OPERATION_MODE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_operation_mode");
    if (!tl_camera_get_operation_mode)
    {
        return init_error_cleanup();
    }

    tl_camera_set_operation_mode = (TL_CAMERA_SET_OPERATION_MODE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_operation_mode");
    if (!tl_camera_set_operation_mode)
    {
        return init_error_cleanup();
    }

    tl_camera_get_default_white_balance_matrix = (TL_CAMERA_GET_DEFAULT_WHITE_BALANCE_MATRIX)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_default_white_balance_matrix");
    if (!tl_camera_get_default_white_balance_matrix)
    {
        return init_error_cleanup();
    }

    tl_camera_get_color_filter_array_phase = (TL_CAMERA_GET_COLOR_FILTER_ARRAY_PHASE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_color_filter_array_phase");
    if (!tl_camera_get_color_filter_array_phase)
    {
        return init_error_cleanup();
    }

    tl_camera_get_color_correction_matrix = (TL_CAMERA_GET_COLOR_CORRECTION_MATRIX)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_color_correction_matrix");
    if (!tl_camera_get_color_correction_matrix)
    {
        return init_error_cleanup();
    }

    tl_camera_get_camera_color_correction_matrix_output_color_space = (TL_CAMERA_GET_CAMERA_COLOR_CORRECTION_MATRIX_OUTPUT_COLOR_SPACE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_camera_color_correction_matrix_output_color_space");
    if (!tl_camera_get_camera_color_correction_matrix_output_color_space)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_hot_pixel_correction_enabled = (TL_CAMERA_GET_IS_HOT_PIXEL_CORRECTION_ENABLED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_hot_pixel_correction_enabled");
    if (!tl_camera_get_is_hot_pixel_correction_enabled)
    {
        return init_error_cleanup();
    }

    tl_camera_set_is_hot_pixel_correction_enabled = (TL_CAMERA_SET_IS_HOT_PIXEL_CORRECTION_ENABLED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_is_hot_pixel_correction_enabled");
    if (!tl_camera_set_is_hot_pixel_correction_enabled)
    {
        return init_error_cleanup();
    }

    tl_camera_get_data_rate = (TL_CAMERA_GET_DATA_RATE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_data_rate");
    if (!tl_camera_get_data_rate)
    {
        return init_error_cleanup();
    }

    tl_camera_set_data_rate = (TL_CAMERA_SET_DATA_RATE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_data_rate");
    if (!tl_camera_set_data_rate)
    {
        return init_error_cleanup();
    }

    tl_camera_get_sensor_pixel_size_bytes = (TL_CAMERA_GET_SENSOR_PIXEL_SIZE_BYTES)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_sensor_pixel_size_bytes");
    if (!tl_camera_get_sensor_pixel_size_bytes)
    {
        return init_error_cleanup();
    }

    tl_camera_get_sensor_pixel_width = (TL_CAMERA_GET_SENSOR_PIXEL_WIDTH)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_sensor_pixel_width");
    if (!tl_camera_get_sensor_pixel_width)
    {
        return init_error_cleanup();
    }

    tl_camera_get_sensor_pixel_height = (TL_CAMERA_GET_SENSOR_PIXEL_HEIGHT)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_sensor_pixel_height");
    if (!tl_camera_get_sensor_pixel_height)
    {
        return init_error_cleanup();
    }

    tl_camera_get_bit_depth = (TL_CAMERA_GET_BIT_DEPTH)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_bit_depth");
    if (!tl_camera_get_bit_depth)
    {
        return init_error_cleanup();
    }

    tl_camera_get_roi = (TL_CAMERA_GET_ROI)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_roi");
    if (!tl_camera_get_roi)
    {
        return init_error_cleanup();
    }

    tl_camera_set_roi = (TL_CAMERA_SET_ROI)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_roi");
    if (!tl_camera_set_roi)
    {
        return init_error_cleanup();
    }

    tl_camera_get_roi_range = (TL_CAMERA_GET_ROI_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_roi_range");
    if (!tl_camera_get_roi_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_serial_number = (TL_CAMERA_GET_SERIAL_NUMBER)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_serial_number");
    if (!tl_camera_get_serial_number)
    {
        return init_error_cleanup();
    }

    tl_camera_get_serial_number_string_length_range = (TL_CAMERA_GET_SERIAL_NUMBER_STRING_LENGTH_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_serial_number_string_length_range");
    if (!tl_camera_get_serial_number_string_length_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_led_on = (TL_CAMERA_GET_IS_LED_ON)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_led_on");
    if (!tl_camera_get_is_led_on)
    {
        return init_error_cleanup();
    }

    tl_camera_set_is_led_on = (TL_CAMERA_SET_IS_LED_ON)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_is_led_on");
    if (!tl_camera_set_is_led_on)
    {
        return init_error_cleanup();
    }

    tl_camera_get_usb_port_type = (TL_CAMERA_GET_USB_PORT_TYPE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_usb_port_type");
    if (!tl_camera_get_usb_port_type)
    {
        return init_error_cleanup();
    }

    tl_camera_get_user_memory = (TL_CAMERA_GET_USER_MEMORY)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_user_memory");
    if (!tl_camera_get_user_memory)
    {
        return init_error_cleanup();
    }

    tl_camera_get_user_memory_maximum_size = (TL_CAMERA_GET_USER_MEMORY_MAXIMUM_SIZE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_user_memory_maximum_size");
    if (!tl_camera_get_user_memory_maximum_size)
    {
        return init_error_cleanup();
    }

    tl_camera_get_communication_interface = (TL_CAMERA_GET_COMMUNICATION_INTERFACE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_communication_interface");
    if (!tl_camera_get_communication_interface)
    {
        return init_error_cleanup();
    }

    tl_camera_get_eep_status = (TL_CAMERA_GET_EEP_STATUS)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_eep_status");
    if (!tl_camera_get_eep_status)
    {
        return init_error_cleanup();
    }

    tl_camera_set_is_eep_enabled = (TL_CAMERA_SET_IS_EEP_ENABLED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_is_eep_enabled");
    if (!tl_camera_set_is_eep_enabled)
    {
        return init_error_cleanup();
    }

    tl_camera_get_biny = (TL_CAMERA_GET_BINY)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_biny");
    if (!tl_camera_get_biny)
    {
        return init_error_cleanup();
    }

    tl_camera_set_biny = (TL_CAMERA_SET_BINY)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_biny");
    if (!tl_camera_set_biny)
    {
        return init_error_cleanup();
    }

    tl_camera_get_gain = (TL_CAMERA_GET_GAIN)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_gain");
    if (!tl_camera_get_gain)
    {
        return init_error_cleanup();
    }

    tl_camera_set_gain = (TL_CAMERA_SET_GAIN)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_gain");
    if (!tl_camera_set_gain)
    {
        return init_error_cleanup();
    }

    tl_camera_get_black_level = (TL_CAMERA_GET_BLACK_LEVEL)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_black_level");
    if (!tl_camera_get_black_level)
    {
        return init_error_cleanup();
    }

    tl_camera_set_black_level = (TL_CAMERA_SET_BLACK_LEVEL)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_black_level");
    if (!tl_camera_set_black_level)
    {
        return init_error_cleanup();
    }

    tl_camera_get_black_level_range = (TL_CAMERA_GET_BLACK_LEVEL_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_black_level_range");
    if (!tl_camera_get_black_level_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_biny_range = (TL_CAMERA_GET_BINY_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_biny_range");
    if (!tl_camera_get_biny_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_sensor_readout_time = (TL_CAMERA_GET_SENSOR_READOUT_TIME)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_sensor_readout_time");
    if (!tl_camera_get_sensor_readout_time)
    {
        return init_error_cleanup();
    }

    tl_camera_get_image_width = (TL_CAMERA_GET_IMAGE_WIDTH)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_image_width");
    if (!tl_camera_get_image_width)
    {
        return init_error_cleanup();
    }

    tl_camera_get_image_height = (TL_CAMERA_GET_IMAGE_HEIGHT)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_image_height");
    if (!tl_camera_get_image_height)
    {
        return init_error_cleanup();
    }

    tl_camera_get_image_width_range = (TL_CAMERA_GET_IMAGE_WIDTH_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_image_width_range");
    if (!tl_camera_get_image_width_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_image_height_range = (TL_CAMERA_GET_IMAGE_HEIGHT_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_image_height_range");
    if (!tl_camera_get_image_height_range)
    {
        return init_error_cleanup();
    }

    tl_camera_open_camera = (TL_CAMERA_OPEN_CAMERA)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_open_camera");
    if (!tl_camera_open_camera)
    {
        return init_error_cleanup();
    }

    tl_camera_close_camera = (TL_CAMERA_CLOSE_CAMERA)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_close_camera");
    if (!tl_camera_close_camera)
    {
        return init_error_cleanup();
    }

    tl_camera_arm = (TL_CAMERA_ARM)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_arm");
    if (!tl_camera_arm)
    {
        return init_error_cleanup();
    }

    tl_camera_issue_software_trigger = (TL_CAMERA_ISSUE_SOFTWARE_TRIGGER)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_issue_software_trigger");
    if (!tl_camera_issue_software_trigger)
    {
        return init_error_cleanup();
    }

    tl_camera_disarm = (TL_CAMERA_DISARM)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_disarm");
    if (!tl_camera_disarm)
    {
        return init_error_cleanup();
    }

    tl_camera_get_taps = (TL_CAMERA_GET_TAPS)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_taps");
    if (!tl_camera_get_taps)
    {
        return init_error_cleanup();
    }

    tl_camera_set_taps = (TL_CAMERA_SET_TAPS)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_taps");
    if (!tl_camera_set_taps)
    {
        return init_error_cleanup();
    }

    tl_camera_get_tap_balance_enable = (TL_CAMERA_GET_TAP_BALANCE_ENABLE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_tap_balance_enable");
    if (!tl_camera_get_tap_balance_enable)
    {
        return init_error_cleanup();
    }

    tl_camera_set_tap_balance_enable = (TL_CAMERA_SET_TAP_BALANCE_ENABLE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_tap_balance_enable");
    if (!tl_camera_set_tap_balance_enable)
    {
        return init_error_cleanup();
    }

    tl_camera_get_nir_boost_enable = (TL_CAMERA_GET_NIR_BOOST_ENABLE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_nir_boost_enable");
    if (!tl_camera_get_nir_boost_enable)
    {
        return init_error_cleanup();
    }

    tl_camera_set_nir_boost_enable = (TL_CAMERA_SET_NIR_BOOST_ENABLE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_nir_boost_enable");
    if (!tl_camera_set_nir_boost_enable)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_cooling_enabled = (TL_CAMERA_GET_IS_COOLING_ENABLED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_cooling_enabled");
    if (!tl_camera_get_is_cooling_enabled)
    {
        return init_error_cleanup();
    }

    tl_camera_get_frame_rate_control_value_range = (TL_CAMERA_GET_FRAME_RATE_CONTROL_VALUE_RANGE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_frame_rate_control_value_range");
    if (!tl_camera_get_frame_rate_control_value_range)
    {
        return init_error_cleanup();
    }

    tl_camera_get_is_frame_rate_control_enabled = (TL_CAMERA_GET_IS_FRAME_RATE_CONTROL_ENABLED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_is_frame_rate_control_enabled");
    if (!tl_camera_get_is_frame_rate_control_enabled)
    {
        return init_error_cleanup();
    }

    tl_camera_set_frame_rate_control_value = (TL_CAMERA_SET_FRAME_RATE_CONTROL_VALUE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_frame_rate_control_value");
    if (!tl_camera_set_frame_rate_control_value)
    {
        return init_error_cleanup();
    }

    tl_camera_set_is_frame_rate_control_enabled = (TL_CAMERA_SET_IS_FRAME_RATE_CONTROL_ENABLED)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_is_frame_rate_control_enabled");
    if (!tl_camera_set_is_frame_rate_control_enabled)
    {
        return init_error_cleanup();
    }

    tl_camera_get_frame_rate_control_value = (TL_CAMERA_GET_FRAME_RATE_CONTROL_VALUE)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_frame_rate_control_value");
    if (!tl_camera_get_frame_rate_control_value)
    {
        return init_error_cleanup();
    }

    tl_camera_get_timestamp_clock_frequency = (TL_CAMERA_GET_TIMESTAMP_CLOCK_FREQUENCY)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_get_timestamp_clock_frequency");
    if (!tl_camera_get_timestamp_clock_frequency)
    {
        return init_error_cleanup();
    }

    tl_camera_convert_gain_to_decibels = (TL_CAMERA_CONVERT_GAIN_TO_DECIBELS)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_convert_gain_to_decibels");
    if (!tl_camera_convert_gain_to_decibels)
    {
        return init_error_cleanup();
    }

    tl_camera_convert_decibels_to_gain = (TL_CAMERA_CONVERT_DECIBELS_TO_GAIN)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_convert_decibels_to_gain");
    if (!tl_camera_convert_decibels_to_gain)
    {
        return init_error_cleanup();
    }

    tl_camera_set_user_memory = (TL_CAMERA_SET_USER_MEMORY)get_function_from_shared_library__or_nullptr(thorlabs_tsi_camera_sdk_shared_library, "tl_camera_set_user_memory");
    if (!tl_camera_set_user_memory)
    {
        return init_error_cleanup();
    }

    return 0;
}

/// <summary>
///     Unloads the shared libraries and clears the function pointers.
/// </summary>
/// <returns>
///     1 for error, 0 for success
/// </returns>
int tl_camera_sdk_dll_terminate(void)
{
    set_camera_functions_to_null();

    get_function_from_shared_library__or_nullptr = 0;

    if (thorlabs_tsi_camera_sdk_shared_library != NULL)
    {
#ifdef _WIN32
        if (!FreeLibrary(thorlabs_tsi_camera_sdk_shared_library))
        {
            return 1;
        }
#else
        if (dlclose(thorlabs_tsi_camera_sdk_shared_library))
        {
            return 1;
        }
#endif
    }

    return 0;
}

#endif
