//  Copyright (C) 2019 Brain Products GmbH  					                                    //

/**
* \class SDK
* \ingroup PropertyBrowser
*
* A set of template classes is used in order to simplify
* calling of functions from the SDK. The defined classes
* basically groups the functions from the SDK into the 
* classes CDLLHandler, CAmplifier, CStorage. The CAmplifier 
* consists of TModule, TDevice, and TChannel classes.
*
* \author Brain Products
* \version
* \date
* Contact
*/

#pragma once
#include "AmplifierSDK.h"
#include <string>
#include <map>

//#pragma comment(lib, "../Debug/AmplifierSDK.lib")

using namespace std;

inline int GetAmplifierProperty(std::string &value, HANDLE DeviceHandle, uint32_t index, unsigned int PropertyID, PropertyGroup propertyGroup)
{
	char cValue[200];
	int res = GetProperty(DeviceHandle, propertyGroup, index, PropertyID, cValue, sizeof(cValue));
	if (res == AMP_OK)
		value = cValue;
	else
		value = "";
	return res;
}


template<typename T>
struct PropertyRange
{
	PropertyRange()
	{
		Type = RT_BITMASK;
		Index = 0;
		ByteLength = (uint32_t)sizeof(RangeArray);
	}

	T					RangeArray[100];
	PropertyRangeType	Type;
	uint32_t			ByteLength;
	uint32_t			Index;
};

class CModule
{
public:
	CModule() = default;
public:
	/**
	* Gets a numeric module property.
	*/
	template<typename T>
	int TGetProperty(ModulePropertyID id, HANDLE hAmplifier, uint32_t nModule, T& value)
	{
		return GetProperty(hAmplifier, PG_MODULE, nModule, id, &value, sizeof(value));
	}
	/**
	* Gets a string module property.
	*/
	template<>
	int TGetProperty<std::string>(ModulePropertyID id, HANDLE hAmplifier, uint32_t nModule, std::string& value)
	{
		return GetAmplifierProperty(value, hAmplifier, nModule, id, PG_MODULE);
	}
	/**
	* Sets a numeric module property.
	*/
	template<typename T>
	int TSetProperty(ModulePropertyID id, HANDLE hAmplifier, uint32_t nModule, T& value)
	{
		return SetProperty(hAmplifier, PG_MODULE, nModule, id, &value, sizeof(value));
	}
	/**
	* Gets a string module property.
	*/
	template<>
	int TSetProperty<std::string>(ModulePropertyID id, HANDLE hAmplifier, uint32_t nModule, std::string& value)
	{
		return SetProperty(hAmplifier, PG_MODULE, nModule, id, &value, sizeof(value)); //(value, hAmplifier, 0, id, PG_DEVICE);
	}
	/**
	* Gets a module property range.
	*/
	template<typename T>
	int TGetPropertyRange(HANDLE hDevice, PropertyRange<T>& propRange, ModulePropertyID propId)
	{
		return GetPropertyRange(hDevice, PG_MODULE, propRange.Index,
			propId, &propRange.RangeArray, &propRange.ByteLength, &propRange.Type);
	}

};

class CDevice
{
public:
	CDevice() = default;
public:
	/**
	* Sets a numeric device property.
	*/
	template<typename T>
	int TGetProperty(DevicePropertyID id, HANDLE hAmplifier, T& value)
	{
		return GetProperty(hAmplifier, PG_DEVICE, 0, id, &value, sizeof(value));
	}
	/**
	* Sets a string device property.
	*/
	template<>
	int TGetProperty<std::string>(DevicePropertyID id, HANDLE hAmplifier, std::string& value)
	{
		return GetAmplifierProperty(value, hAmplifier, 0, id, PG_DEVICE);
	}
	/**
	* Sets a numeric device property.
	*/
	template<typename T>
	int TSetProperty(DevicePropertyID id, HANDLE hAmplifier, T& value)
	{
		return SetProperty(hAmplifier, PG_DEVICE, 0, id, &value, sizeof(value));
	}
	/**
	* Sets a string device property.
	*/
	template<>
	int TSetProperty<std::string>(DevicePropertyID id, HANDLE hAmplifier, std::string& value)
	{
		return SetProperty(hAmplifier, PG_DEVICE, 0, id, &value, sizeof(value));
	}
	/**
	* Gets a device property range.
	*/
	template<typename T>
	int TGetPropertyRange(HANDLE hDevice, PropertyRange<T>& propRange, DevicePropertyID propId)
	{
		return GetPropertyRange(hDevice, PG_DEVICE, propRange.Index,
			propId, &propRange.RangeArray, &propRange.ByteLength, &propRange.Type);
	}
};

class CChannel
{
public:
	CChannel() = default;
public:
	/**
	* Gets a numeric channel property.
	*/
	template<typename T>
	int TGetProperty(ChannelPropertyID id, HANDLE hAmplifier, uint32_t nChannel, T& value)
	{
		return GetProperty(hAmplifier, PG_CHANNEL, nChannel, id, &value, sizeof(value));
	}
	/**
	* Gets a string channel property.
	*/
	template<>
	int TGetProperty<std::string>(ChannelPropertyID id, HANDLE hAmplifier, uint32_t nChannel, std::string& value)
	{
		return GetAmplifierProperty(value, hAmplifier, nChannel, id, PG_CHANNEL);
	}
	/**
	* Gets a channel property range.
	*/
	template<typename T>
	int TGetPropertyRange(HANDLE hDevice, PropertyRange<T>& propRange, ChannelPropertyID propId)
	{
		return GetPropertyRange(hDevice, PG_CHANNEL, propRange.Index,
			propId, &propRange.RangeArray, &propRange.ByteLength, &propRange.Type);
	}
	/**
	* Sets a numeric channel property.
	*/
	template<typename T>
	int TSetProperty(ChannelPropertyID id, HANDLE hAmplifier, uint32_t index, T& value)
	{
		return SetProperty(hAmplifier, PG_CHANNEL, index, id, &value, sizeof(value));
	}
	/**
	* Sets a string channel property.
	*/
	template<>
	int TSetProperty<std::string>(ChannelPropertyID id, HANDLE hAmplifier, uint32_t index, std::string& value)
	{
		return SetProperty(hAmplifier, PG_CHANNEL, index, id, &value, (uint32_t)value.length());
	}

};

class CAmplifier
{
public:
	CAmplifier() = default;

public:
	CModule		m_Module;
	CChannel	m_Channel;
	CDevice		m_Device;
	HANDLE		m_hAmplifier = NULL;
public:
	/**
	* Opens the amplifier with the device number retrieved \n
	* from EnumerateDevices. The amplifier handle is stored as a member.
	*/
	int Open(int nDeviceNumber)
	{
		return OpenDevice(nDeviceNumber, &m_hAmplifier);
	}
	/**
	* Closes this amplifier device.
	*/
	int Close()
	{
		int nRet = -1;
		nRet = CloseDevice(m_hAmplifier);
		m_hAmplifier = NULL;
		return nRet;
	}

#pragma region GetProperty
	/**
	* Template redirection GetProperty
	*/
	template<typename T>
	int GetProperty(T& value, DevicePropertyID propId)
	{
		return m_Device.TGetProperty(propId, m_hAmplifier, value);
	}
	/**
	* Template redirection GetProperty
	*/
	template<typename T>
	int GetProperty(T& value, uint32_t nModule, ModulePropertyID propId)
	{
		return m_Module.TGetProperty(propId, m_hAmplifier, nModule, value);
	}
	/**
	* Template redirection GetProperty
	*/
	template<typename T>
	int GetProperty(T& value, uint32_t nChannel, ChannelPropertyID propId)
	{
		return m_Channel.TGetProperty(propId, m_hAmplifier, nChannel, value);
	};

#pragma endregion

#pragma region SetProperty
	/**
	* Template redirection SetProperty
	*/
	template<typename T>
	int SetProperty(T& value, DevicePropertyID propId)
	{
		return m_Device.TSetProperty(propId, m_hAmplifier, value);
	}
	/**
	* Template redirection SetProperty
	*/
	template<typename T>
	int SetProperty(T& value, uint32_t nModule, ModulePropertyID propId)
	{
		return m_Module.TSetProperty(propId, m_hAmplifier, nModule, value);
	}
	/**
	* Template redirection SetProperty
	*/
	template<typename T>
	int SetProperty(T& value, uint32_t nChannel, ChannelPropertyID propId)
	{
		return m_Channel.TSetProperty(propId, m_hAmplifier, nChannel, value);
	}

#pragma endregion

#pragma region GetPropertyRange
	/**
	* Template redirection GetPropertyRange
	*/
	template<typename T>
	int GetPropertyRange(PropertyRange<T>& propRange, DevicePropertyID propID)
	{
		return m_Device.TGetPropertyRange(m_hAmplifier, propRange, propID);
	}
	/**
	* Template redirection GetPropertyRange
	*/
	template<typename T>
	int GetPropertyRange(PropertyRange<T>& propRange, ModulePropertyID propID)
	{
		return m_Module.TGetPropertyRange(m_hAmplifier, propRange, propID);
	}
	/**
	* Template redirection GetPropertyRange
	*/
	template<typename T>
	int GetPropertyRange(PropertyRange<T>& propRange, ChannelPropertyID propID)
	{
		return m_Channel.TGetPropertyRange(m_hAmplifier, propRange, propID);
	}
#pragma endregion

	/**
	* Starts the acquisition with the specified recording mode
	*/
	int StartAcquisition(RecordingMode mode)
	{
		return ::StartAcquisition(m_hAmplifier, mode);
	}
	/**
	* Stops the acquisition
	*/
	int StopAcquisition()
	{
		return ::StopAcquisition(m_hAmplifier);
	}
	/**
	* Gets the data from the amplifier, if Recorder modes was \n
	* prior set to RM_IMPEDANCE this function retrieves impedance 
	* data, otherwise it retrieves continous data.
	*/
	int GetData(void* pData, int32_t nDataByteLength, int32_t nSamples)
	{
		return ::GetData(m_hAmplifier, pData, nDataByteLength, nSamples);
	}


};

class CDllHandler
{
public:
	CDllHandler() = default;
	/**
	* Sets the Amplifier Family, triggers SDK to load the correct dll.
	*/
	static int SetAmplifierFamily(AmplifierFamily nAmpFamily)
	{
		return SetAmplifierFamily(nAmpFamily);
	}
	/**
	* Gets info according to specified type.
	*/
	static int GetInfo(InfoType nInfoType, void* pData, int nDataSize)
	{
		return ::GetInfo(nInfoType, pData, nDataSize);
	}
};

class CStorage
{
public :
	CStorage() = default;

	/**
	* Stores a data block
	*/
	static int StoreDataBlock(HANDLE hAmp, void* pData, uint32_t dataByteLength)
	{
		return ::StoreDataBlock(hAmp, pData, dataByteLength);
	}
	/**
	* Stops recording
	*/
	static int StopRecording(CAmplifier& amp)
	{
		return ::StopRecording(amp.m_hAmplifier);
	}
	/**
	* Starts recording; If the flag bUseInternalMem == true, data \n
	* is stored on internal memory. Otherwise the fullpath is used to 
	* store the data in the corresponding folder with the specified name.
	*/
	static int StartRecording(CAmplifier& amp, const char* pFullPath, const char* comment , bool bUseInternalMem)
	{
		return ::StartRecording(amp.m_hAmplifier, pFullPath, comment, bUseInternalMem);
	}
	

	/**
	* This function utilizes the SDK function SetMarker to write Annotations
	* to the marker file.
	*/
	static int SetAnnotation(HANDLE hAmplifier, const char* Description)
	{
		return ::SetMarker(hAmplifier, Description);

	}

	/**
	* This function utilizes the SDK function SetMarker to write Markers
	* to the marker file. If a better timing behavior is desired, use 
	* hardware triggers instead.
	*/
	static int SetMarker(HANDLE hAmplifier, const char* Type, const char* Description)
	{
		return ::SetMarker(hAmplifier, Description, Type);
		
	}

	/**
	* This function is used to set the channel labels of the EEG channels.
	*/
	static int SetChannelLabels(HANDLE hAmplifier, const char* Labels)
	{
		return ::SetChannelLabels(hAmplifier, Labels);
	}

};