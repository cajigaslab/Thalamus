//////////////////////////////////////////////////////////////////
/*!
  \file AOSystemAPI.h
  \brief Neuro Omega System SDK API (Ver.1)
*/
//////////////////////////////////////////////////////////////////

#ifndef __AO_SYSTEM_API_H__
#define __AO_SYSTEM_API_H__

#ifdef _WIN32
#ifdef __EXPORT_SYSTEM_SDK__
#define DECLDIR __declspec(dllexport)
#else
#define DECLDIR __declspec(dllimport)
#endif
#else
#define DECLDIR
#endif

#include "AOTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////////
// Helper structures

/*!
  \struct SInformation
  \brief Struct to hold channel information
*/
typedef struct information {
  int32 channelID;       /*!< The channel ID */
  cChar channelName[30]; /*!< The channel name */
} SInformation;

/*!
  \struct MAC_ADDR
  \brief Struct to hold network card MAC address
*/
typedef struct MAC_ADDR {
  int addr[6];
} MAC_ADDR;

/*!
  \enum ESignalTypes
  \brief Impedance check signal type
*/
typedef enum {
  eSin = 1 /*!< Sin wave */
} ESignalTypes;

/*!
  \enum EStimType
  \brief Analog input contact stimulation type
*/
typedef enum {
  eStimType_NoStim = 0,     /*!< No Stimulation */
  eStimType_OneSource = 1,  /*!< One Source Stimulation */
  eStimType_MultiSource = 2 /*!< Multiple Sources For Stimulation */
} EStimType;

/*!
  \fn typedef void (*AOParseFunction)(int16 *pData, int *dataSize)
  \brief  Function pointer type.
  \param  pData     [IN] data array.
  \param  pDataSize [IN] data array size.

  Callback function called when new data received from the system.
*/
typedef void (*AOParseFunction)(int16 *pData, int *pDataSize);

/*!
  \fn typedef void (*AODisplayError)(cChar* pMessage, int nMessage)
  \brief  Function pointer type.
  \param  pMessage [IN] NULL terminated string.
  \param  nMessage [IN] pMessage length.

  Callback function to get feedback messages from the system.
*/
typedef void (*AODisplayError)(cChar *pMessage, int nMessage);

//////////////////////////////////////////////////////////////////
// System API Functions

/*!
  \fn int GetDllVersion()
  \brief  Get System SDK DLL Version.
  \return System SDK DLL Version.
*/
DECLDIR int GetDllVersion();

/*!
  \fn int SetCallBackDisplayError(AODisplayError callback)
  \brief  Set call back function for error display.
  \param  callback [IN] defined by third party according to AODisplayError type.
  \return eAO_OK    Success.
  \return otherwise Fail.

  Callback function to get back error messages.
*/
DECLDIR int SetCallBackDisplayError(AODisplayError callback);

/*!
  \fn int ErrorHandlingfunc(int* pErrorCount, cChar* sError, int nError)
  \brief  Get total errors count and the last error occur.
  \param  pErrorCount  [OUT] Total errors count since app start running.
  \param  sError       [OUT] The last error occur.
  \param  nError       [IN]  sError length.
  \return eAO_OK       Success.
  \return eAO_ARG_NULL One or more of the input arguments is NULL.
  \return eAO_BAD_ARG  if nError <= 0.
*/
DECLDIR int ErrorHandlingfunc(int *pErrorCount, cChar *sError, int nError);

/*!
  \fn int StartConnection(MAC_ADDR* pSystemMAC, MAC_ADDR* pUserMAC, int
  nAdapterIndex, AOParseFunction callback) \brief  Establish connection to
  system. \param  pSystemMAC    [IN] System MAC Address, i.e.
  "00:21:BA:07:AB:9E". \param  pUserMAC      [IN] User (caller) artificial MAC
  Address, i.e. "11:1C:7E:6C:7F:BE". \param  nAdapterIndex [IN] User PC network
  adapter card index that is connected to the system switch. -1 for auto search.
  \param  callback      [IN] See AOParseFunction. This input parameter is
  OBSOLETE and should be NULL. \return eAO_OK        Success. \return
  eAO_ARG_NULL  One or more of the input arguments is NULL. \return eAO_BAD_ARG
  if nAdapterIndex < 0 and not equal to -1. \return eAO_ALREADY_CONNECTED if
  connection already in progress. \return eAO_FAIL      if fail to establish
  connection. use ErrorHandlingfunc to get more info.

  Establish connection to the system.
*/
DECLDIR int StartConnection(MAC_ADDR *pSystemMAC, MAC_ADDR *pUserMAC,
                            int nAdapterIndex, AOParseFunction callback = NULL);

/*!
  \fn int DefaultStartConnection(MAC_ADDR* pSystemMAC, AOParseFunction callback)
  \brief  Establish connection to system.
  \param  pSystemMAC    [IN] System MAC Address, i.e. "00:21:BA:07:AB:9E".
  \param  callback      [IN] See AOParseFunction. This input parameter is
  OBSOLETE and should be NULL. \return eAO_OK        Success. \return
  eAO_ARG_NULL  One or more of the input arguments is NULL. \return
  eAO_ALREADY_CONNECTED if connection already in progress. \return eAO_FAIL if
  fail to establish connection. use ErrorHandlingfunc to get more info.

  Establish connection to the system.
  Auto search for user PC network adapter card index that is connected to the
  system switch. Sets user MAC address to 'AA:BB:CC:DD:EE:FF'.
*/
DECLDIR int DefaultStartConnection(MAC_ADDR *pSystemMAC,
                                   AOParseFunction callback = NULL);

/*!
  \fn int CheckQualityConnection(int* pQualityType, real32* pQualityPercent)
  \brief  Check system connection quality.
  \param  pQualityType      [OUT] 1 - Poor connection, 2 - Medium connection, 3
  - High connection \param  pQualityPercent   [OUT] System throughput in
  percent. \return eAO_OK            Success. \return eAO_ARG_NULL      One or
  more of the input arguments is NULL. \return eAO_NOT_CONNECTED if the system
  is not connected.
*/
DECLDIR int CheckQualityConnection(int *pQualityType, real32 *pQualityPercent);

/*!
  \fn int isConnected()
  \brief  Check system connection status.
  \return eAO_DISCONNECTED  The SDK is disconnected from the system.
  \return eAO_CONNECTED     The SDK is connected to the system.
  \return eAO_CONNECTING    The SDK is trying to connect to the system.

  See EAOConnection
*/
DECLDIR int isConnected();

/*!
  \fn int CloseConnection()
  \brief  Disconnect system connection.
  \return eAO_OK Success.
*/
DECLDIR int CloseConnection();

/*!
  \fn int GetLatestTimeStamp(ULONG *pLastTS)
  \brief  Get the system timestamp.
  \param  pLastTS           [OUT] The system timestamp.
  \return eAO_OK            Success.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int GetLatestTimeStamp(ULONG *pLastTS);

/*!
  \fn int SetSavePath(cChar* sSavePath, int nSavePath)
  \brief  Change mpx files saving directory.
  \param  sSavePath [IN] mpx files new saving directory path, i.e.
  "C:\\Surgeries_Data\\Patient_X\\". \param  nSavePath [IN] sSavePath length. No
  more than 100 characters, not including the end of string character. \return
  eAO_OK            Success. i.e. Request sent successfuly. \return eAO_ARG_NULL
  One or more of the input arguments is NULL. \return eAO_BAD_ARG       if
  nSavePath < 0 or nSavePath > 100. \return eAO_BAD_ARG       if sSavePath
  length not equal to nSavePath. \return eAO_NOT_CONNECTED if the system is not
  connected.

  Path must exist on the saving PC.
  Note: Success does not guarantee that the Neuro Omega UI changed the path.
  Request might fail.
*/
DECLDIR int SetSavePath(cChar *sSavePath, int nSavePath);

/*!
  \fn int SetSaveFileName(cChar* sFileName, int nFileName)
  \brief  Change mpx files saving base name.
  \param  sFileName [IN] mpx files new saving base name, i.e.
  "ExpirementName.mpx". \param  nFileName [IN] nFileName length. No more than 40
  characters, not including the end of string character. \return eAO_OK Success.
  i.e. Request sent successfuly. \return eAO_ARG_NULL      One or more of the
  input arguments is NULL. \return eAO_BAD_ARG       if nFileName <= 0 or
  nFileName > 40. \return eAO_BAD_ARG       if sFileName length not equal to
  nFileName. \return eAO_NOT_CONNECTED if the system is not connected.

  if a file exists with the same name in the saving directory, it might be
  overriden. Note: Success does not guarantee that the Neuro Omega UI changed
  the mpx files base name. Request might fail.
*/
DECLDIR int SetSaveFileName(cChar *sFileName, int nFileName);

/*!
  \fn int StartSave()
  \brief  Start saving mpx file on the saving PC.
  \return eAO_OK            Success. i.e. Request sent successfuly.
  \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int StartSave();

/*!
  \fn int StartSaveTS(uint32 uStartSaveTS)
  \brief  Start saving mpx file on the saving PC after data timestamp
  uStartSaveTS. \param  uStartSaveTS [IN] Start saving timestamp according to
  system sampling rate. \return eAO_OK            Success. i.e. Request sent
  successfuly. \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int StartSaveTS(uint32 uStartSaveTS);

/*!
  \fn int StopSave()
  \brief  Stop saving mpx file on the saving PC, if saving in progress...
  \return eAO_OK            Success. i.e. Request sent successfuly.
  \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int StopSave();

/*!
  \fn int StopSaveTS(uint32 uStopSaveTS)
  \brief  Stop saving mpx file on the saving PC after data timestamp
  uStopSaveTS, if saving in progress... \param  uStopSaveTS [IN] Stop saving
  timestamp according to system sampling rate. \return eAO_OK Success. i.e.
  Request sent successfuly. \return eAO_NOT_CONNECTED if the system is not
  connected.
*/
DECLDIR int StopSaveTS(uint32 uStopSaveTS);

/*!
  \fn int SendText(cChar* sText, int nText)
  \brief  Save text to mpx file while saving in progress.
  \param  sText [IN] The text to save, i.e. "Expirement Start".
  \param  nText [IN] nText length. No more than 100 characters, not including
  the end of string character. \return eAO_OK            Success. i.e. Request
  sent successfuly. \return eAO_ARG_NULL      One or more of the input arguments
  is NULL. \return eAO_BAD_ARG       if nText <= 0 or nText > 100. \return
  eAO_BAD_ARG       if sText length not equal to nText. \return
  eAO_NOT_CONNECTED if the system is not connected.

  The text event timestamped with the latest system timestamp.
*/
DECLDIR int SendText(cChar *sText, int nText);

/*!
  \fn int GetChannelsCount(uint32* pChannelsCount)
  \brief  Get the number of defined channels.
  \param  pChannelsCount [OUT] The number of defined channels.
  \return eAO_OK            Success.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int GetChannelsCount(uint32 *pChannelsCount);

/*!
  \fn int GetAllChannels(SInformation* pChannelsInfo, int32 nChannelsInfo)
  \brief  Get channel information for each defined channel.
  \param  pChannelsInfo [OUT] The channels information buffer. Allocated by the
  caller. \param  nChannelsInfo [IN ] The channels information buffer size.
  \return eAO_OK            Success.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_BAD_ARG       if nChannelsInfo smaller than channels count.
  \return eAO_NOT_CONNECTED if the system is not connected.

  Should allocate pChannelsInfo with size according to GetChannelsCount() result
  to get all channels information.
*/
DECLDIR int GetAllChannels(SInformation *pChannelsInfo, int32 nChannelsInfo);

/*!
  \fn int TranslateNameToID(cChar* sChannelName, int nName, int* pChannelID)
  \brief  Get Channel ID according to Channel Name.
  \param  sChannelName [IN]  The requested channel name, i.e. "SPK 01".
  \param  nChannelName [IN]  sChannelName length.
  \param  pChannelID   [Out] The channel ID of the channel sChannelName.
  \return eAO_OK            Success.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_BAD_ARG       if nChannelName <= 0.
  \return eAO_BAD_ARG       if sChannelName length not equal to nChannelName.
  \return eAO_CHANNEL_NOT_EXIST if no channel found with the given name
  sChannelName. \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int TranslateNameToID(cChar *sChannelName, int nChannelName,
                              int *pChannelID);

/*!
  \fn int SetChannelName(int nChannelID, cChar* sNewName, int nNewName)
  \brief  Change channel name for the channel with ID nChannelID.
  \param  nChannelID [IN] The requested channel ID.
  \param  sNewName   [IN] The requested channel new name, i.e. "SPK 01".
  \param  nNewName   [IN] sNewName length. No more than 29 characters, not
  including the end of string character. \return eAO_OK            Success. i.e.
  Request sent successfuly. \return eAO_ARG_NULL      One or more of the input
  arguments is NULL. \return eAO_BAD_ARG       if nNewName <= 0 || nNewName
  > 29. \return eAO_BAD_ARG       if sNewName length not equal to nNewName.
  \return eAO_CHANNEL_NOT_EXIST  if no channel found with the given ID
  nChannelID. \return eAO_CHANNEL_NAME_EXIST if there is another channel that
  have the same name sNewName. \return eAO_NOT_CONNECTED if the system is not
  connected.
*/
DECLDIR int SetChannelName(int nChannelID, cChar *sNewName, int nNewName);

/*!
  \fn int GetChannelSaveState(int nChannelID, BOOL* pSave)
  \brief  Get channel saving state.
  \param  nChannelID [IN]  The requested channel ID.
  \param  pSave      [OUT] Save status. TRUE - channel saved to mpx file when
  saving in progress, otherwise not saved. \return eAO_OK            Success.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_CHANNEL_NOT_EXIST if no channel found with the given ID
  nChannelID. \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int GetChannelSaveState(int nChannelID, BOOL *pSave);

/*!
  \fn int SetChannelSaveState(int nChannelID, BOOL bSave)
  \brief  Set channel saving state.
  \param  nChannelID [IN] The requested channel ID.
  \param  bSave      [IN] Save status. TRUE - channel saved to mpx file when
  saving in progress, otherwise not saved. \return eAO_OK            Success.
  \return eAO_CHANNEL_NOT_EXIST if no channel found with the given ID
  nChannelID. \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int SetChannelSaveState(int nChannelID, BOOL bSave);

/*!
  \fn int GetCutOffFC(int nChannelID, real32 *pLP_FC, real32 *pHP_FC)
  \brief  Get contact analog input channel High Pass / Low Pass frequency
  cutoff. \param  nChannelID [IN]  The requested channel ID. \param  pLP_FC
  [OUT] Channel Low Pass frequency cutoff. \param  pHP_FC     [OUT] Channel High
  Pass frequency cutoff. \return eAO_OK            Success. \return eAO_ARG_NULL
  One or more of the input arguments is NULL. \return eAO_BAD_ARG       The
  given channel is not Analog Input channel. \return eAO_CHANNEL_NOT_EXIST if no
  channel found with the given ID nChannelID. \return eAO_NOT_CONNECTED if the
  system is not connected.
*/
DECLDIR int GetCutOffFC(int nChannelID, real32 *pLP_FC, real32 *pHP_FC);

/*!
  \fn int SetChannelThreshold(int nChannelID, int nThreshold_uV, int nDirection)
  \brief  Change SEGMENTED channel level line threshold and direction.
  \param  nChannelID    [IN] The requested SEGMENTED channel ID.
  \param  nThreshold_uV [IN] Level line new threshold in Micro Volts.
  \param  nDirection    [IN] Level line new direction. 1 - Down, 2 - Up. See
  EAOLevelLineDirection. \return eAO_OK            Success. i.e. Request sent
  successfuly. \return eAO_BAD_ARG       if nDirection different than 1 - Down
  and 2 - Up. See EAOLevelLineDirection. \return eAO_BAD_ARG       if
  nThreshold_uV out of range. \return eAO_BAD_ARG       if the channel is not
  segmented. \return eAO_CHANNEL_NOT_EXIST if no channel found with the given ID
  nChannelID. \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int SetChannelThreshold(int nChannelID, int nThreshold_uV,
                                int nDirection);

/*!
  \fn int AddBufferChannel(int nChannelID, int nBufferingTime_mSec)
  \brief  Request to buffer (store) specific channel data.
  \param  nChannelID          [IN] The requested channel ID, i.e. 10000, 10001
  ... \param  nBufferingTime_mSec [IN] Channel data acquisition window in mSec.
  \return eAO_OK            Success.
  \return eAO_BAD_ARG       if nBufferingTime_mSec not in range [5000..20000]
  mSec. \return eAO_CHANNEL_NOT_EXIST if no channel found with the given ID
  nChannelID. \return eAO_NOT_CONNECTED if the system is not connected.

  Start data acquisition to channel nChannelID.
  Buffer the last nBufferingTime_mSec data.
*/
DECLDIR int AddBufferChannel(int nChannelID, int nBufferingTime_mSec);

/*!
  \fn int ClearBuffers()
  \brief  Reset acquisition for all buffered channels.
  \return eAO_OK Success.

  Delete the buffered channels CURRENT buffered data.
  Note: The SDK will keep buffering new data for the buffered channels.
*/
DECLDIR int ClearBuffers();

/*!
  \fn int GetChannelData(int nChannelID, int16* pData, int nData, int*
  pDataCapture) \brief  Get channel data. \param  nChannelID   [IN]  The
  requested channel ID. \param  pData        [OUT] Array to fill the returned
  channel data. \param  nData        [IN]  pData array size. \param pDataCapture
  [OUT] The actual returned channel data size in pData. \return eAO_OK Success.
  \return eAO_FAIL          if the given channel not buffered. i.e.
  AddBufferChannel was not called for the given channel. \return eAO_ARG_NULL
  One or more of the input arguments is NULL. \return eAO_BAD_ARG       if nData
  <= 0. \return eAO_CHANNEL_NOT_EXIST if no channel found with the given ID
  nChannelID. \return eAO_NOT_CONNECTED if the system is not connected.

  Note: Must call AddBufferChannel ONE TIME for each channel desired to get its
  data. Note: It is forbidden to call GetChannelData and GetAlignedData from
  different threads in parallel.

  Please advice the SDK Manual for extra information.
*/
DECLDIR int GetChannelData(int nChannelID, int16 *pData, int nData,
                           int *pDataCapture);

/*!
  \fn int GetAlignedData(int16* pData, int nData, int* pDataCapture, int*
  pChannels, int nChannels, ULONG* pBeginTS) \brief  Get multiple channels
  samples starting from same timestamp. \param  pData        [OUT] The samples
  buffer. Allocated by the caller. \param  nData        [IN ] The samples buffer
  size. \param  pDataCapture [OUT] The actual number of samples filled in
  pSamples. \param  pChannels    [IN ] Requested channels IDs array. \param
  nChannels    [IN ] Requested channels IDs array size. \param  pBeginTS [OUT]
  The first sample timestamp. Common for all channels samples buffer. \return
  eAO_OK            Success. \return eAO_FAIL          if one of the given
  channels not buffered. i.e. AddBufferChannel was not called for the channel.
  \return eAO_FAIL          if one of the given channel buffered but has no
  data. \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_BAD_ARG       if nData <= 0 || nChannels <= 0.
  \return eAO_BAD_ARG       if nData < nChannels.
  \return eAO_BAD_ARG       if one of the given channels not analog input
  continuous. i.e. SPK/LFP/RAW... \return eAO_BAD_ARG       if not all the given
  channels have the same sampling rate. \return eAO_CHANNEL_NOT_EXIST if one of
  the given channels not found. \return eAO_NOT_CONNECTED if the system is not
  connected.

  The result in pData is as follow:
  The samples count for each channel is (*pDataCapture/nChannels).
  For the channel at index i the samples in pData starts from index
  (*pDataCapture/nChannels)*i. i in range [0..nChannels-1]

  Note: Must call AddBufferChannel ONE TIME for each channel desired to get its
  data. Note: It is forbidden to call GetChannelData and GetAlignedData from
  different threads in parallel.

  Please advice the SDK Manual for extra information.
*/
DECLDIR int GetAlignedData(int16 *pData, int nData, int *pDataCapture,
                           int *pChannels, int nChannels, ULONG *pBeginTS);

/*!
  \fn int ListenToDigitalChannel(int nDigitalChannelID, int nMask, int nTime_mS)
  \brief  Block application until getting data for the given digital channel.
  \param  nDigitalChannelID [IN] The requested digital channel ID.
  \param  nMask             [IN] The desired relevant bits. Only first 8 bits
  used. \param  nTime_mS          [IN] The time to wait for the given digital
  channel. -1 - Infinit. \return eAO_OK            Success. i.e. digital channel
  nDigitalChannelID received. \return eAO_FAIL          if stop listen before
  digital channel nDigitalChannelID received. \return eAO_BAD_ARG       if
  nDigitalChannelID not digital input/output. \return eAO_BAD_ARG       if nMask
  use bytes other than the first byte. \return eAO_CHANNEL_NOT_EXIST if no
  channel found with the given ID nDigitalChannelID \return eAO_NOT_CONNECTED if
  the system is not connected.
*/
DECLDIR int ListenToDigitalChannel(int nDigitalChannelID, int nMask,
                                   int nTime_mS);

/*!
  \fn int GetStimType(int nContactID, int* pStimType)
  \brief  Get analog input contact stimulation type.
  \param  nContactID [IN]  The requested contact ID.
  \param  pStimType  [OUT] See EStimType.
  \return eAO_OK            Success.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_CONTACT_NOT_EXIST if no contact found with the given ID
  nContactID. \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int GetStimType(int nContactID, int *pStimType);

/*!
  \fn int SetStimulationParameters(real32 fFirstPhaseAmp_mA, real32
  fFirstPhaseWidth_mS, real32 fSecondPhaseAmp_mA, real32 fSecondPhaseWidth_mS,
  int Freq_Hz,\ real32 fDuration_Sec, int nReturnChannel, int nContactID, real32
  fFirstPhaseDelay_mS=0, real32 fSecondPhaseDelay_mS=0); \brief  Set contact
  digital stimulation parameters, AKA Square Wave. \param  fFirstPhaseAmp_mA
  [IN] Square wave first phase amplitude in Milliamps. \param
  fFirstPhaseWidth_mS  [IN] Square wave first phase width in Milliseconds.
  \param  fSecondPhaseAmp_mA   [IN] Square wave second phase amplitude in
  Milliamps. \param  fSecondPhaseWidth_mS [IN] Square wave second phase width in
  Milliseconds. \param  nFreq_Hz             [IN] Square wave frequency in Hz.
  \param  fDuration_Sec        [IN] Square wave duration in Seconds.
  \param  nReturnChannel       [IN] The stimulation common return channel. -1
  for Global Return. \param  nContactID           [IN] The contact to apply the
  square wave stimulation parameters. \param  fFirstPhaseDelay_mS  [IN] Square
  wave first phase delay in Milliseconds. \param  fSecondPhaseDelay_mS [IN]
  Square wave second phase delay in Milliseconds. \return eAO_OK Success. i.e.
  Request sent successfuly. \return eAO_FAIL          if the total pulse width
  and the pulse frequency does not match. i.e. (Pulse max frequency) < nFreq_Hz.
  \return eAO_BAD_ARG       abs(fFirstPhaseAmp_mA)  > the given contact max
  stimulation amplitude. \return eAO_BAD_ARG       abs(fSecondPhaseAmp_mA) > the
  given contact max stimulation amplitude. \return eAO_BAD_ARG
  fFirstPhaseWidth_mS  < 0 or fFirstPhaseWidth_mS  > 0.5. \return eAO_BAD_ARG
  fSecondPhaseWidth_mS < 0 or fSecondPhaseWidth_mS > 0.5. \return eAO_BAD_ARG
  nFreq_Hz < 1 or nFreq_Hz > 300. \return eAO_BAD_ARG       fDuration_Sec <
  0.005 or fDuration_Sec > 30000. \return eAO_BAD_ARG       fFirstPhaseDelay_mS
  < 0. \return eAO_BAD_ARG       fSecondPhaseDelay_mS < 0 or
  fSecondPhaseDelay_mS > 1. \return eAO_BAD_ARG           if Stim. Contact and
  Return channel contact are the same. \return eAO_NO_STIMULATION    if the
  given contact stim type is eStimType_NoStim. \return eAO_CONTACT_NOT_EXIST if
  no contact found with the given ID nContactID. \return eAO_CONTACT_NOT_EXIST
  if no contact found with the given ID nReturnChannel, in case nReturnChannel
  not Global Return. \return eAO_NOT_CONNECTED     if the system is not
  connected.

  Please advice the SDK Manual for extra information.
*/
DECLDIR int
SetStimulationParameters(real32 fFirstPhaseAmp_mA, real32 fFirstPhaseWidth_mS,
                         real32 fSecondPhaseAmp_mA, real32 fSecondPhaseWidth_mS,
                         int nFreq_Hz, real32 fDuration_Sec, int nReturnChannel,
                         int nContactID, real32 fFirstPhaseDelay_mS = 0,
                         real32 fSecondPhaseDelay_mS = 0);

/*!
  \fn int StartStimulation(int nContactID)
  \brief  Request to start stimulation on specific analog input contact.
  \param  nContactID [IN]  The requested analog input contact ID.
  \return eAO_OK                Success. i.e. Request sent successfuly.
  \return eAO_NO_STIMULATION    if the given contact stim type is
  eStimType_NoStim. \return eAO_CONTACT_NOT_EXIST if no contact found with the
  given ID nContactID. \return eAO_NOT_CONNECTED     if the system is not
  connected.

  On success, the system will stimulate on the specific contact according to
  it's last set stimulation parameters.
*/
DECLDIR int StartStimulation(int nContactID);

/*!
  \fn int StopStimulation(int nContactID)
  \brief  Request to stop stimulation on specific analog input contact.
  \param  nContactID [IN] The requested analog input contact ID. -1 if request
  to stop all current stimulating contacts. \return eAO_OK Success. i.e. Request
  sent successfuly. \return eAO_NO_STIMULATION    if the given contact stim type
  is eStimType_NoStim. \return eAO_CONTACT_NOT_EXIST if no contact found with
  the given ID nContactID. \return eAO_NOT_CONNECTED     if the system is not
  connected.

  On success, the system will stop stimulate on the specific contact.
*/
DECLDIR int StopStimulation(int nContactID);

/*!
  \fn int LoadWaveToEmbedded(int16* pSamples, int nSamples, int
  nDownSampleFactor, cChar* sWaveName) \brief  Set new analog stimulation wave.
  \param  pSamples          [IN] Analog stimulation wave samples array. Sample
  in Micro Amp unit. \param  nSamples          [IN] pSamples array size. No more
  than 8 Million. \param  nDownSampleFactor [IN] Define wave samples sampling
  rate: (System Sampling Rate)/(Down Sample Factor). \param  sWaveName [IN]
  Analog stimulation wave name, i.e. "Stim Wave 1". No more than 10 characters
  including the end of string character. \return eAO_OK            Success. i.e.
  Request sent successfuly. \return eAO_ARG_NULL      One or more of the input
  arguments is NULL. \return eAO_BAD_ARG       if nSamples <= 0 or nSamples >
  8000000. \return eAO_BAD_ARG       if Down Sample Factor not one of:
  [1,2,4,8,16]. \return eAO_BAD_ARG       if Wave name length bigger than 10
  characters, including the end of string character. \return eAO_BAD_ARG if Wave
  name empty. \return eAO_BAD_ARG       if Wave name illegal. Should have only
  digits and letters. \return eAO_NOT_CONNECTED if the system is not connected.

  Please advice the SDK Manual for extra information.
*/
DECLDIR int LoadWaveToEmbedded(int16 *pSamples, int nSamples,
                               int nDownSampleFactor, cChar *sWaveName);

/*!
  \fn int StartAnalogStimulation(int nContactID, int nWaveId, int nFreq_Hz,
  real32 fDuration_Sec, int nReturnChannel) \brief  Request to start stimulation
  on specific analog input contact. \param  nContactID          [IN] The contact
  to apply on the analog stimulation wave. \param  nWaveId             [IN] The
  analog stimulation wave ID. \param  nFreq_Hz            [IN] The analog
  stimulation wave frequency in Hz. \param  fDuration_Sec       [IN] The analog
  stimulation wave duration in Seconds. \param  nReturnChannel      [IN] The
  stimulation common return channel. \return eAO_OK                Success. i.e.
  Request sent successfuly. \return eAO_BAD_ARG           if nFreq_Hz != -1
  (Repeat without delay) and nFreq_Hz < 1 or nFreq_Hz > 300. \return eAO_BAD_ARG
  if fDuration_Sec < 0.005 or fDuration_Sec > 30000. \return eAO_BAD_ARG if
  Stim. Contact and Return channel contact are the same. \return
  eAO_NO_STIMULATION    if the given contact stim type is eStimType_NoStim.
  \return eAO_CONTACT_NOT_EXIST if no contact found with the given ID
  nContactID. \return eAO_CONTACT_NOT_EXIST if no contact found with the given
  ID nReturnChannel, in case nReturnChannel not Global Return. \return
  eAO_NOT_CONNECTED     if the system is not connected.

  Please advice the SDK Manual for extra information.
*/
DECLDIR int StartAnalogStimulation(int nContactID, int nWaveId, int nFreq_Hz,
                                   real32 fDuration_Sec, int nReturnChannel);

/*!
  \fn int StartDigitalStimulation(int nContactID, real32 fFirstPhaseDelay_mS,
  real32 fFirstPhaseAmp_mA, real32 fFirstPhaseWidth_mS,\
                                                        real32
  fSecondPhaseDelay_mS, real32 fSecondPhaseAmp_mA, real32 fSecondPhaseWidth_mS,
  int nFreq_Hz,\ real32 fDuration_Sec, int nReturnChannel) \brief  Request to
  start stimulation on specific analog input contact. \param  nContactID [IN]
  The contact to apply the square wave stimulation parameters. \param
  fFirstPhaseDelay_mS  [IN] Square wave first phase delay in Milliseconds.
  \param  fFirstPhaseAmp_mA    [IN] Square wave first phase amplitude in
  Milliamps. \param  fFirstPhaseWidth_mS  [IN] Square wave first phase width in
  Milliseconds. \param  fSecondPhaseDelay_mS [IN] Square wave second phase delay
  in Milliseconds. \param  fSecondPhaseAmp_mA   [IN] Square wave second phase
  amplitude in Milliamps. \param  fSecondPhaseWidth_mS [IN] Square wave second
  phase width in Milliseconds. \param  nFreq_Hz             [IN] Square wave
  frequency in Hz. \param  fDuration_Sec        [IN] Square wave duration in
  Seconds. \param  nReturnChannel       [IN] The stimulation common return
  channel.

  If succeed to set the given square wave stimulation parameters for the given
  contact, the system will start stimulate the given square wave on the
  requested stim contact.

  See SetStimulationParameters for return values.
  Please advice the SDK Manual for extra information.
*/
DECLDIR int StartDigitalStimulation(int nContactID, real32 fFirstPhaseDelay_mS,
                                    real32 fFirstPhaseAmp_mA,
                                    real32 fFirstPhaseWidth_mS,
                                    real32 fSecondPhaseDelay_mS,
                                    real32 fSecondPhaseAmp_mA,
                                    real32 fSecondPhaseWidth_mS, int nFreq_Hz,
                                    real32 fDuration_Sec, int nReturnChannel);

/*!
  \fn int SendDout(int nMask, int nValue)
  \brief  Generate Digital output on DIG-OUT sockets.
  \param  nMask  [IN] Bitwise mask. Only the first byte used.
  \param  nValue [IN] Bitwise value. Only the first byte used.
  \return eAO_OK                Success. i.e. Request sent successfuly.
  \return eAO_BAD_ARG           if nMask use bytes other than the first byte.
  \return eAO_BAD_ARG           if nValue use bytes other than the first byte.
  \return eAO_CHANNEL_NOT_EXIST if DIG-OUT socket not exist.
  \return eAO_NOT_CONNECTED     if the system is not connected.

  To generate digital output on DIG-OUT socket i (in range [0..7]):
  nMask  bit i should be set.
  nValue bit i should be set/clear.
*/
DECLDIR int SendDout(int nMask, int nValue);

/*!
  \fn int SendDigitalData(int nDigitalChannelID, int nMask, int nValue)
  \brief  Generate Digital output on digital output socket.
  \param  nDigitalChannelID [IN] The digital channel ID. Please advice the SDK
  Manual appendix. \param  nMask             [IN] Bitwise mask. Only the first
  byte used. \param  nValue            [IN] Bitwise value. Only the first byte
  used. \return eAO_OK                Success. i.e. Request sent successfuly.
  \return eAO_BAD_ARG           if nDigitalChannelID not digital output channel.
  \return eAO_BAD_ARG           if nMask use bytes other than the first byte.
  \return eAO_BAD_ARG           if nValue use bytes other than the first byte.
  \return eAO_CHANNEL_NOT_EXIST if no channel found with the given ID
  nDigitalChannelID. \return eAO_NOT_CONNECTED     if the system is not
  connected.

  To generate digital output on nDigitalChannelID bit i (in range [0..7]):
  nMask  bit i should be set.
  nValue bit i should be set/clear.
*/
DECLDIR int SendDigitalData(int nDigitalChannelID, int nMask, int nValue);

/*!
  \fn int GetDriveDepth(int* pDepth)
  \brief  Get the current drive depth.
  \param  pDepth [OUT] The current drive depth in uM.
  \return eAO_OK            Success.
  \return eAO_FAIL          if the drive not connected.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int GetDriveDepth(int32 *pDepth);

/*!
  \fn int GetMoveMotorTS(uint32* pMotorMoveTS)
  \brief  Get the system timestamp when the drive start moving.
  \param  pMotorMoveTS [OUT] The system timestamp when the drive start moving.
  \return eAO_OK            Success.
  \return eAO_FAIL          if the drive not connected.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int GetMoveMotorTS(uint32 *pMotorMoveTS);

/*!
  \fn int GetStopMotorTS(uint32* pMotorStopTS)
  \brief  Get the system timestamp when the drive stopped after the last drive
  movement. \param  pMotorStopTS [OUT] The system timestamp when the drive
  stopped after the last drive movement. \return eAO_OK            Success.
  \return eAO_FAIL          if the drive not connected.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int GetStopMotorTS(uint32 *pMotorStopTS);

/*!
  \fn void GetNextBlock(int16* pData, int nData, int* pActualData)
  \brief  Get last received Stream Base blocks from the system.
  \param  pData       [OUT] Stream Base buffer.
  \param  nData       [IN]  pData Stream Base buffer size.
  \param  pActualData [OUT] The actual number of data filled in pData.
  \return eAO_OK            Success.
  \return eAO_ARG_NULL      One or more of the input arguments is NULL.
  \return eAO_BAD_ARG       if nData <= 0.
  \return eAO_NOT_CONNECTED if the system is not connected.
*/
DECLDIR int GetNextBlock(int16 *pData, int nData, int *pActualData);

/*!
  \fn int RemoveDataType(cChar cType, BOOL bRemove=TRUE)
  \brief  Filter GetNextBlock Stream Base types.
  \param  cType   [IN] Stream Base data type. See StreamFormat.h
  \param  bRemove [IN] if TRUE, GetNextBlock will throw all Stream Base blocks
  of the type cType. \return eAO_OK            Success. \return eAO_BAD_ARG if
  cType not in range 0-255.
*/
DECLDIR int RemoveDataType(cChar cType, BOOL bRemove = TRUE);

/*!
  \fn void AO_Exit()
  \brief Reset system SDK.

  Should be called at the end for clean exit.
*/
DECLDIR void AO_Exit();

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __AO_SYSTEM_API_H__
