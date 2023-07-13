#include "CTC.h"

//
// Functionality macros
//
#define CTC_PrefetchLine( n, l ) _mm_prefetch( ( CHAR* )( g_CTC_CommunicationLines + ( n * g_CTC_CacheLineSize ) ), l );
#define    CTC_FlushLine( n )    _mm_clflushopt(          g_CTC_CommunicationLines + ( n * g_CTC_CacheLineSize ) );

//
// Base virtual address for the communication channel cache lines
//
UINT8* g_CTC_CommunicationLines = NULL;

//
// The size of a cache line on the current executing machine
//
UINT64 g_CTC_CacheLineSize      = NULL;

VOID
CTC_Initialze( 
	IN LPVOID CommunicationLines OPTIONAL
	)
{
	UINT32 CPUInfo[ 4 ]{ };

	//
	// Obtain the feature bits and processor information for the executing machine(EAX=1)
	//
	__cpuidex( ( INT32* )&CPUInfo, 1, NULL );

	//
	// Obtain the cache line size(bits 15:8)*8
	//
	g_CTC_CacheLineSize = ( ( CPUInfo[ 1 ] >> 8 ) & 0xFF ) * 8;

	if ( CommunicationLines == NULL ) {
		g_CTC_CommunicationLines = ( UINT8* )GetModuleHandleA( "kernelbase.dll" );
	}
	else {
		g_CTC_CommunicationLines = ( UINT8* )( ( UINT64 )CommunicationLines & ~( g_CTC_CacheLineSize - 1ull ) );
	}
}

#pragma optimize( "", off )
DECLSPEC_NOINLINE
UINT64
CTC_MeasureLine_Internal( 
	IN UINT32 Line 
	)
{
	UINT32 Junk;

	//
	// Obtain initial timestamp counter
	//
	UINT64 InitTSC = __rdtscp( &Junk );

	//
	// Perform a read operation at the specified address
	//
	Junk = *( g_CTC_CommunicationLines + ( Line * g_CTC_CacheLineSize ) );

	//
	// Return read delta
	//
	return __rdtscp( &Junk ) - InitTSC;
}

DECLSPEC_NOINLINE
UINT64
CTC_MeasureLine( 
	IN UINT32 LineNumber 
	)
{
	UINT64 Average = NULL;

	//
	// Fetch the communication line into all cache levels
	//
	CTC_PrefetchLine( LineNumber, _MM_HINT_T0 );

	for ( UINT32 i = NULL; i < 10; i++ ) {

		//
		// Perform timing measurement
		//
		Average += CTC_MeasureLine_Internal( LineNumber );
	}

	//
	// Return the average time of 10 measurements
	//
	return Average / 10;
}

DECLSPEC_NOINLINE
BOOL
CTC_IsLinePositive( 
	IN UINT32 LineNumber 
	)
{
	UINT16 NumMeasurements = NULL;
	UINT16 Likelihood      = NULL;

	while ( NumMeasurements < 16 ) {
		//
		// If the current measurement is above the threshold, increase likelihood 
		//
		if ( CTC_MeasureLine( LineNumber ) > CTC_MEASUREMENT_POSITIVE_THRESHOLD ) {
			Likelihood++;
		}

		NumMeasurements++;
	}

	return ( Likelihood > ( NumMeasurements / 2 ) );
}

DECLSPEC_NOINLINE
VOID
CTC_SetLinesToUINT64( 
	IN UINT64 Value 
	)
{
	for ( UINT32 n = CTC_TRANSMIT_FLUSH_FREQUENCY; n--; )
	{
		for ( UINT64 i = NULL; i < 64; i++ ) 
		{
			//
			// Flush the line that corresponds to the current bit if set
			//
			if ( Value & ( 1ull << i ) ) {
				CTC_FlushLine( i );
			}
		}
	}
}
#pragma optimize( "", on  )

DECLSPEC_NOINLINE
UINT64
CTC_MostFrequentUINT64( 
	IN UINT64* Values, 
	IN UINT16  Count 
	)
{
	UINT64 Result       = NULL;
	UINT16 OccuranceBar = NULL;

	for ( UINT16 i = NULL; i < Count; i++ )
	{
		UINT16 CurrentOccurances = NULL;

		for ( UINT16 j = NULL; j < Count; j++ )
		{
			if ( &Values[ j ] == &Values[ i ] ) {
				continue;
			}

			if ( Values[ j ] == Values[ i ] ) {
				CurrentOccurances++;
			}

			if ( CurrentOccurances > ( Count >> 1 ) ) {
				Result = Values[ i ];
				return Result;
			}
		}

		if ( CurrentOccurances > OccuranceBar ) 
		{
			OccuranceBar = CurrentOccurances;
			Result = Values[ i ];
		}
	}

	return Result;
}

VOID
CTC_GenerateChecksum( 
	IN  PCTC_TRANSMIT_BLOCK TransmitBlock,
	OUT UINT16*             Checksum 
	)
{
#define CTC_CHECKSUM_CRC_SEED 0x5596A0B1

	UINT32 CRC = _mm_crc32_u16( CTC_CHECKSUM_CRC_SEED, 
		TransmitBlock->s.ArrayEntry ^ ( ( TransmitBlock->s.Value & 0xFFFF ) + ( ( TransmitBlock->s.Value >> 16 ) & 0xFFFF ) ) );

	*Checksum = ( ( CRC >> 16 ) & 0xFFFF ) ^ ( CRC & 0xFFFF );
}

DECLSPEC_NOINLINE
UINT64
CTC_ConvertLinesToUINT64( 
	VOID 
	)
{
	UINT64 ValueSamples[ 16 ]{ };
	UINT16 SampleCount = NULL;

	while ( SampleCount < ARRAYSIZE( ValueSamples ) ) 
	{
		for ( UINT64 i = NULL; i < 64; i++ ) {
			ValueSamples[ SampleCount ] |= ( ( UINT64 )CTC_IsLinePositive( i ) << i );
		}

		SampleCount++;
	}

	return CTC_MostFrequentUINT64( ValueSamples, SampleCount );
}

DECLSPEC_NOINLINE
VOID
CTC_TransmitData_Internal(
	IN UINT32* Array, 
	IN UINT32  ArraySize 
	)
{
	CTC_SetLinesToUINT64( CTC_TRANSMIT_START_MAGIC );

	for ( UINT32 i = NULL; i < ArraySize; i++ )
	{
		CTC_TRANSMIT_BLOCK CurrentBlock;

		CurrentBlock.s.Value      = Array[ i ];
		CurrentBlock.s.ArrayEntry = i;

		CTC_GenerateChecksum( &CurrentBlock, &CurrentBlock.s.Checksum );

		CTC_SetLinesToUINT64( CurrentBlock.AsUint64 );
	}

	CTC_SetLinesToUINT64( CTC_TRANSMIT_END_MAGIC );
}

#include <cstdio>
DECLSPEC_NOINLINE
VOID
CTC_ReceiveData_Internal( 
	OUT UINT32* Array,
	IN  UINT32  ArraySize
	)
{
	//
	// Await the start of the transmission
	//
	while ( CTC_ConvertLinesToUINT64( ) != CTC_TRANSMIT_START_MAGIC ) {
	}

	while ( true )
	{
		UINT64 Value    = CTC_ConvertLinesToUINT64( );
		UINT16 Checksum = NULL;

		if ( Value == CTC_TRANSMIT_END_MAGIC ) {
			break;
		}

		PCTC_TRANSMIT_BLOCK TransmitBlock = ( PCTC_TRANSMIT_BLOCK )&Value;

		if ( TransmitBlock->s.ArrayEntry >= ArraySize ) {
			continue;
		}

		CTC_GenerateChecksum( TransmitBlock, &Checksum );

		if ( Checksum == TransmitBlock->s.Checksum ) {
			Array[ TransmitBlock->s.ArrayEntry ] = TransmitBlock->s.Value;
		}
	}
}

BOOL
CTC_ReceiveData( 
	OUT UINT8* Data, 
	IN  UINT32 Length 
	)
{
	//
	// Align the length of the array to the size of a UINT32
	//
	UINT32 AlignedLength = ( Length + 3 ) & ~( 3 );

	//
	// Allocate a temporary buffer
	//
	UINT32* RecvBuffer = ( UINT32* )HeapAlloc( GetProcessHeap( ), HEAP_ZERO_MEMORY, AlignedLength );

	if ( RecvBuffer == NULL ) {
		return FALSE;
	}

	//
	// Receive the data
	//
	CTC_ReceiveData_Internal( RecvBuffer, AlignedLength >> 2 );

	RtlCopyMemory( Data, RecvBuffer, Length );

	//
	// Free the temporary buffer
	//
	HeapFree( GetProcessHeap( ), NULL, RecvBuffer );

	return TRUE;
}

BOOL
CTC_TransmitData( 
	IN UINT8* Data,
	IN UINT32 Length
	)
{
	//
	// Align the length of the array to the size of a UINT32
	//
	UINT32 AlignedLength = ( Length + 3 ) & ~( 3 );

	//
	// Allocate a temporary buffer
	//
	UINT32* SendBuffer = ( UINT32* )HeapAlloc( GetProcessHeap( ), HEAP_ZERO_MEMORY, AlignedLength );

	if ( SendBuffer == NULL ) {
		return FALSE;
	}

	RtlCopyMemory( SendBuffer, Data, Length );

	//
	// Transmit the data
	//
	CTC_TransmitData_Internal( SendBuffer, AlignedLength >> 2 );

	//
	// Free the temporary buffer
	//
	HeapFree( GetProcessHeap( ), NULL, SendBuffer );

	return TRUE;
}