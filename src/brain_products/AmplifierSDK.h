//  Copyright (C) 2019 Brain Products GmbH 

/*------------------------------------------------------------------------------------------------*/
//  Project:	Amplifier Windows Library
/*------------------------------------------------------------------------------------------------*/
//  Purpose:	Software development Kit for Brain Products Amplifiers
//  Author:		Brain Products
//  Date:		2019-12-21
//
//	Change History
//	21.12.2018		1.0		Initial Implementation
//	17.01.2019		1.1		Implementation of SetMarker function
//	08.02.2019		1.2		Adds change history
//  09.12.2019		1.3		Added Copyright
//  06.07.2020      1.4     Added SetChannelLabels
/*------------------------------------------------------------------------------------------------*/

 					                                    

#pragma once
#include "Amplifier_LIB.h"

#define SDK_BASE						(-1000)		
#define	SDK_FAIL_FILES_CREATE			(SDK_BASE-1)
#define SDK_FAIL_STORE_DATA				(SDK_BASE-2)
#define SDK_FAIL_SETUP					(SDK_BASE-3)
#define SDK_ERR_BUFFER_SIZE				(SDK_BASE-4)
#define SDK_ERR_AMPLIB_NOTFOUND			(SDK_BASE-5)
#define SDK_ERR_INVALID_FILEHANDLE		(SDK_BASE-6)
#define SDK_FAIL_WRITEDATA				(SDK_BASE-7)
#define SDK_FAIL_STATE					(SDK_BASE-8)

#define SDK_MAJOR				(1)			// Application interface major version
#define SDK_MINOR				(2)			// Application interface minor version


#ifdef AMPLIFIERSDK_EXPORTS
#define AMPLIFIERS_SDK __declspec(dllexport)
#else
#define AMPLIFIERS_SDK __declspec(dllimport)
#endif


#ifndef WINAPI
#include <windows.h>
#endif
#include <stdint.h>
#include <vector>
#include <string>

typedef enum eAmplifierFamily
{
	eLiveAmpFamily = 0,
	eActiChampFamily
}AmplifierFamily;


enum InfoType
{
	eAPIVersion = 0,
	eLIBVersion,
	eSDKVersion
};


#ifdef __cplusplus
extern "C" {  
#endif
/*------------------------------------------------------------------------------------------------*/
// Interface functions
// All of the interface functions will either return a value >= 0 as a valid response 
// or a negative number in case of an error.
/*------------------------------------------------------------------------------------------------*/


#define AMPSDK AMPLIFIERS_SDK int WINAPI 
/// <summary>	General way to call for Version Infos of the 
///				underlying API´s and Libraries </summary>
/// <param name="InfoType">	Enum value specifying the Info to retrieve </param>
/// <param name="Data">	Returned data  </param>
/// <param name="DataSize">	Byte length of returned data  </param>
/// <returns> AMP_OK on success, error code otherwise </returns>
AMPSDK GetInfo(InfoType InfoType, void* Data, int DataSize);

/// <summary>	Loads the libraries that is associated 
///				to the specified amplifier family.  </summary>
/// <param name="AmpFamily">	Enum value specifying the amplifier family </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK SetAmplifierFamily(AmplifierFamily AmpFamily);
/// <summary>	Enumerate available devices. </summary>
/// <param name="HWI">				Select an hardware communiction interface or let the library
/// 								select one by using "ANY". If this parameter is set to "ANY", the
/// 								automatically selected interface is returned by this variable. 
/// 								Possible values are "ANY", "USB", "BT" and "SIM"</param>
/// <param name="HWISize">			Size of the HWI string buffer. </param>
/// <param name="DeviceAddress">	a pre-selected device adress. </param>
/// <param name="flags">			device depending flags. </param>
/// <returns>	The number of available devices. </returns>
AMPSDK EnumerateDevices(char *HWI, int32_t HWISize, const char *DeviceAddress, uint32_t flags);

/// <summary>	Opens a device. </summary>
/// <param name="DeviceNr">	   	The zero based device number. </param>
/// <param name="DeviceHandle">	Returns the handle of the device. </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK OpenDevice(int DeviceNr, HANDLE* DeviceHandle);

/// <summary>	Closes a device. </summary>
/// <param name="DeviceHandle">	Handle of the device. </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK CloseDevice(HANDLE DeviceHandle);

/// <summary>	Get a property value. </summary>
/// <param name="DeviceHandle"> 	Handle of the device. </param>
/// <param name="PropertyGroup">	Property group selection </param>
/// <param name="Index">			The index is not necessary  for the device property group and should be zero.
/// 								For the channel property group this is the zero based channel number. 
/// 								For the module property group this is the zero based module number.</param>
/// <param name="PropertyID">		Property identifier. </param>
/// <param name="PropertyValue">	Property value buffer. </param>
/// <param name="ValueByteSize">	Size of the value buffer in byte. </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK GetProperty(HANDLE DeviceHandle, uint32_t PropertyGroup, uint32_t index, int32_t PropertyID, void *PropertyValue, uint32_t ValueByteSize);

/// <summary>	Get the property range and the range type. </summary>
/// <param name="DeviceHandle"> 	Handle of the device. </param>
/// <param name="PropertyGroup">	Property group selection </param>
/// <param name="Index">			The index is not necessary  for the device property group and should be zero.
/// 								For the channel property group this is the zero based channel number. 
/// 								For the module property group this is the zero based module number.</param>
/// <param name="PropertyID">		Property identifier. </param>
/// <param name="RangeArray">		Property range array buffer. 
/// 								The array elements have the same datatype as the property itself,
/// 								except RT_MINMAX for string properties, which has integer elements
/// 								for the min. and max. number of characters.
/// 								Discrete string properties range is returned as a zero terminated string 
/// 								with elements separated by a LF character.</param>
/// <param name="ArrayByteSize">	Size of the array buffer in byte. </param>
/// <param name="RangeType">		Type of the property range. </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK GetPropertyRange(HANDLE DeviceHandle, t_PropertyGroup PropertyGroup, uint32_t index, int32_t PropertyID, void *RangeArray, uint32_t *ArrayByteSize, t_PropertyRangeType *RangeType);

/// <summary>	Gets a device address. </summary>
/// <param name="DeviceNr">			The zero based device number. </param>
/// <param name="DeviceAddress">	The device address buffer. </param>
/// <param name="BufferSize">   	Size of the buffer. </param>
/// <returns>	. </returns>
AMPSDK GetDeviceAddress(int32_t DeviceNr, char *DeviceAddress, int32_t BufferSize);

/// <summary>	Set a property value. </summary>
/// <param name="DeviceHandle"> 	Handle of the device. </param>
/// <param name="PropertyGroup">	Property group selection </param>
/// <param name="Index">			The index is not necessary  for the device property group and should be zero.
/// 								For the channel property group this is the zero based channel number. 
/// 								For the module property group this is the zero based module number.</param>
/// <param name="PropertyID">		Property identifier. </param>
/// <param name="PropertyValue">	Property value buffer. </param>
/// <param name="ValueByteSize">	Size of the value buffer in byte. </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK SetProperty(HANDLE DeviceHandle, t_PropertyGroup PropertyGroup, uint32_t index, int32_t PropertyID, void *PropertyValue, uint32_t ValueByteSize);

/// <summary>	1. AcquisitionMode = Normal/Test:
///				Read acquired data from the device. 
/// 			The channel order in the buffer is
///				S1_SAMPLECOUNTER, S1_CH1 .. S1_CHn,
///				S2_SAMPLECOUNTER, S2_CH1 .. S2_CHn,
///				...
///				Sn_SAMPLECOUNTER, Sn_CH1 .. Sn_CHn
///				The sample size depends on the number of enabled channels and the channels data type.
///				The sample counter is a 64 bit unsigned integer. 
///				2. AcquisitionMode = Impedance:
///				Impedance values |GND|REF|CHN1|REF|CHN2|REF...
/// 			</summary>
/// <param name="DeviceHandle">			Handle of the device. </param>
/// <param name="Buffer">				Receive buffer. </param>
/// <param name="BufferSize">			Size of the receive buffer in bytes. </param>
/// <param name="RequestedSamples">		Number of requested samples (not yet supported). </param>
/// <returns>	Number of bytes written into the receive buffer on success, error code otherwise </returns>
AMPSDK GetData(HANDLE DeviceHandle, void *Buffer, int32_t BufferSize, int32_t RequestedSamples);


/// <summary>	Start recording to internal memory or HDD. </summary>
/// <param name="DeviceHandle">	Handle of the device. </param>
/// <param name="Buffer">	Data buffer block to be stored. </param>
/// <param name="BufferLength">	Buffer byte length </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK AcquireDataBlock(HANDLE DeviceHandle, void* Buffer, uint32_t BufferLength);

/// <summary>	Start recording to internal memory or HDD. </summary>
/// <param name="DeviceHandle">	Handle of the device. </param>
/// <param name="Buffer">	Data buffer block to be stored. </param>
/// <param name="BufferLength">	Buffer byte length </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK StoreDataBlock(HANDLE DeviceHandle, void* Buffer, uint32_t BufferLength);


/// <summary>	Start recording to internal memory or HDD. </summary>
/// <param name="DeviceHandle">	Handle of the device. </param>
/// <param name="FullPath">	Full path in case of HDD Recording, FileName in case of internal memory recording. </param>
/// <param name="UserComment"> The comment string to be added to the comment section </param>
/// <returns>AMP_OK on success, error code otherwise  </returns>
AMPSDK StartRecording(HANDLE DeviceHandle, const char* FullPath, const char* UserComment, bool BOnSDCard=false);

/// <summary>	Stop recording. </summary>
/// <param name="DeviceHandle">	Handle of the device </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK StopRecording(HANDLE DeviceHandle);

/// <summary>	Start acquisition. </summary>
/// <param name="DeviceHandle">	Handle of the device </param>
/// <param name="Mode">	Recording mode according to t_RecordingMode enum </param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK StartAcquisition(HANDLE DeviceHandle, t_RecordingMode Mode);

/// <summary>	Stop acquisition. </summary>
/// <param name="DeviceHandle">	Handle of the device to which 
///								the storage is associated.</param>
/// <returns>AMP_OK on success, error code otherwise  </returns>
AMPSDK StopAcquisition(HANDLE DeviceHandle);


/// <summary>Sets a Marker with desired type. Caveat: Timing is not precise, if you want a better
///			 timing behaviour use hardware triggers.</summary>
/// <param name = "DeviceHandle"> Device Handle spcifing the current Storage File</param>
/// <param name="Description">	The description of the marker/annotation	</param>
///	 <param name="Type"> The type of the desired marker. Default type is "Annotation".</param>
/// <returns>AMP_OK on success, error code otherwise </returns>
AMPSDK SetMarker(HANDLE DeviceHandle, const char* Description, const char * Type = "Annotation");


/// <summary>Sets custom channel labels for EEG channels. This should be called prior to recording
///           in order to have any effect.</summary>
/// <param name="DeviceHandle"></param>
/// <param name="Labels"></param>
/// <returns>AMP_OK on success, error code otherwise</returns>
AMPSDK SetChannelLabels(HANDLE DeviceHandle, const char* Labels);


#ifdef __cplusplus
}
#endif
