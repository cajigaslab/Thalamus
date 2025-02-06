#ifndef __AO_TYPES_H__
#define __AO_TYPES_H__

/////////////////////////////////////////////////////////
// File: AOTypes.h
// Description: defines data types according to OS platform
//
/////////////////////////////////////////////////////////

#ifdef _WIN32

#include <windows.h>

// Win32 Defines
typedef char cChar;
typedef char int8;
typedef unsigned char uint8;
typedef unsigned char byte;
typedef short int16;
typedef unsigned short uint16;
typedef long int32;
typedef unsigned long uint32;
typedef float real32;
typedef double real64;
typedef signed __int64 int64;
typedef unsigned __int64 uint64;
typedef unsigned long ULONG;

#endif // _WIN32

/*!
  \enum EAOResult
  \brief System SDK API return values
*/
enum EAOResult {
  eAO_CONTACT_NOT_EXIST = -9, /*!< The specified contact not exist */
  eAO_MEM_TOO_SMALL = -8, /*!< Internal: Read/Write error, memory too small */
  eAO_MEM_EMPTY = -7,     /*!< Internal: Read error, memory empty */
  eAO_MEM_FULL = -6,      /*!< Internal: Write error, memory full */
  eAO_INIT_ERR = -5,      /*!< Internal: Object still not initialized */
  eAO_ALLOC_ERR = -4,     /*!< Internal: Memory allocation failed */
  eAO_FAIL = -3,          /*!< General Error - Depends on the function */
  eAO_BAD_ARG = -2,       /*!< Bad Argument - Depends on the function */
  eAO_LIB_ERR = -1,       /*!< Library Error */
  eAO_OK = 0,             /*!< No Error */
  eAO_NO_STIMULATION = 3, /*!< Can't stimulate on the specified channel */
  eAO_NOT_CONNECTED = 4,  /*!< The system is not connected */
  ERROR_DEVICEDRIVERNULL = 5, /*!< The device driver is NULL */
  eAO_CHANNEL_NAME_EXIST = 6, /*!< The specified channel name already exist */
  eAO_CHANNEL_NOT_EXIST = 8,  /*!< The specified channel not exist */
  eAO_ARG_NULL = 9,           /*!< Input argument is NULL */
  eAO_ALREADY_CONNECTED = 10, /*!< The system is already connected */
};

enum EAOConnection {
  eAO_DISCONNECTED = 0, /*!< The SDK is disconnected from the system */
  eAO_CONNECTED = 1,    /*!< The SDK is connected to the system */
  eAO_CONNECTING = 2    /*!< The SDK is trying to connect to the system */
};

enum EAOLevelLineDirection { eAO_LEVEL_DOWN = 1, eAO_LEVEL_UP = 2 };

#endif // __AO_TYPES_H__
