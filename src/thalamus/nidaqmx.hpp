#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <NIDAQmx.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {

  struct DAQmxAPI {
    decltype(&::DAQmxStartTask) DAQmxStartTask;
    decltype(&::DAQmxStopTask) DAQmxStopTask;
    decltype(&::DAQmxClearTask) DAQmxClearTask;
    decltype(&::DAQmxReadAnalogF64) DAQmxReadAnalogF64;
    decltype(&::DAQmxCreateTask) DAQmxCreateTask;
    decltype(&::DAQmxCreateAIVoltageChan) DAQmxCreateAIVoltageChan;
    decltype(&::DAQmxCreateAICurrentChan) DAQmxCreateAICurrentChan;
    decltype(&::DAQmxCfgSampClkTiming) DAQmxCfgSampClkTiming;
    decltype(&::DAQmxRegisterEveryNSamplesEvent) DAQmxRegisterEveryNSamplesEvent;
    decltype(&::DAQmxWriteDigitalLines) DAQmxWriteDigitalLines;
    decltype(&::DAQmxWriteAnalogScalarF64) DAQmxWriteAnalogScalarF64;
    decltype(&::DAQmxWriteAnalogF64) DAQmxWriteAnalogF64;
    decltype(&::DAQmxCreateDOChan) DAQmxCreateDOChan;
    decltype(&::DAQmxCreateAOVoltageChan) DAQmxCreateAOVoltageChan;
    decltype(&::DAQmxCreateAOCurrentChan) DAQmxCreateAOCurrentChan;
    decltype(&::DAQmxRegisterDoneEvent) DAQmxRegisterDoneEvent;
    decltype(&::DAQmxCfgDigEdgeStartTrig) DAQmxCfgDigEdgeStartTrig;
    decltype(&::DAQmxSetBufInputBufSize) DAQmxSetBufInputBufSize;
    decltype(&::DAQmxGetErrorString) DAQmxGetErrorString;
    decltype(&::DAQmxGetExtendedErrorInfo) DAQmxGetExtendedErrorInfo;
    decltype(&::DAQmxTaskControl) DAQmxTaskControl;
    decltype(&::DAQmxGetSysDevNames) DAQmxGetSysDevNames;
    decltype(&::DAQmxWriteDigitalU32) DAQmxWriteDigitalU32;
    decltype(&::DAQmxGetTaskNumDevices) DAQmxGetTaskNumDevices;
    decltype(&::DAQmxGetNthTaskDevice) DAQmxGetNthTaskDevice;
    decltype(&::DAQmxGetDevProductCategory) DAQmxGetDevProductCategory;
    decltype(&::DAQmxSetRefClkSrc) DAQmxSetRefClkSrc;
    decltype(&::DAQmxSetRefClkRate) DAQmxSetRefClkRate;
    decltype(&::DAQmxSetWriteRegenMode) DAQmxSetWriteRegenMode;

    static DAQmxAPI* get_singleton();
  private:
    DAQmxAPI() = default;
    DAQmxAPI(const DAQmxAPI&) = default;
  };
  
}
