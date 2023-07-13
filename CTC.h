#ifndef __CTC_H__
#define __CTC_H__

#include <intrin.h>
#include <Windows.h>
#include <winternl.h>

//
// The threshold a line measurement needs to break to be considered a positive line measurement
//
#define CTC_MEASUREMENT_POSITIVE_THRESHOLD 75

//
// The amount of times a cache line is flushed repeatedly. The higher, the slower and more accurate.
//
#define CTC_TRANSMIT_FLUSH_FREQUENCY 250000

//
// The magic number through which the start of a transmission is acknowledged
//
#define CTC_TRANSMIT_START_MAGIC 0xBEEFC0DE00000001

//
// The magic numbe through which the end of a transmission is acknowledged
//
#define CTC_TRANSMIT_END_MAGIC 0xBEEFC0DE00000002

#pragma pack( push, 1 )
typedef union _CTC_TRANSMIT_BLOCK
{
	UINT64 AsUint64;

	struct
	{
		UINT32 Value;
		UINT16 ArrayEntry;
		UINT16 Checksum;
	}s;
} CTC_TRANSMIT_BLOCK, *PCTC_TRANSMIT_BLOCK;
#pragma push( pop )

//
// Base virtual address for the communication channel cache lines
//
extern UINT8* g_CTC_CommunicationLines;

//
// The size of a cache line on the current executing machine
//
extern UINT64 g_CTC_CacheLineSize;

/**
 * @brief Initialize the CTC functionality for interprocess communication 
 * 
 * @param [in, optional] CommunicationLines: Cache lines of this specified region will be used as a communicaton channel
*/
VOID
CTC_Initialze( 
	IN LPVOID CommunicationLines OPTIONAL
	);

/**
 * @brief Transmit one or multiple bytes of data to the receiving process
 * 
 * @param [in]   Data: The base virtual address of the data to send 
 * @param [in] Length: The length of the data to send
 * 
 * @return TRUE if the function succeeds
 * @return FALSE if the function fails 
*/
BOOL
CTC_TransmitData( 
	IN UINT8* Data,
	IN UINT32 Length
	);

/**
 * @brief Receive one or multiple bytes from the sending process
 * 
 * @param [out]   Data: The base virtual address of the location in which the data will be received 
 * @param [in]  Length: The length of the data 
 * 
 * @return TRUE if the function succees
 * @return FALSE if the function fails
*/
BOOL
CTC_ReceiveData( 
	OUT UINT8* Data, 
	IN  UINT32 Length 
	);

#endif