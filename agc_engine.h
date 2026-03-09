/*
 * Filename:	agc_engine.c
 * Purpose:	This is the main engine for binary simulation of the Apollo AGC
 *  		computer.  It is separate from the Display/Keyboard (DSKY)
 *  		simulation and Apollo hardware simulation, though compatible
 *  		with them.  The executable binary may be created using the
 *  		yayul (Yet Another YUL) assembler.
 * Compiler:	GNU gcc.
 * Contact:	Ron Burkey <info@sandroid.org>
 * Reference:	http://www.ibiblio.org/apollo/index.html

  Filename:  agc_engine.h
  Purpose:  Header file for AGC emulator engine.
  Contact:  Ron Burkey <info@sandroid.org>
  Reference:  http://www.ibiblio.org/apollo
  http://hrst.mit.edu/hrs/apollo/public/archive/1689.pdf and
  http://hrst.mit.edu/hrs/apollo/public/archive/1704.pdf.

*/


#ifndef NULL
#define NULL ((void *) 0)
#endif

#include <stdio.h>

// Embedded, gcc cross-compiler.
typedef short int16_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;

#include <stdint.h>

//----------------------------------------------------------------------
// Externals
//----------------------------------------------------------------------
extern volatile byte ipc_core;   // signals if core1 can run
extern struct channel Agc2Dsky;
extern struct channel Dsky2Agc;   // actual channels 
extern TFT_eSPI tft;  // Invoke library
extern queue_t a2d, d2a;		// queues for the channels
//----------------------------------------------------------------------------
// Constants.

#include "common.h"

// Max number of symbols in a yaAGC sym-dump.
#define MAX_SYM_DUMP 25

// Max number of files in a file dump
#define MAX_FILE_DUMP 25

// Physical AGC timing was generated from a master 1024 KHz clock, divided by 12.
// This resulted in a machine cycle of just over 11.7 microseconds.  Note that the
// constant is unsigned long long.
#define AGC_PER_SECOND ((1024000 + 6) / 12)

// Number of registers to treat as 16 bits rather than 15 bits.  I started here
// with 020, but I found that rupt 4 will load BB into the accumulator and check
// for overflow, with bad results.
#define REG16 3

// Handy names for the memory locations associated with special-purpose
// registers, in octal.
#define RegA 00
#define RegL 01
#define RegQ 02
#define RegEB 03
#define RegFB 04
#define RegZ 05
#define RegBB 06
#define RegZERO 07
#define RegARUPT 010
#define RegLRUPT 011
#define RegQRUPT 012
// Addresses 013 and 014 are spares.
#define RegZRUPT 015
#define RegBBRUPT 016
#define RegBRUPT 017
#define RegCYR 020
#define RegSR 021
#define RegCYL 022
#define RegEDOP 023
// Addresses 024-057 are counters.
#define RegCOUNTER 024
#define RegTIME2 024
#define RegTIME1 025
#define RegTIME3 026
#define RegTIME4 027
#define RegTIME5 030
#define RegTIME6 031
#define RegCDUX 032
#define RegCDUY 033
#define RegCDUZ 034
#define RegOPTY 035
#define RegOPTX 036
#define RegPIPAX 037
#define RegPIPAY 040
#define RegPIPAZ 041
// 042-044 are spares in the CM, rotational hand controller in LM.
#define RegRHCP 042
#define RegRHCY 043
#define RegRHCR 044
#define RegINLINK 045
#define RegRNRAD 046
#define RegGYROCTR 047
#define RegCDUXCMD 050
#define RegCDUYCMD 051
#define RegCDUZCMD 052
#define RegOPTYCMD 053
#define RegOPTXCMD 054
// 055-056 are spares.
#define RegOUTLINK 057
#define RegALTM 060
// Addresses 061-03777 are general-purpose RAM.
#define RegRAM 060
// Addresses 04000-0117777 are ROM (core memory).
#define RegCORE 04000
#define RegEND 0120000

// Constants related to "input/output channels".
#define NUM_CHANNELS 512
#define ChanSCALER2 03
#define ChanSCALER1 04
#define ChanS 07

#define CH77_PARITY_FAIL    000001
#define CH77_TC_TRAP        000004
#define CH77_RUPT_LOCK      000010
#define CH77_NIGHT_WATCHMAN 000020

#define DSKY_AGC_WARN 000001
#define DSKY_TEMP     000010
#define DSKY_KEY_REL  000020
#define DSKY_VN_FLASH 000040
#define DSKY_OPER_ERR 000100
#define DSKY_RESTART  000200
#define DSKY_STBY     000400
#define DSKY_EL_OFF   001000

#define NUM_INTERRUPT_TYPES 10

// Max number of 15-bit words in a downlink-telemetry list.
#define MAX_DOWNLINK_LIST 260

// Screen buffer for telemetry downlinks.  The terminal must be at least
// one bigger in each dimension than the actual amount of text used.
#define DEFAULT_SWIDTH 79
#define DEFAULT_SHEIGHT 42
#define SWIDTH 160
#define SHEIGHT 100

// Identifies the various downlink lists, except for erasable dumps.
#define DL_CM_POWERED_LIST 0
#define DL_LM_ORBITAL_MANEUVERS 1
#define DL_CM_COAST_ALIGN 2
#define DL_LM_COAST_ALIGN 3
#define DL_CM_RENDEZVOUS_PRETHRUST 4
#define DL_LM_RENDEZVOUS_PRETHRUST 5
#define DL_CM_PROGRAM_22 6
#define DL_LM_DESCENT_ASCENT 7
#define DL_LM_LUNAR_SURFACE_ALIGN 8
#define DL_CM_ENTRY_UPDATE 9
#define DL_LM_AGS_INITIALIZATION_UPDATE 10

//---------------------------------------------------------------------------
// Data types.

// Stuff for specifying how to print various fields.

typedef enum {
  FMT_SP, FMT_DP, FMT_OCT, FMT_2OCT, FMT_DEC, FMT_2DEC, FMT_USP
} Format_t;

// Function used for writing out telemetry data.
typedef void Swrite_t (void);
typedef char *Sformat_t (int IndexIntoList, int Scale, Format_t Format);

typedef struct {
  int IndexIntoList;  // if -1, then is a spacer.
  char Name[65];
  int Scale;
  Format_t Format;
  Sformat_t *Formatter;
  int Row;    // If 0,0, then just "next" position.
  int Col;
} FieldSpec_t;

typedef struct {
  char Title[SWIDTH + 1];
  FieldSpec_t FieldSpecs[MAX_DOWNLINK_LIST];
} DownlinkListSpec_t;

// A type of function for processing downlink lists.
typedef void ProcessDownlinkList_t (const DownlinkListSpec_t *Spec);

//--------------------------------------------------------------------------
// Each instance of the AGC CPU simulation has a data structure of type agc_t
// that contains the CPU's internal states, the complete memory space, and any
// other little handy items needed to track execution by the CPU.

typedef struct
{
  // The following variable counts the total number of clock cycles since
  // CPU-startup.  A 64-bit integer is used, because with a 32-bit integer
  // you'd get only about 14 hours before the counter wraps around.
  uint64_t /* unsigned long long */ CycleCounter;
  // All memory -- registers, RAM, and ROM -- is 16-bit, consisting of 15 bits
  // of data and one of (odd) parity.  The MIT documents consistently
  // use octal, so we do as well.
  //int16_t Memory[RegEND];             // Note use of octal.
  int16_t Erasable[8][0400];  // Banks 0,1,2 are "unswitched erasable".
  // There are actually only 36 (0-043) fixed banks, but the calculation of bank
  // numbers by the AGC can theoretically go 0-39 (0-047).  Therefore, I
  // provide some extra.
  int16_t Fixed[40][02000]; // Banks 2,3 are "fixed-fixed".
  uint32_t Parities[40*(02000/32)];
  // There are also "input/output channels".  Output channels are acted upon
  // immediately, but input channels are buffered from asynchronous data.
  int16_t InputChannel[NUM_CHANNELS];
  int16_t OutputChannel7;
  int16_t OutputChannel10[16];
  // The indexing value.
  int16_t IndexValue;
  int8_t InterruptRequests[1 + NUM_INTERRUPT_TYPES];  // 0-index not used.
  // CPU internal flags.
  unsigned ExtraCode:1;   // Set by the "Extend" instruction.
  unsigned AllowInterrupt:1;
  //unsigned RegA16:1;    // Bit "16" of register A.
  unsigned InIsr:1;   // Set when in an ISR, reset when in normal code.
  unsigned SubstituteInstruction:1; // Use BBRUPT register.
  unsigned PendFlag:1;    // Multi-MCT instruction pending.
  unsigned PendDelay:3;   // Countdown to pending instruction.
  unsigned ExtraDelay:3;  // ... and extra, for special cases.
  //unsigned RegQ16:1;    // Bit "16" of register Q.
  unsigned DownruptTimeValid:1; // Set if the DownruptTime field is valid.
  unsigned NightWatchman:1;     // Set when Night Watchman is watching. Cleared by accessing address 67.
  unsigned NightWatchmanTripped:1; // Set when Night Watchman has been tripped and its CH77 bit is being asserted.
  unsigned RuptLock:1;          // Set when rupts are being watched. Cleared by executing any non-ISR instruction
  unsigned NoRupt:1;            // Set when rupts are being watched. Cleared by executing any ISR instruction
  unsigned TCTrap:1;            // Set when TC is being watched. Cleared by executing any non-TC/TCF instruction
  unsigned NoTC:1;              // Set when TC is being watched. Cleared by executing TC or TCF
  unsigned Standby:1;           // Set while the computer is in standby mode.
  unsigned SbyPressed:1;        // Set while PRO is being held down; cleared by releasing PRO
  unsigned SbyStillPressed:1;   // Set upon entry to standby, until PRO is released
  unsigned ParityFail:1;        // Set when a parity failure is encountered accessing memory (in yaAGC, just hitting banks 44+)
  unsigned CheckParity:1;       // Enable parity checking for fixed memory.
  unsigned RestartLight:1;      // The present state of the RESTART light
  unsigned TookBZF:1;           // Flag for having just taken a BZF branch, used for simulation of a TC Trap hardware bug
  unsigned TookBZMF:1;          // Flag for having just taken a BZMF branch, used for simulation of a TC Trap hardware bug
  unsigned GeneratedWarning:1;  // Whether there is a pending input to the warning filter
  unsigned Trap31A:1;           // Enable flag for Trap 31A
  unsigned Trap31B:1;           // Enable flag for Trap 31B
  unsigned Trap32:1;            // Enable flag for Trap 32
  uint32_t WarningFilter;       // Current voltage of the AGC warning filter
  uint64_t /*unsigned long long */ DownruptTime;  // Time when next DOWNRUPT occurs.
  int Downlink;
  int NextZ;                    // Next value for the Z register
  int ScalerCounter;            // Counter to keep track of scaler increment timing
  int ChannelRoutineCount;      // Counter to keep track of channel interface routine timing
  unsigned DskyTimer;           // Timer for DSKY-related timing
  unsigned DskyFlash;           // DSKY flash counter (0 = flash occurring)
  unsigned DskyChannel163;      // Copy of the fake DSKY channel 163
  // The following pointer is present for whatever use the Orbiter
  // integration squad wants.  The Virtual AGC code proper doesn't use it
  // in any way.
  void *agc_clientdata;
} agc_t;

// Forwards
void WriteIO (agc_t * State, int Address, int Value);
int ReadIO (agc_t * State, int Address);

// Stuff for --debug-dsky mode.
#define MAX_DEBUG_RULES 256
typedef struct
{
  int KeyCode;
  int Channel;
  int Value;
  char Logic;
} DebugRule_t;

int DebugDsky = 0;
int InhibitAlarms = 0;
int NumDebugRules = 0;
DebugRule_t DebugRules[MAX_DEBUG_RULES];

// Stuff for --debug mode.
#define MAX_BACKTRACE_POINTS 100
#define BACKTRACES_PER_LINE 5
typedef struct {
  uint64_t /* unsigned long long */ CycleCounter;
  int16_t Erasable[8][0400];  // Banks 0,1,2 are "unswitched erasable".
  int16_t InputChannel[NUM_CHANNELS];
  int16_t OutputChannel7;
  int16_t OutputChannel10[16];
  int16_t IndexValue;
  int8_t InterruptRequests[1 + NUM_INTERRUPT_TYPES];
  int8_t DueToInterrupt;  // Indicates interrupt type causing jump (0 if not).
  unsigned ExtraCode:1;   // Set by the "Extend" instruction.
  unsigned AllowInterrupt:1;  // Set when interrupts are enabled.
  //unsigned RegA16:1;    // Bit "16" of register A.
  unsigned InIsr:1;   // Set when in an ISR, reset when in normal code.
  unsigned SubstituteInstruction:1; // Use BBRUPT register.
  //unsigned RegQ16:1;    // Bit "16" of register Q.
} BacktracePoint_t;

typedef struct
{
  int Socket;
  unsigned char Packet[4];
  int Size;
  int ChannelMasks[256];
  //int DedaBufferCount;
  //int DedaBufferWanted;
  //int DedaBufferReadout;
  //int DedaBufferDefault;
  //int DedaBuffer[9];
} Client_t;

#define DEFAULT_MAX_CLIENTS 10

int DebugMode = 0;
int SingleStepCounter = -2;   // -2 when not in --debug mode.
int BacktraceInitialized = 0;   // Becomes -1 on error.
// We have a backtrace circular buffer, in which we place an entry every
// time an instruction is hit that may branch. The buffer is updated only
// if we're in --debug mode.
BacktracePoint_t *BacktracePoints = NULL;
int BacktraceNextAdd = 0;
int BacktraceCount = 0;
// MAX_CLIENTS is the maximum number of hardware simulations which can be
// attached.  The DSKY is always one, presumably.  The array is a list of
// the sockets used for the clients.  Thus stuff shown below is the
// DEFAULT setup.  The max number of clients can be change during runtime
// initialization by setting MAX_CLIENTS to a different number, allocating
// new arrays of clients and sockets corresponding to the new size, and
// then pointing the Clients and ServerSockets pointers at those arrays.
int MAX_CLIENTS = DEFAULT_MAX_CLIENTS;
static Client_t DefaultClients[DEFAULT_MAX_CLIENTS];
static int DefaultSockets[DEFAULT_MAX_CLIENTS];
Client_t *Clients = DefaultClients;
int *ServerSockets = DefaultSockets;
int NumServers = 0;
int SocketInterlaceReload = 50;
int DebugDeda = 0;
int DedaQuiet = 0;
int DedaMonitor = 0;
int DedaAddress;
uint64_t /* unsigned long long */ DedaWhen;
int DownlinkListBuffer[MAX_DOWNLINK_LIST];
int DownlinkListCount = 0;
int DownlinkListExpected = 0;
int DownlinkListZero = -1;
ProcessDownlinkList_t *ProcessDownlinkList = NULL;
int CmOrLm = 0; // Default is 0 (LM); other choice is 1 (CM)
char Sbuffer[SHEIGHT][SWIDTH + 1];
int Sheight = DEFAULT_SHEIGHT;
int Swidth = DEFAULT_SWIDTH;
int LastRhcPitch = 0;
int LastRhcYaw = 0;
int LastRhcRoll = 0;

#ifndef DECODE_DIGITAL_DOWNLINK_C
extern Swrite_t *SwritePtr;
#endif

////////////////////////////////////////////////////////////////////////////////////////////
///////////// end agc_engine.h
////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////
/////////////// IO SPECIFIC 
//////////////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// Any kind of setup needed by your i/o-channel model.

static int ChannelIsSetUp = 0;

void ChannelSetup (agc_t *State)
{
  ChannelIsSetUp = 1;
}

//-----------------------------------------------------------------------------
// The simulated CPU in yaAGC calls this function whenever it wants to write
// output data to an "i/o channel", other than i/o channels 1 and 2, which are
// overlapped with the L and Q central registers.  For example, in an embedded
// design, this would physically control the individual electrical signals
// comprising the i/o port.  In my recommended reference design (see
// SocketAPI.c) data would be streamed out a socket connection from a port.
// In a customized version, FOR EXAMPLE, data might be written to a shared
// memory array, and other execution threads might be woken up to process the
// changed data.

void ChannelOutput (agc_t * State, int Channel, int Value)
{
  if (!ChannelIsSetUp) ChannelSetup (State);
  
  if ((Channel == 010) || (Channel == 011) || (Channel == 013) || (Channel == 0163)){
	Agc2Dsky.ch = Channel;
	Agc2Dsky.val = Value;
	queue_add_blocking(&a2d, &Agc2Dsky);
  }
  
  #ifdef DEBUG
  char str1[30];
  sprintf(str1, "**** dsky<-AGC %o / %o", Channel, Value);
  Serial.println(str1);
  #endif
  
  // Some output channels have purposes within the CPU, so we have to
  // account for those separately.
  if (Channel == 7)
    {
      State->InputChannel[7] = State->OutputChannel7 = (Value & 0160);
      return;
    }
  // Stick data into the RHCCTR registers, if bits 8,9 of channel 013 are set.
  if (Channel == 013 && 0600 == (0600 & Value) && !CmOrLm)
    {
      State->Erasable[0][042] = LastRhcPitch;
      State->Erasable[0][043] = LastRhcYaw;
      State->Erasable[0][044] = LastRhcRoll;
    }
}

//----------------------------------------------------------------------
// The simulated CPU in yaAGC calls this function to check for input data
// once for each call to agc_engin.  This input data may be of two kinds:
//   1. Data available on an "i/o channel"; in this case, a value
//     of 0 is returned; you can handle as much or as little data
//     of this kind in any given invocation; or
//  2. A request for an "unprogrammed sequence" to automatically
//     increment or decrement a counter.  In this case a value of
//     1 is returned.  The function must return immediately upon
//     one of these requests, in order ot preserve system timing.
// The former type of data is supposed to be directly written to the
// array State->InputChannel[], while the latter is supposed to call the
// function UnprogrammedIncrement() to handle the actual incrementing.
// ChannelInput() has the responsibility of raising an interrupt-request
// flag (in the array State->InterruptRequests[]) if the i/o channel
// data is supposed to cause an interrupt.  (An example would
// be if the input data represented a DSKY keystroke.)  Interrupt-raising
// due to overflow of counters is handled automatically by the function
// UnprogrammedChannel() and doesn't need to be addressed directly.
//
// For example, in an embedded design, this input data would reflect the
// physical states of individual electrical signals.
// In my recommended reference design (see SocketAPI.c) the data would be
// taken from an incoming stream of a socket connection to a port.
// In a customized version, FOR EXAMPLE, data might indicate changes in a
// shared memory array partially controlled by other execution threads.
//
// Note:  You are guaranteed that yaAGC processes at least one instruction
// between any two calls to ChannelInput.

int ChannelInput (agc_t *State)
{
  int RetVal = 0;
  char str1[30];
  
  if (!ChannelIsSetUp) ChannelSetup (State);

  if (queue_try_remove(&d2a, &Dsky2Agc)){
	#ifdef DEBUG  
	sprintf(str1, "dsky->AGC %o / %o", Dsky2Agc.ch, Dsky2Agc.val);
	Serial.println(str1);   
    #endif

	// In this case we're dealing with a counter increment.
	// So increment the counter.		
	/*
	if (Dsky2Agc.ch & 0x80){
		UnprogrammedIncrement (State, Dsky2Agc.ch, Dsky2Agc.val);
		return (1);
	}*/
	
	WriteIO (State, Dsky2Agc.ch, Dsky2Agc.val);
    
	if (Dsky2Agc.ch == 015) 
	  State->InterruptRequests[5] = 1;
	else if (Dsky2Agc.ch == 0173)
	  {
		State->Erasable[0][RegINLINK] = (Dsky2Agc.val & 077777);
		State->InterruptRequests[7] = 1;
	  }
	// Fictitious registers for rotational hand controller (RHC).
	// Note that the RHC angles are not immediately used, but
	// merely squirreled away for later.  They won't actually
	// go into the counter registers until the RHC counters are
	// enabled and the data requested (bits 8,9 of channel 13).
	else if (Dsky2Agc.ch == 0166)
	  {
		LastRhcPitch = Dsky2Agc.val;
		ChannelOutput (State, Dsky2Agc.ch, Dsky2Agc.val);	// echo
	  }
	else if (Dsky2Agc.ch == 0167)
	  {
		LastRhcYaw = Dsky2Agc.val;
		ChannelOutput (State, Dsky2Agc.ch, Dsky2Agc.val);	// echo
	  }
	else if (Dsky2Agc.ch == 0170)
	  {
		LastRhcRoll = Dsky2Agc.val;
		ChannelOutput (State, Dsky2Agc.ch, Dsky2Agc.val);	// echo
	  }
	}

  // If there are changes to the input channels, write the data
  // directly to the array State->InputChannel[].  Don't forget to
  // raise a flag in State->InterruptRequests if the incoming data
  // is supposed to do that.  (Mainly, DSKY keystrokes.)

  // If the inputs request unprogrammed counter-increment sequences,
  // then call the function UnprogrammedChannel(State,Counter,IncType)
  // to process them.  The different unprogrammed sequences are
  // related to the IncTypes as follows:
  //  PINC  000
  //  PCDU  001
  //  MINC  002
  //  MCDU  003
  //  DINC  004
  //  SHINC 005
  //  SHANC 006
  // (Refer to the developer page on www.ibiblio.org/apollo/index.html.)
  // Only registers 32 (octal) through 60 (octal) may actually used as
  // counters, and not all of them.  (Refer to the AGC assembly-language
  // manual at www.ibiblio.org/apollo/index.html.)

  return (RetVal);
}

//----------------------------------------------------------------------
// A function for handling anything routinely needed (i.e., executed on
// a regular schedule) by the i/o channel model of ChannelInput and
// ChannelOutput.  There are no good reasons that I know of why this
// would be needed, other than by my reference model (see SocketAPI.c),
// so you might just want to let this empty.

void ChannelRoutine (agc_t *State)
{
  if (!ChannelIsSetUp)
    ChannelSetup (State);
  // ... anything you like ...

}

//----------------------------------------------------------------------
// This function is useful only for debugging the socket interface, and
// so can be left as-is.

void ShiftToDeda (agc_t *State, int Data)
{
}
//////////////////////////////////////////////////////////////////////////////////////////
/////////////// END IO SPECIFIC 
//////////////////////////////////////////////////////////////////////////////////////////


// This stub-function is here to keep agc_engine from slowing itself down by
// saving backtrace information, which is useful only for a debugger we're not
// building into the code anyway.
void BacktraceAdd (agc_t *State, int Cause)
{
  // Keep this empty.
}


// If COARSE_SMOOTH is 1, then the timing of coarse-alignment (in terms of 
// bursting and separation of bursts) is according to the Delco manual.
// However, since the simulated IMU has no physical inertia, it adjusts 
// instantly (and therefore jerkily).  The COARSE_SMOOTH constant creates
// smaller bursts, and therefore smoother FDAI motion.  Normally, there are
// 192 pulses in a burst.  In the simulation, there are 192/COARSE_SMOOTH
// pulses in a burst.  COARSE_SMOOTH should be in integral divisor of both
// 192 and of 50*1024.  This constrains it to be any power of 2, up to 64.
#define COARSE_SMOOTH 8

// Some helpful macros for manipulating registers.
#define c(Reg) State->Erasable[0][Reg]
#define IsA(Address) ((Address) == RegA)
#define IsL(Address) ((Address) == RegL)
#define IsQ(Address) ((Address) == RegQ)
#define IsEB(Address) ((Address) == RegEB)
#define IsZ(Address) ((Address) == RegZ)
#define IsReg(Address,Reg) ((Address) == (Reg))

// Some helpful constants in parsing the "address" field from an instruction 
// or from the Z register.
#define SIGNAL_00   000000
#define SIGNAL_01   002000
#define SIGNAL_10   004000
#define SIGNAL_11   006000
#define SIGNAL_0011 001400
#define MASK9       000777
#define MASK10      001777
#define MASK12      007777

// Some numerical constant, in AGC format. 
#define AGC_P0 ((int16_t) 0)
#define AGC_M0 ((int16_t) 077777)
#define AGC_P1 ((int16_t) 1)
#define AGC_M1 ((int16_t) 077776)

// Here are arrays which tell (for each instruction, as determined by the
// uppermost 5 bits of the instruction) how many extra machine cycles are 
// needed to execute the instruction.  (In other words, the total number of
// machine cycles for the instruction, minus 1.) The opcode and quartercode
// are taken into account.  There are two arrays -- one for normal 
// instructions and one for "extracode" instructions.
static const int InstructionTiming[32] =
  { 0, 0, 0, 0,			// Opcode = 00.
      1, 0, 0, 0,			// Opcode = 01.
      2, 1, 1, 1,			// Opcode = 02.
      1, 1, 1, 1,			// Opcode = 03.
      1, 1, 1, 1,			// Opcode = 04.
      1, 2, 1, 1,			// Opcode = 05.
      1, 1, 1, 1,			// Opcode = 06.
      1, 1, 1, 1			// Opcode = 07.
    };

// Note that the following table does not properly handle the EDRUPT or
// BZF/BZMF instructions, and extra delay may need to be added specially for
// those cases.  The table figures 2 MCT for EDRUPT and 1 MCT for BZF/BZMF.
static const int ExtracodeTiming[32] =
  { 1, 1, 1, 1,			// Opcode = 010.
      5, 0, 0, 0,			// Opcode = 011.
      1, 1, 1, 1,			// Opcode = 012.
      2, 2, 2, 2,			// Opcode = 013.
      2, 2, 2, 2,			// Opcode = 014.
      1, 1, 1, 1,			// Opcode = 015.
      1, 0, 0, 0,			// Opcode = 016.
      2, 2, 2, 2			// Opcode = 017.
    };

// A way, for debugging, to disable interrupts. The 0th entry disables 
// everything if 0.  Entries 1-10 disable individual interrupts.
int DebuggerInterruptMasks[11] =
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

//-----------------------------------------------------------------------------
// Stuff for doing structural coverage analysis.  Yes, I know it could be done
// much more cleverly.

int CoverageCounts = 0;			// Increment coverage counts is != 0.
unsigned ErasableReadCounts[8][0400];
unsigned ErasableWriteCounts[8][0400];
unsigned ErasableInstructionCounts[8][0400];
unsigned FixedAccessCounts[40][02000];
unsigned IoReadCounts[01000];
unsigned IoWriteCounts[01000];

// For debugging the CDUX,Y,Z inputs.
//File *CduLog = NULL;

//-----------------------------------------------------------------------------
// DSKY handling constants and variables.
#define DSKY_OVERFLOW 81920
#define DSKY_FLASH_PERIOD 4

#define WARNING_FILTER_INCREMENT  15000
#define WARNING_FILTER_DECREMENT     15
#define WARNING_FILTER_MAX       140000
#define WARNING_FILTER_THRESHOLD  20000

//-----------------------------------------------------------------------------
// Functions for reading or writing from/to i/o channels.  The reason we have
// to provide a function for this rather than accessing the i/o-channel buffer
// directly is that the L and Q registers appear in both memory and i/o space,
// at the same addresses. 

int ReadIO (agc_t * State, int Address)
{
  if (Address < 0 || Address > 0777)
    return (0);
  if (CoverageCounts)
    IoReadCounts[Address]++;
  if (Address == RegL || Address == RegQ)
    return (State->Erasable[0][Address]);
  return (State->InputChannel[Address]);
}


void WriteIO (agc_t * State, int Address, int Value)
{
  // The value should be in AGC format. 
  Value &= 077777;
  if (Address < 0 || Address > 0777)
    return;
  if (CoverageCounts)
    IoWriteCounts[Address]++;
  if (Address == RegL || Address == RegQ)
    State->Erasable[0][Address] = Value;

  if (Address == 010)
    {
      // Channel 10 is converted externally to the CPU into up to 16 ports,
      // by means of latching relays.  We need to capture this data.
      State->OutputChannel10[(Value >> 11) & 017] = Value;
    }
  else if ((Address == 015 || Address == 016) && Value == 022)
    {
      // RSET being pressed on either DSKY clears the RESTART light
      // flip-flop directly, without software intervention
      State->RestartLight = 0;
    }
  else if (Address == 033)
    {
      // Channel 33 bits 11-15 are controlled internally, so don't let
      // anybody write to them
      Value = (State->InputChannel[Address] & 076000) | (Value & 001777);
    }

  State->InputChannel[Address] = Value;
}


void CpuWriteIO (agc_t * State, int Address, int Value)
{
  //static int Downlink = 0;

  if (Address == 013)
    {
      // Enable the appropriate traps for HANDRUPT. Note that the trap
      // settings cannot be read back out, so after setting the traps the
      // enable bits are masked out.
      if (Value & 004000)
        State->Trap31A = 1;
      if (Value & 010000)
        State->Trap31B = 1;
      if (Value & 020000)
        State->Trap32 = 1;

      Value &= 043777;
    }
  if (Address == 033)
    {
      // 2005-07-04 RSB.  The necessity for this was pointed out by Mark 
      // Grant via Markus Joachim.  Although channel 033 is an input channel,
      // the CPU writes to it from time to time, to "reset" bits 11-15 to 1.
      // Apparently, these are latched inputs, and this resets the latches.
      State->InputChannel[Address] |= 076000;

      // Don't allow the AGC warning input to be reset if the light
      // is still on
      if (State->WarningFilter > WARNING_FILTER_THRESHOLD)
        State->InputChannel[Address] &= 057777;

      // The actual value that was written now doesn't matter, so make sure
      // no changes occur.
      Value = State->InputChannel[Address];
    }
  else if (Address == 077)
    {
      // Similarly, the CH77 Restart Monitor Alarm Box has latches for
      // alarm codes that are reset when CH77 is written to.
      Value = 0;

      // If the Night Watchman was recently tripped, its CH77 bit
      // is forcibly asserted (unlike all the others) for 1.28s
      if (State->NightWatchmanTripped)
        Value |= CH77_NIGHT_WATCHMAN;
    }
  else if (Address == 011 && (Value & 01000))
    {
      // The DSKY RESTART light is reset whenever CH11 bit 10 is written
      // with a 1. The controlling flip-flop in the AGC also has a hard
      // line to the DSKY's RSET button, so on depression of RSET the
      // light is turned off without need for software intervention.
      State->RestartLight = 0;
    }

  WriteIO (State, Address, Value);
  ChannelOutput (State, Address, Value & 077777);

  // 2005-06-25 RSB.  DOWNRUPT stuff.  I assume that the 20 ms. between
  // downlink transmissions is due to the time needed for transmitting,
  // so I don't interrupt at a regular rate,  Instead, I make sure that
  // there are 20 ms. between transmissions
  if (Address == 034)
    State->Downlink |= 1;
  else if (Address == 035)
    State->Downlink |= 2;
  if (State->Downlink == 3)
    {
      //State->InterruptRequests[8] = 1;	// DOWNRUPT.
      State->DownruptTimeValid = 1;
      State->DownruptTime = State->CycleCounter + (AGC_PER_SECOND / 50);
      State->Downlink = 0;
    }
}

//-----------------------------------------------------------------------------
// This function does all of the processing associated with converting a 
// 12-bit "address" as used within instructions or in the Z register, to a
// pointer to the actual word in the simulated memory.  In other words, here
// we take memory bank-selection into account.  

static int16_t * FindMemoryWord (agc_t * State, int Address12)
{
  //int PseudoAddress;
  int AdjustmentEB, AdjustmentFB;
  int16_t *Addr;

  // Get rid of the parity bit.
  //Address12 = Address12;

  // Make sure the darn thing really is 12 bits.
  Address12 &= 07777;

  // Check to see if NEWJOB (67) has been accessed for Night Watchman
  if (Address12 == 067)
  {
    // Address 67 has been accessed in some way. Clear the Night Watchman.
    State->NightWatchman = 0;
  }

  // It should be noted as far as unswitched-erasable and common-fixed memory
  // is concerned, that the following rules actually do result in continuous
  // block of memory that don't have problems in crossing bank boundaries.
  if (Address12 < 00400)	// Unswitched-erasable.
    return (&State->Erasable[0][Address12 & 00377]);
  else if (Address12 < 01000)	// Unswitched-erasable (continued).
    return (&State->Erasable[1][Address12 & 00377]);
  else if (Address12 < 01400)	// Unswitched-erasable (continued).
    return (&State->Erasable[2][Address12 & 00377]);
  else if (Address12 < 02000)	// Switched-erasable.
    {
      // Recall that the parity bit is accounted for in the shift below.
      AdjustmentEB = (7 & (c (RegEB)>> 8));
      return (&State->Erasable[AdjustmentEB][Address12 & 00377]);
    }
  else if (Address12 < 04000)	// Fixed-switchable.
    {
      AdjustmentFB = (037 & (c (RegFB) >> 10));
      // Account for the superbank bit. 
      if (030 == (AdjustmentFB & 030) && (State->OutputChannel7 & 0100) != 0)
      AdjustmentFB += 010;
    }
  else if (Address12 < 06000)	// Fixed-fixed.
  AdjustmentFB = 2;
  else			  // Fixed-fixed (continued).
  AdjustmentFB = 3;

  Addr = (&State->Fixed[AdjustmentFB][Address12 & 01777]);

  if (State->CheckParity)
    {
      // Check parity for fixed memory if such checking is enabled
      uint16_t LinearAddr = AdjustmentFB*02000 + (Address12 & 01777);
      int16_t ExpectedParity = (State->Parities[LinearAddr / 32] >> (LinearAddr % 32)) & 1;
      int16_t Word = ((*Addr) << 1) | ExpectedParity;
      Word ^= (Word >> 8);
      Word ^= (Word >> 4);
      Word ^= (Word >> 2);
      Word ^= (Word >> 1);
      Word &= 1;
      if (Word != 1)
        {
          // The program is trying to access unused fixed memory, which
          // will trigger a parity alarm.
          State->ParityFail = 1;
          State->InputChannel[077] |= CH77_PARITY_FAIL;
        }
    }
  return Addr;
}

// Same thing, basically, but for collecting coverage data.
#if 0
static void CollectCoverage (agc_t * State, int Address12, int Read, int Write, int Instruction)
{
  int AdjustmentEB, AdjustmentFB;

  if (!CoverageCounts)
  return;

  // Get rid of the parity bit.
  Address12 = Address12;

  // Make sure the darn thing really is 12 bits.
  Address12 &= 07777;

  if (Address12 < 00400)// Unswitched-erasable.
    {
      AdjustmentEB = 0;
      goto Erasable;
    }
  else if (Address12 < 01000)	// Unswitched-erasable (continued).
    {
      AdjustmentEB = 1;
      goto Erasable;
    }
  else if (Address12 < 01400)	// Unswitched-erasable (continued).
    {
      AdjustmentEB = 2;
      goto Erasable;
    }
  else if (Address12 < 02000)	// Switched-erasable.
    {
      // Recall that the parity bit is accounted for in the shift below.
      AdjustmentEB = (7 & (c (RegEB) >> 8));
      Erasable:
      Address12 &= 00377;
      if (Read)
      ErasableReadCounts[AdjustmentEB][Address12]++;
      if (Write)
      ErasableWriteCounts[AdjustmentEB][Address12]++;
      if (Instruction)
      ErasableInstructionCounts[AdjustmentEB][Address12]++;
    }
  else if (Address12 < 04000)	// Fixed-switchable.
    {
      AdjustmentFB = (037 & (c (RegFB) >> 10));
      // Account for the superbank bit. 
      if (030 == (AdjustmentFB & 030) && (State->OutputChannel7 & 0100) != 0)
      AdjustmentFB += 010;
      Fixed:
      FixedAccessCounts[AdjustmentFB][Address12 & 01777]++;
    }
  else if (Address12 < 06000)	// Fixed-fixed.
    {
      AdjustmentFB = 2;
      goto Fixed;
    }
  else				// Fixed-fixed (continued).
    {
      AdjustmentFB = 3;
      goto Fixed;
    }
  return;
}
#endif //0

//-----------------------------------------------------------------------------
// Assign a new value to "erasable" memory, performing editing as necessary
// if the destination address is one of the 4 editing registers.  The value to
// be written is a properly formatted AGC value in D1-15.  The difference between
// Assign and AssignFromPointer is simply that Assign needs a memory bank number
// and an offset into that bank, while AssignFromPointer simply uses a pointer
// directly to the simulated memory location.

static void Assign (agc_t * State, int Bank, int Offset, int Value)
{
  if (Bank < 0 || Bank >= 8)
    return;			// Non-erasable memory.
  if (Offset < 0 || Offset >= 0400)
    return;
  if (CoverageCounts)
    ErasableWriteCounts[Bank][Offset]++;
  if (Bank == 0)
    {
      switch (Offset)
	{
	case RegZ:
	  State->NextZ = Value;
	  break;
	case RegCYR:
	  Value &= 077777;
	  if (0 != (Value & 1))
	    Value = (Value >> 1) | 040000;
	  else
	    Value = (Value >> 1);
	  break;
	case RegSR:
	  Value &= 077777;
	  if (0 != (Value & 040000))
	    Value = (Value >> 1) | 040000;
	  else
	    Value = (Value >> 1);
	  break;
	case RegCYL:
	  Value &= 077777;
	  if (0 != (Value & 040000))
	    Value = (Value << 1) + 1;
	  else
	    Value = (Value << 1);
	  break;
	case RegEDOP:
	  Value &= 077777;
	  Value = ((Value >> 7) & 0177);
	  break;
	case RegZERO:
	  Value = AGC_P0;
	  break;
	default:
	  // No editing of the Value is needed in this case.
	  break;
	}
      if (Offset >= REG16 || (Offset >= 020 && Offset <= 023))
	State->Erasable[0][Offset] = Value & 077777;
      else
	State->Erasable[0][Offset] = Value & 0177777;
    }
  else
    State->Erasable[Bank][Offset] = Value & 077777;
}

static void AssignFromPointer (agc_t * State, int16_t * Pointer, int Value)
{
  int Address;
  Address = Pointer - State->Erasable[0];
  if (Address >= 0 && Address < 04000)
    {
      Assign (State, Address / 0400, Address & 0377, Value);
      return;
    }
}

//-----------------------------------------------------------------------------
// Compute the "diminished absolute value".  The input data and output data
// are both in AGC 1's-complement format.

static int16_t dabs (int16_t Input)
{
  if (0 != (040000 & Input))
    Input = 037777 & ~Input;	// Input was negative, but now is positive.
  if (Input > 1)		// "diminish" it if >1.
    Input--;
  else
    Input = AGC_P0;
  return (Input);
}

// Same, but for 16-bit registers.
static int odabs (int Input)
{
  if (0 != (0100000 & Input))
    Input = (0177777 & ~Input);	// Input was negative, but now is positive.
  if (Input > 1)		// "diminish" it if >1.
    Input--;
  else
    Input = AGC_P0;
  return (Input);
}

//-----------------------------------------------------------------------------
// Convert an AGC-formatted word to CPU-native format. 

static int agc2cpu (int Input)
{
  if (0 != (040000 & Input))
    return (-(037777 & ~Input));
  else
    return (037777 & Input);
}

//-----------------------------------------------------------------------------
// Convert a native CPU-formatted word to AGC format. If the input value is
// out of range, it is truncated by discarding high-order bits.

static int cpu2agc (int Input)
{
  if (Input < 0)
    return (077777 & ~(-Input));
  else
    return (077777 & Input);
}

//-----------------------------------------------------------------------------
// Double-length versions of the same. 

static int agc2cpu2 (int Input)
{
  if (0 != (02000000000 & Input))
    return (-(01777777777 & ~Input));
  else
    return (01777777777 & Input);
}

static int cpu2agc2 (int Input)
{
  if (Input < 0)
    return (03777777777 & ~(01777777777 & (-Input)));
  else
    return (01777777777 & Input);
}

//----------------------------------------------------------------------------
// Here is a small suite of functions for converting back and forth between
// 15-bit SP values and 16-bit accumulator values.

#if 0

// Gets the full 16-bit value of the accumulator (plus parity bit).

static int GetAccumulator (agc_t * State)
  {
    int Value;
    Value = State->Erasable[0][RegA];
    Value &= 0177777;
    return (Value);
  }

// Gets the full 16-bit value of Q (plus parity bit).

static int GetQ (agc_t * State)
  {
    int Value;
    Value = State->Erasable[0][RegQ];
    Value &= 0177777;
    return (Value);
  }

// Store a 16-bit value (plus parity) into the accumulator.

static void PutAccumulator (agc_t * State, int Value)
  {
    c (RegA) = (Value & 0177777);
  }

// Store a 16-bit value (plus parity) into Q.

static void PutQ (agc_t * State, int Value)
  {
    c (RegQ) = (Value & 0177777);
  }

#endif // 0

// Returns +1, -1, or +0 (in SP) format, on the basis of whether an
// accumulator-style "16-bit" value (really 17 bits including parity)
// contains overflow or not.  To do this for the accumulator itself,
// use ValueOverflowed(GetAccumulator(State)).

static int16_t ValueOverflowed (int Value)
{
  switch (Value & 0140000)
    {
    case 0040000:
      return (AGC_P1);
    case 0100000:
      return (AGC_M1);
    default:
      return (AGC_P0);
    }
}

// Return an overflow-corrected value from a 16-bit (plus parity ) SP word.
// This involves just moving bit 16 down to bit 15.

int16_t OverflowCorrected (int Value)
{
  return ((Value & 037777) | ((Value >> 1) & 040000));
}

// Sign-extend a 15-bit SP value so that it can go into the 16-bit (plus parity)
// accumulator.

int SignExtend (int16_t Word)
{
  return ((Word & 077777) | ((Word << 1) & 0100000));
}

//-----------------------------------------------------------------------------
// Here are functions to convert a DP into a more-decent 1's-
// complement format in which there's not an extra sign-bit to contend with.
// (In other words, a 29-bit format in which there's a single sign bit, rather
// than a 30-bit format in which there are two sign bits.)  And vice-versa.
// The DP value consists of two adjacent SP values, MSW first and LSW second,
// and we're given a pointer to the second word.  The main difficulty here
// is dealing with the case when the two SP words don't have the same sign,
// and making sure all of the signs are okay when one or more words are zero.
// A sign-extension is added a la the normal accumulator.

static int SpToDecent (int16_t * LsbSP)
{
  int16_t Msb, Lsb;
  int Value, Complement;
  Msb = LsbSP[-1];
  Lsb = *LsbSP;
  if (Msb == AGC_P0 || Msb == AGC_M0)	// Msb is zero.
    {
      // As far as the case of the sign of +0-0 or -0+0 is concerned,
      // we follow the convention of the DV instruction, in which the
      // overall sign is the sign of the less-significant word.
      Value = SignExtend (Lsb);
      if (Value & 0100000)
	Value |= ~0177777;
      return (07777777777 & Value);	// Eliminate extra sign-ext. bits.
    }
  // If signs of Msb and Lsb words don't match, then make them match.
  if ((040000 & Lsb) != (040000 & Msb))
    {
      if (Lsb == AGC_P0 || Lsb == AGC_M0)	// Lsb is zero.
	{
	  // Adjust sign of Lsb to match Msb.
	  if (0 == (040000 & Msb))
	    Lsb = AGC_P0;
	  else
	    Lsb = AGC_M0;	// 2005-08-17 RSB.  Was "Msb".  Oops!
	}
      else			// Lsb is not zero.
	{
	  // The logic will be easier if the Msb is positive.
	  Complement = (040000 & Msb);
	  if (Complement)
	    {
	      Msb = (077777 & ~Msb);
	      Lsb = (077777 & ~Lsb);
	    }
	  // We now have Msb positive non-zero and Lsb negative non-zero.
	  // Subtracting 1 from Msb is equivalent to adding 2**14 (i.e.,
	  // 0100000, accounting for the parity) to Lsb.  An additional 1 
	  // must be added to account for the negative overflow.
	  Msb--;
	  Lsb = ((Lsb + 040000 + AGC_P1) & 077777);
	  // Restore the signs, if necessary.
	  if (Complement)
	    {
	      Msb = (077777 & ~Msb);
	      Lsb = (077777 & ~Lsb);
	    }
	}
    }
  // We now have an Msb and Lsb of the same sign; therefore,
  // we can simply juxtapose them, discarding the sign bit from the 
  // Lsb.  (And recall that the 0-position is still the parity.)
  Value = (03777740000 & (Msb << 14)) | (037777 & Lsb);
  // Also, sign-extend for further arithmetic.
  if (02000000000 & Value)
    Value |= 04000000000;
  return (Value);
}

static void DecentToSp (int Decent, int16_t * LsbSP)
{
  int Sign;
  Sign = (Decent & 04000000000);
  *LsbSP = (037777 & Decent);
  if (Sign)
    *LsbSP |= 040000;
  LsbSP[-1] = OverflowCorrected (0177777 & (Decent >> 14));	// Was 13.
}

// Adds two sign-extended SP values.  The result may contain overflow.
int AddSP16 (int Addend1, int Addend2)
{
  int Sum;
  Sum = Addend1 + Addend2;
  if (Sum & 0200000)
    {
      Sum += AGC_P1;
      Sum &= 0177777;
    }
  return (Sum);
}

// Absolute value of an SP value.

static int16_t AbsSP (int16_t Value)
{
  if (040000 & Value)
    return (077777 & ~Value);
  return (Value);
}

// Check if an SP value is negative.

//static int
//IsNegativeSP (int16_t Value)
//{
//  return (0 != (0100000 & Value));
//}

// Negate an SP value.

static int16_t NegateSP (int16_t Value)
{
  return (077777 & ~Value);
}

//-----------------------------------------------------------------------------
// The following are various operations performed on counters, as defined
// in Savage & Drake (E-2052) 1.4.8.  The functions all return 0 normally,
// and return 1 on overflow.

//#include <stdio.h>
static int TrapPIPA = 0;

// 1's-complement increment
int CounterPINC (int16_t * Counter)
{
  int16_t i;
  int Overflow = 0;
  i = *Counter;
  if (i == 037777)
    {
      Overflow = 1;
      i = AGC_P0;
    }
  else
    {
      Overflow = 0;
      if (TrapPIPA)
	printf ("PINC: %o", i);
      i = ((i + 1) & 077777);
      if (TrapPIPA)
	printf (" %o", i);
      if (i == AGC_P0)	// Account for -0 to +1 transition.
	i++;
      if (TrapPIPA)
	printf (" %o\n", i);
    }
  *Counter = i;
  return (Overflow);
}

// 1's-complement decrement, but only of negative integers.
int CounterMINC (int16_t * Counter)
{
  int16_t i;
  int Overflow = 0;
  i = *Counter;
  if (i == (int16_t) 040000)
    {
      Overflow = 1;
      i = AGC_M0;
    }
  else
    {
      Overflow = 0;
      if (TrapPIPA)
	printf ("MINC: %o", i);
      i = ((i - 1) & 077777);
      if (TrapPIPA)
	printf (" %o", i);
      if (i == AGC_M0)	// Account for +0 to -1 transition.
	i--;
      if (TrapPIPA)
	printf (" %o\n", i);
    }
  *Counter = i;
  return (Overflow);
}

// 2's-complement increment.
int CounterPCDU (int16_t * Counter)
{
  int16_t i;
  int Overflow = 0;
  i = *Counter;
  if (i == (int16_t) 077777)
    Overflow = 1;
  i++;
  i &= 077777;
  *Counter = i;
  return (Overflow);
}

// 2's-complement decrement.
int CounterMCDU (int16_t * Counter)
{
  int16_t i;
  int Overflow = 0;
  i = *Counter;
  if (i == 0)
    Overflow = 1;
  i--;
  i &= 077777;
  *Counter = i;
  return (Overflow);
}

// Diminish increment.
int CounterDINC (agc_t *State, int CounterNum, int16_t * Counter)
{
  int RetVal = 0;
  int16_t i;
  i = *Counter;
  if (i == AGC_P0 || i == AGC_M0)	// Zero?
    {
      // Emit a ZOUT.
      if (CounterNum != 0)
	ChannelOutput (State, 0x80 | CounterNum, 017);

      RetVal = 1;
    }
  else if (040000 & i)			// Negative?
    {
      i = AddSP16(SignExtend(i), SignExtend(AGC_P1)) & 077777;

      // Emit a MOUT.
      if (CounterNum != 0)
	ChannelOutput (State, 0x80 | CounterNum, 016);
    }
  else					// Positive?
    {
      i = AddSP16(SignExtend(i), SignExtend(AGC_M1)) & 077777;

      // Emit a POUT.
      if (CounterNum != 0)
	ChannelOutput (State, 0x80 | CounterNum, 015);
    }

  *Counter = i;

  return (RetVal);
}

// Left-shift increment.
int CounterSHINC (int16_t * Counter)
{
  int16_t i;
  int Overflow = 0;
  i = *Counter;
  if (020000 & i)
    Overflow = 1;
  i = (i << 1) & 037777;
  *Counter = i;
  return (Overflow);
}

// Left-shift and add increment.
int CounterSHANC (int16_t * Counter)
{
  int16_t i;
  int Overflow = 0;
  i = *Counter;
  if (020000 & i)
    Overflow = 1;
  i = ((i << 1) + 1) & 037777;
  *Counter = i;
  return (Overflow);
}

// Pinch hits for the above in setting interrupt requests with INCR,
// AUG, and DIM instructins.  The docs aren't very forthcoming as to 
// which counter registers are affected by this ... but still.

static void InterruptRequests (agc_t * State, int16_t Address10, int Sum)
{
  if (ValueOverflowed (Sum) == AGC_P0)
    return;
  if (IsReg(Address10, RegTIME1))
    CounterPINC (&c(RegTIME2));
  else if (IsReg(Address10, RegTIME5))
    State->InterruptRequests[2] = 1;
  else if (IsReg(Address10, RegTIME3))
    State->InterruptRequests[3] = 1;
  else if (IsReg(Address10, RegTIME4))
    State->InterruptRequests[4] = 1;
  // TIME6 requires a ZOUT to happen during a DINC sequence for its
  // interrupt to fire
}

//-------------------------------------------------------------------------------
// The case of PCDU or MCDU triggers being applied to the CDUX,Y,Z counters
// presents a special problem.  The AGC expects these triggers to be 
// applied at a certain fixed rate.  The DAP portion of Luminary or Colossus
// applies a digital filter to the counts, in order to eliminate electrical
// noise, as well as noise caused by vibration of the spacecraft.  Therefore,
// if the simulated IMU applies PCDU/MCDU triggers too fast, the digital
// filter in the DAP will simply reject the count, and therefore the spacecraft's
// orientation cannot be measured by the DAP.  Consequently, we have to 
// fake up a kind of FIFO on the triggers to the CDUX,Y,Z counters so that
// we can increment or decrement the counters at no more than the fixed rate.
// (Conversely, of course, the simulated IMU has to be able to supply the 
// triggers *at least* as fast as the fixed rate.)
//
// Actually, there are two different fixed rates for PCDU/MCDU:  400 counts
// per second in "slow mode", and 6400 counts per second in "fast mode".
//
// *** FIXME! All of the following junk will need to move to agc_t, and will
//     somehow have to be made compatible with backtraces. ***
// The way the FIFO works is that it can hold an ordered set of + counts and
// - counts.  For example, if it held 7,-5,10, it would mean to apply 7 PCDUs,
// followed by 5 MCDUs, followed by 10 PCDUs.  If there are too many sign-changes
// buffered, triggers will be transparently dropped.
#define MAX_CDU_FIFO_ENTRIES 128
#define NUM_CDU_FIFOS 3			// Increase to 5 to include OPTX, OPTY.
#define FIRST_CDU 032
typedef struct
{
  int Ptr;				// Index of next entry being pulled.
  int Size;				// Number of entries.
  int IntervalType;			// 0,1,2,0,1,2,...
  uint64_t NextUpdate;	// Cycle count at which next counter update occurs.
  int32_t Counts[MAX_CDU_FIFO_ENTRIES];
} CduFifo_t;

static CduFifo_t CduFifos[NUM_CDU_FIFOS];// For registers 032, 033, and 034.
static int CduChecker = 0;		// 0, 1, ..., NUM_CDU_FIFOS-1, 0, 1, ...

// Here's an auxiliary function to add a count to a CDU FIFO.  The only allowed
// increment types are:
//	001	PCDU "slow mode"
//	003	MCDU "slow mode"
//	021	PCDU "fast mode"
//	023	MCDU "fast mode"
// Within the FIFO, we distinguish these cases as follows:
//	001	Upper bits = 00
//	003	Upper bits = 01
//	021	Upper bits = 10
//	023	Upper bits = 11
// The least-significant 30 bits are simply the absolute value of the count.
static void PushCduFifo (agc_t *State, int Counter, int IncType)
{
  CduFifo_t *CduFifo;
  int Next, Interval;
  int32_t Base;
  if (Counter < FIRST_CDU || Counter >= FIRST_CDU + NUM_CDU_FIFOS)
    return;
  switch (IncType)
    {
    case 1:
      Interval = 213;
      Base = 0x00000000;
      break;
    case 3:
      Interval = 213;
      Base = 0x40000000;
      break;
    case 021:
      Interval = 13;
      Base = 0x80000000;
      break;
    case 023:
      Interval = 13;
      Base = 0xC0000000;
      break;
    default:
      return;
    }
  //if (CduLog != NULL)
    //fprintf (CduLog, "< " FORMAT_64U " %o %02o\n", State->CycleCounter, Counter, IncType);
  CduFifo = &CduFifos[Counter - FIRST_CDU];
  // It's a little easier if the FIFO is completely empty.
  if (CduFifo->Size == 0)
    {
      CduFifo->Ptr = 0;
      CduFifo->Size = 1;
      CduFifo->Counts[0] = Base + 1;
      CduFifo->NextUpdate = State->CycleCounter + Interval;
      CduFifo->IntervalType = 1;
      return;
    }
  // Not empty, so find the last entry in the FIFO.
  Next = CduFifo->Ptr + CduFifo->Size - 1;
  if (Next >= MAX_CDU_FIFO_ENTRIES)
    Next -= MAX_CDU_FIFO_ENTRIES;
  // Last entry has different sign from the new data?
  if ((CduFifo->Counts[Next] & 0xC0000000) != (unsigned) Base)
    {
      // The sign is different, so we have to add a new entry to the
      // FIFO.
      if (CduFifo->Size >= MAX_CDU_FIFO_ENTRIES)
	{
	  // No place to put it, so drop the data.
	  return;
	}
      CduFifo->Size++;
      Next++;
      if (Next >= MAX_CDU_FIFO_ENTRIES)
	Next -= MAX_CDU_FIFO_ENTRIES;
      CduFifo->Counts[Next] = Base + 1;
      return;
    }
  // Okay, add in the new data to the last FIFO entry.  The sign is assured
  // to be compatible.  The size of the FIFO doesn't increase. We also don't
  // bother to check for arithmetic overflow, since only the wildest IMU
  // failure could cause it.
  CduFifo->Counts[Next]++;
}

// Here's an auxiliary function to perform the next available PCDU or MCDU
// from a CDU FIFO, if it is time to do so.  We only check one of the CDUs
// each time around (in order to preserve proper cycle counts), so this function 
// must be called at at least an 6400*NUM_CDU_FIFO cps rate.  Returns 0 if no
// counter was updated, non-zero if a counter was updated.
static int ServiceCduFifo (agc_t *State)
{
  int Count, RetVal = 0, HighRate, DownCount;
  CduFifo_t *CduFifo;
  int16_t *Ch;
  // See if there are any pending PCDU or MCDU counts we need to apply.  We only
  // check one of the CDUs, and the CDU to check is indicated by CduChecker.
  CduFifo = &CduFifos[CduChecker];

  if (CduFifo->Size > 0 && State->CycleCounter >= CduFifo->NextUpdate)
    {
      // Update the counter.
      Ch = &State->Erasable[0][CduChecker + FIRST_CDU];
      Count = CduFifo->Counts[CduFifo->Ptr];
      HighRate = (Count & 0x80000000);
      DownCount = (Count & 0x40000000);
      if (DownCount)
	{
	  CounterMCDU (Ch);
	  //if (CduLog != NULL)
	    //fprintf (CduLog, ">\t\t" FORMAT_64U " %o 03\n", State->CycleCounter, CduChecker + FIRST_CDU);
	}
      else
	{
	  CounterPCDU (Ch);
	  //if (CduLog != NULL)
	    //fprintf (CduLog, ">\t\t" FORMAT_64U " %o 01\n", State->CycleCounter, CduChecker + FIRST_CDU);
	}
      Count--;
      // Update the FIFO.
      if (0 != (Count & ~0xC0000000))
	CduFifo->Counts[CduFifo->Ptr] = Count;
      else
	{
	  // That FIFO entry is exhausted.  Remove it from the FIFO.
	  CduFifo->Size--;
	  CduFifo->Ptr++;
	  if (CduFifo->Ptr >= MAX_CDU_FIFO_ENTRIES)
	    CduFifo->Ptr = 0;
	}
      // And set next update time.
      // Set up for next update time.  The intervals is are of the form
      // x, x, y, depending on whether CduIntervalType is 0, 1, or 2.
      // This is done because with a cycle type of 1024000/12 cycles per
      // second, the exact CDU update times don't fit on exact cycle
      // boundaries, but every 3rd CDU update does hit a cycle boundary.
      if (CduFifo->NextUpdate == 0)
	CduFifo->NextUpdate = State->CycleCounter;
      if (CduFifo->IntervalType < 2)
	{
	  if (HighRate)
	    CduFifo->NextUpdate += 13;
	  else
	    CduFifo->NextUpdate += 213;
	  CduFifo->IntervalType++;
	}
      else
	{
	  if (HighRate)
	    CduFifo->NextUpdate += 14;
	  else
	    CduFifo->NextUpdate += 214;
	  CduFifo->IntervalType = 0;
	}
      // Return an indication that a counter was updated.
      RetVal = 1;
    }

  CduChecker++;
  if (CduChecker >= NUM_CDU_FIFOS)
    CduChecker = 0;

  return (RetVal);
}

//----------------------------------------------------------------------------
// This function is used to update the counter registers on the basis of 
// commands received from the outside world.

void UnprogrammedIncrement (agc_t *State, int Counter, int IncType)
{
  int16_t *Ch;
  int Overflow = 0;
  Counter &= 0x7f;
  Ch = &State->Erasable[0][Counter];
  if (CoverageCounts)
    ErasableWriteCounts[0][Counter]++;
  switch (IncType)
    {
    case 0:
      //TrapPIPA = (Counter >= 037 && Counter <= 041);
      Overflow = CounterPINC (Ch);
      break;
    case 1:
    case 021:
      // For the CDUX,Y,Z counters, push the command into a FIFO.
      if (Counter >= FIRST_CDU && Counter < FIRST_CDU + NUM_CDU_FIFOS)
	PushCduFifo (State, Counter, IncType);
      else
	Overflow = CounterPCDU (Ch);
      break;
    case 2:
      //TrapPIPA = (Counter >= 037 && Counter <= 041);
      Overflow = CounterMINC (Ch);
      break;
    case 3:
    case 023:
      // For the CDUX,Y,Z counters, push the command into a FIFO.
      if (Counter >= FIRST_CDU && Counter < FIRST_CDU + NUM_CDU_FIFOS)
	PushCduFifo (State, Counter, IncType);
      else
	Overflow = CounterMCDU (Ch);
      break;
    case 4:
      Overflow = CounterDINC (State, Counter, Ch);
      break;
    case 5:
      Overflow = CounterSHINC (Ch);
      break;
    case 6:
      Overflow = CounterSHANC (Ch);
      break;
    default:
      break;
    }
  if (Overflow)
    {
      // On some counters, overflow is supposed to cause
      // an interrupt.  Take care of setting the interrupt request here.

    }
  TrapPIPA = 0;
}

//----------------------------------------------------------------------------
// Function handles the coarse-alignment output pulses for one IMU CDU drive axis.  
// It returns non-0 if a non-zero count remains on the axis, 0 otherwise.

static int BurstOutput (agc_t *State, int DriveBitMask, int CounterRegister, int Channel)
{
  static int CountCDUX = 0, CountCDUY = 0, CountCDUZ = 0; // In target CPU format.
  int DriveCount = 0, DriveBit, Direction = 0, Delta, DriveCountSaved;
  if (CounterRegister == RegCDUXCMD)
    DriveCountSaved = CountCDUX;
  else if (CounterRegister == RegCDUYCMD)
    DriveCountSaved = CountCDUY;
  else if (CounterRegister == RegCDUZCMD)
    DriveCountSaved = CountCDUZ;
  else
    return (0);
  // Driving this axis?
  DriveBit = (State->InputChannel[014] & DriveBitMask);
  // If so, we must retrieve the count from the counter register.
  if (DriveBit)
    {
      DriveCount = State->Erasable[0][CounterRegister];
      State->Erasable[0][CounterRegister] = 0;
    }
  // The count may be negative.  If so, normalize to be positive and set the
  // direction flag.
  Direction = (040000 & DriveCount);
  if (Direction)
    {
      DriveCount ^= 077777;
      DriveCountSaved -= DriveCount;
    }
  else
    DriveCountSaved += DriveCount;
  if (DriveCountSaved < 0)
    {
      DriveCountSaved = -DriveCountSaved;
      Direction = 040000;
    }
  else
    Direction = 0;
  // Determine how many pulses to output.  The max is 192 per burst.
  Delta = DriveCountSaved;
  if (Delta >= 192 / COARSE_SMOOTH)
    Delta = 192 / COARSE_SMOOTH;
  // If the count is non-zero, pulse it.
  if (Delta > 0)
    {
      ChannelOutput (State, Channel, Direction | Delta);
      DriveCountSaved -= Delta;
    }
  if (Direction)
    DriveCountSaved = -DriveCountSaved;
  if (CounterRegister == RegCDUXCMD)
    CountCDUX = DriveCountSaved;
  else if (CounterRegister == RegCDUYCMD)
    CountCDUY = DriveCountSaved;
  else if (CounterRegister == RegCDUZCMD)
    CountCDUZ = DriveCountSaved;
  return (DriveCountSaved);
}

static void UpdateDSKY(agc_t *State)
{
  unsigned LastChannel163 = State->DskyChannel163;

  State->DskyChannel163 &= ~(DSKY_KEY_REL | DSKY_VN_FLASH | DSKY_OPER_ERR | DSKY_RESTART | DSKY_STBY | DSKY_AGC_WARN | DSKY_TEMP);

  if (State->InputChannel[013] & 01000)
    // The light test is active. Light RESTART and STBY.
    State->DskyChannel163 |= DSKY_RESTART | DSKY_STBY; // 

  // If we're in standby, light the standby light
  if (State->Standby)
    State->DskyChannel163 |= DSKY_STBY;

  // Make the RESTART light mirror State->RestartLight.
  if (State->RestartLight)
    State->DskyChannel163 |= DSKY_RESTART;

  // Light TEMP if channel 11 bit 4 is set, or channel 30 bit 15 is set
  if ((State->InputChannel[011] & 010) || (State->InputChannel[030] & 040000))
    State->DskyChannel163 |= DSKY_TEMP;

  // Set KEY REL and OPER ERR according to channel 11
  if (State->InputChannel[011] & DSKY_KEY_REL)
    State->DskyChannel163 |= DSKY_KEY_REL;
  if (State->InputChannel[011] & DSKY_OPER_ERR)
    State->DskyChannel163 |= DSKY_OPER_ERR;

  // Turn on the AGC warning light if the warning filter is above its threshold
  if (State->WarningFilter > WARNING_FILTER_THRESHOLD)
    {
      State->DskyChannel163 |= DSKY_AGC_WARN;

      // Set the AGC Warning input bit in channel 33
      State->InputChannel[033] &= 057777;
    }

  // Update the DSKY flash counter based on the DSKY timer
  while (State->DskyTimer >= DSKY_OVERFLOW)
    {
      State->DskyTimer -= DSKY_OVERFLOW;
      State->DskyFlash = (State->DskyFlash + 1) % DSKY_FLASH_PERIOD;
    }

  // Flashing lights on the DSKY have a period of 1.28s, and a 75% duty cycle
  if (!State->Standby && State->DskyFlash == 0)
    {
      // If V/N FLASH is high, then the lights are turned off
      if (State->InputChannel[011] & DSKY_VN_FLASH)
        State->DskyChannel163 |= DSKY_VN_FLASH;

      // Flash off the KEY REL and OPER ERR lamps
      State->DskyChannel163 &= ~DSKY_KEY_REL;
      State->DskyChannel163 &= ~DSKY_OPER_ERR;
    }

  // Send out updated display information, if something on the DSKY changed
  if (State->DskyChannel163 != LastChannel163)
    ChannelOutput(State, 0163, State->DskyChannel163);
}

//----------------------------------------------------------------------------
// This function implements a model of what happens in the actual AGC hardware
// during a divide -- but made a bit more readable / software-centric than the 
// actual register transfer level stuff. It should nevertheless give accurate
// results in all cases, including those that result in "total nonsense".
// If A, L, or Z are the divisor, it assumes that the unexpected transformations
// have already been applied to the "divisor" argument.
static void SimulateDV(agc_t *State, uint16_t divisor)
{
    uint16_t dividend_sign = 0;
    uint16_t divisor_sign = 0;
    uint16_t remainder;
    uint16_t remainder_sign = 0;
    uint16_t quotient_sign = 0;
    uint16_t quotient = 0;
    uint16_t sum = 0;
    uint16_t a = c(RegA);
    uint16_t l = c(RegL);
    int i;

    // Assume A contains the sign of the dividend
    dividend_sign = a & 0100000;

    // Negate A if it was positive
    if (!dividend_sign)
      a = ~a;
    // If A is now -0, take the dividend sign from L
    if (a == 0177777)
      dividend_sign = l & 0100000;
    // Negate L if the dividend is negative.
    if (dividend_sign)
      l = ~l;

    // Add 40000 to L
    l = AddSP16(l, 040000);
    // If this did not cause positive overflow, add one to A
    if (ValueOverflowed(l) != AGC_P1)
      a = AddSP16(a, 1);
    // Initialize the remainder with the current value of A
    remainder = a;

    // Record the sign of the divisor, and then take its absolute value
    divisor_sign = divisor & 0100000;
    if (divisor_sign)
      divisor = ~divisor;
    // Initialize the quotient via a WYD on L (L's sign is placed in bits
    // 16 and 1, and L bits 14-1 are placed in bits 15-2).
    quotient_sign = l & 0100000;
    quotient = quotient_sign | ((l & 037777) << 1) | (quotient_sign >> 15);

    for (i = 0; i < 14; i++)
    {
        // Shift up the quotient
        quotient <<= 1;
        // Perform a WYD on the remainder
        remainder_sign = remainder & 0100000;
        remainder = remainder_sign | ((remainder & 037777) << 1);
        // The sign is only placed in bit 1 if the quotient's new bit 16 is 1
        if ((quotient & 0100000) == 0)
          remainder |= (remainder_sign >> 15);
        // Add the divisor to the remainder
        sum = AddSP16(remainder, divisor);
        if (sum & 0100000)
          {
            // If the resulting sum has its bit 16 set, OR a 1 onto the
            // quotient and take the sum as the new remainder
            quotient |= 1;
            remainder = sum;
          }
    }
    // Restore the proper quotient sign
    a = quotient_sign | (quotient & 077777);

    // The final value for A is negated if the dividend sign and the
    // divisor sign did not match
    c(RegA) = (dividend_sign != divisor_sign) ? ~a : a;
    // The final value for L is negated if the dividend was negative
    c(RegL) = (dividend_sign) ? remainder : ~remainder;
}

//-----------------------------------------------------------------------------
// Execute one machine-cycle of the simulation.  Use agc_engine_init prior to 
// the first call of agc_engine, to initialize State, and then call agc_engine 
// thereafter every (simulated) 11.7 microseconds.
//
// Returns:
//      0 -- success
// I'm not sure if there are any circumstances under which this can fail ...

// Note on addressing of bits within words:  The MIT docs refer to bits
// 1 through 15, with 1 being the least-significant, and 15 the most 
// significant.  A 16th bit, the (odd) parity bit, would be bit 0 in this
// scheme.  Now, we're probably not going to use the parity bit in our
// simulation -- I haven't fully decided this at the time I'm writing
// this note -- so we have a choice of whether to map the 15 bits that ARE
// used to D0-14 or to D1-15.  I'm going to choose the latter, even though
// it requires slightly more processing, in order to conform as obviously
// as possible to the MIT docs.

#define SCALER_OVERFLOW 80
#define SCALER_DIVIDER 3

// Fine-alignment.
// The gyro needs 3200 pulses per second, and therefore counts twice as
// fast as the regular 1600 pps counters.
#define GYRO_OVERFLOW 160
#define GYRO_DIVIDER (2 * 3)
static unsigned GyroCount = 0;
static unsigned OldChannel14 = 0, GyroTimer = 0;

// Coarse-alignment.
// The IMU CDU drive emits bursts every 600 ms.  Each cycle is 
// 12/1024000 seconds long.  This happens to mean that a burst is
// emitted every 51200 CPU cycles, but we multiply it out below
// to make it look pretty
#define IMUCDU_BURST_CYCLES ((600 * 1024000) / (1000 * 12 * COARSE_SMOOTH))
static uint64_t ImuCduCount = 0;
static unsigned ImuChannel14 = 0;

int agc_engine(agc_t * State){
  int i, j;
  uint16_t ProgramCounter, Instruction, /*OpCode,*/ QuarterCode, sExtraCode;
  int16_t *WhereWord;
  uint16_t Address12, Address10, Address9;
  int ValueK, KeepExtraCode = 0;
  //int Operand;
  int16_t Operand16;
  int16_t CurrentEB, CurrentFB, CurrentBB;
  uint16_t ExtendedOpcode;
  int Overflow, Accumulator;
  //int OverflowQ, Qumulator;
  // Keep track of TC executions for the TC Trap alarm
  int ExecutedTC = 0;
  int JustTookBZF = 0;
  int JustTookBZMF = 0;


  sExtraCode = 0;
   
  // For DOWNRUPT
  if (State->DownruptTimeValid && State->CycleCounter >= State->DownruptTime)
    {
      State->InterruptRequests[8] = 1;	// Request DOWNRUPT
      State->DownruptTimeValid = 0;
    }

  // The first time through the loop, light up the DSKY RESTART light
  if (State->CycleCounter == 0)
    {
      State->RestartLight = 1;
    }

  State->CycleCounter++;

  //----------------------------------------------------------------------
  // The following little thing is useful only for debugging yaDEDA with
  // the --debug-deda command-line switch.  It just outputs the contents
  // of the address that was specified by the DEDA at 1/2 second intervals.
  if (DedaMonitor && State->CycleCounter >= DedaWhen)
    {
      int16_t Data;
      Data = State->Erasable[0][DedaAddress];
      DedaWhen = State->CycleCounter + 1024000 / 24;	// 1/2 second.
      ShiftToDeda (State, (DedaAddress >> 6) & 7);
      ShiftToDeda (State, (DedaAddress >> 3) & 7);
      ShiftToDeda (State, DedaAddress & 7);
      ShiftToDeda (State, 0);
      ShiftToDeda (State, (Data >> 12) & 7);
      ShiftToDeda (State, (Data >> 9) & 7);
      ShiftToDeda (State, (Data >> 6) & 7);
      ShiftToDeda (State, (Data >> 3) & 7);
      ShiftToDeda (State, Data & 7);
    }

  //----------------------------------------------------------------------
  // Update the thingy that determines when 1/1600 second has passed.
  // 1/1600 is the basic timing used to drive timer registers.  1/1600
  // second happens to be 160/3 machine cycles.

  State->ScalerCounter += SCALER_DIVIDER;
  State->DskyTimer += SCALER_DIVIDER;

  //-------------------------------------------------------------------------

  // Handle server stuff for socket connections used for i/o channel
  // communications.  Stuff like listening for clients we only do
  // every once and a while---nominally, every 100 ms.  Actually 
  // processing input data is done every cycle.
  if (State->ChannelRoutineCount == 0)
    ChannelRoutine (State);
  State->ChannelRoutineCount = ((State->ChannelRoutineCount + 1) & 017777);

  // Update the various hardware-driven DSKY lights
  UpdateDSKY(State);

  // Get data from input channels.  Return immediately if a unprogrammed 
  // counter-increment was performed.
  if (ChannelInput (State))
    return (0);

  // If in --debug-dsky mode, don't want to take the chance of executing
  // any AGC code, since there isn't any loaded anyway.
  if (DebugDsky)
    return (0);

  //----------------------------------------------------------------------  
  // This stuff takes care of extra CPU cycles used by some instructions.

  // A little extra delay, needed sometimes after branch instructions that
  // don't always take the same amount of time.
  if (State->ExtraDelay)
    {
      State->ExtraDelay--;
      return (0);
    }

  // If an instruction that takes more than one clock-cycle is in progress,
  // we simply return.  We don't do any of the actual computations for such
  // an instruction until the last clock cycle for it is reached.  
  // (Except for a few weird cases dealt with by ExtraDelay as above.) 
  if (State->PendFlag && State->PendDelay > 0)
    {
      State->PendDelay--;
      return (0);
    }

  //----------------------------------------------------------------------
  // Take care of any PCDU or MCDU operations that are lingering in CDU
  // FIFOs.
  if (ServiceCduFifo (State))
    {
      // A CDU counter was serviced, so a cycle was used up, and we must
      // return.  
      return (0);
    }

  if (State->InputChannel[032] & 020000)
    {
      State->SbyPressed = 0;
      State->SbyStillPressed = 0;
    }
   

  //----------------------------------------------------------------------
  // Here we take care of counter-timers.  There is a basic 1/3200 second
  // clock that is used to drive the timers.  1/3200 second happens to
  // be SCALER_OVERFLOW/SCALER_DIVIDER machine cycles, and the variable
  // ScalerCounter has already been updated the correct number of 
  // multiples of SCALER_DIVIDER.  Note that incrementing a timer register
  // takes 1 machine cycle.

  // This can only iterate once, but I use 'while' just in case.
  while (State->ScalerCounter >= SCALER_OVERFLOW)
    {
      int TriggeredAlarm = 0;

      // First, update SCALER1 and SCALER2. These are direct views into
      // the clock dividers in the Scaler module, and so don't take CPU
      // time to 'increment'
      State->ScalerCounter -= SCALER_OVERFLOW;
      State->InputChannel[ChanSCALER1]++;
      if (State->InputChannel[ChanSCALER1] == 040000)
        {
          State->InputChannel[ChanSCALER1] = 0;
          State->InputChannel[ChanSCALER2] = (State->InputChannel[ChanSCALER2] + 1) & 037777;
        }

      // Check alarms first, since there's a chance we might go to standby
      if (04000 == (07777 & State->InputChannel[ChanSCALER1]))
        {
          // The Night Watchman begins looking once every 1.28s
          if (!State->Standby)
            State->NightWatchman = 1;

          // The standby circuit finishes checking to see if we're going to standby now
          // (it has the same period as but is 180 degrees out of phase with the Night Watchman)
          if (State->SbyPressed && ((State->InputChannel[013] & 002000) || State->Standby))
            {
              if (!State->Standby)
                {
                  // Standby is enabled, and PRO has been held down for the required amount of time.
                  State->Standby = 1;
                  State->SbyStillPressed = 1;

                  // While this isn't technically an alarm, it causes GOJAM just like all the rest
                  TriggeredAlarm = 1;

                  // Turn on the STBY light, and switch off the EL segments
                  State->DskyChannel163 |= DSKY_STBY | DSKY_EL_OFF;
                  ChannelOutput(State, 0163, State->DskyChannel163);
                }
              else if (!State->SbyStillPressed)
                {
                  // PRO was pressed for long enough to turn us back on. Let's get going!
                  State->Standby = 0;

                  // Turn off the STBY light
                  State->DskyChannel163 &= ~(DSKY_STBY | DSKY_EL_OFF);
                  ChannelOutput(State, 0163, State->DskyChannel163);
                }
            }
        }
      else if (00000 == (07777 & State->InputChannel[ChanSCALER1]))
        {
          // The standby circuit checks the SBY/PRO button state every 1.28s
          if (0 == (State->InputChannel[032] & 020000))
            State->SbyPressed = 1;

          // The Night Watchman finishes looking now
          if (!State->Standby && State->NightWatchman)
            {
              // NEWJOB wasn't checked before 0.64s elapsed. Sound the alarm!
              TriggeredAlarm = 1;

              // Set the NIGHT WATCHMAN bit in channel 77. Don't go through CpuWriteIO() because
              // instructions writing to CH77 clear it. We'll broadcast changes to it in the
              // generic alarm handler a bit further down.
              State->InputChannel[077] |= CH77_NIGHT_WATCHMAN;
              State->NightWatchmanTripped = 1;
            }
          else
            // If it's been 1.28s since a Night Watchman alarm happened, stop asserting its
            // channel 77 bit
            State->NightWatchmanTripped = 0;
        }
      else if (00 == (07 & State->InputChannel[ChanSCALER1]))
        {
          // Update the warning filter. Once every 160ms, if an input to the filter has been
          // generated (or if the light test is active), the filter is charged. Otherwise,
          // it slowly discharges. This is being modeled as a simple linear function right now,
          // and should be updated when we learn its real implementation details.
          if ((0400 == (0777 & State->InputChannel[ChanSCALER1])) &&
              (State->GeneratedWarning || (State->InputChannel[013] & 01000)))
            {
              State->GeneratedWarning = 0;
              State->WarningFilter += WARNING_FILTER_INCREMENT;
              if (State->WarningFilter > WARNING_FILTER_MAX)
                State->WarningFilter = WARNING_FILTER_MAX;
            }
          else
            {
              if (State->WarningFilter >= WARNING_FILTER_DECREMENT)
                State->WarningFilter -= WARNING_FILTER_DECREMENT;
              else
                State->WarningFilter = 0;
            }
        }

      // All the rest of this is switched off during standby.
      if (!State->Standby)
        {
          if (0400 == (0777 & State->InputChannel[ChanSCALER1]))
            {
              // The Rupt Lock alarm watches ISR state starting every 160ms
              State->RuptLock = 1;
              State->NoRupt = 1;
            }
          else if ((State->RuptLock || State->NoRupt) && 0300 == (0777 & State->InputChannel[ChanSCALER1]))
            {
              // We've either had no interrupts, or stuck in one, for 140ms. Sound the alarm!
              TriggeredAlarm = 1;

              // Set the RUPT LOCK bit in channel 77.
              State->InputChannel[077] |= CH77_RUPT_LOCK;
            }

          if (020 == (037 & State->InputChannel[ChanSCALER1]))
            {
              // The TC Trap alarm watches executing instructions every 5ms
              State->TCTrap = 1;
              State->NoTC = 1;
            }
          else if ((State->TCTrap || State->NoTC) && 000 == (037 & State->InputChannel[ChanSCALER1]))
            {
              // We've either executed no TC at all, or only TCs, for the past 5ms. Sound the alarm!
              TriggeredAlarm = 1;

              // Set the TC TRAP bit in channel 77.
              State->InputChannel[077] |= CH77_TC_TRAP;
            }

          // Now that that's taken care of...
          // Update the 10 ms. timers TIME1 and TIME3.
          // Recall that the registers are in AGC integer format,
          // and therefore are actually shifted left one space.
          // When taking a reset, the real AGC would skip unprogrammed
          // sequences and go straight to GOJAM. The requests, however,
          // would be saved and the counts would happen immediately
          // after the first instruction at 4000, so doing them now
          // is not too inaccurate.
          if (020 == (037 & State->InputChannel[ChanSCALER1]))
	    {
	      State->ExtraDelay++;
	      if (CounterPINC (&c(RegTIME1)))
	        {
	          State->ExtraDelay++;
	          CounterPINC (&c(RegTIME2));
	        }
	      State->ExtraDelay++;
	      if (CounterPINC (&c(RegTIME3)))
	        State->InterruptRequests[3] = 1;
	    }
          // TIME5 is the same as TIME3, but 5 ms. out of phase.
          if (000 == (037 & State->InputChannel[ChanSCALER1]))
	    {
	      State->ExtraDelay++;
	      if (CounterPINC (&c(RegTIME5)))
	        State->InterruptRequests[2] = 1;
	    }
          // TIME4 is the same as TIME3, but 7.5ms out of phase
          if (010 == (037 & State->InputChannel[ChanSCALER1]))
	    {
	      State->ExtraDelay++;
	      if (CounterPINC (&c(RegTIME4)))
	        State->InterruptRequests[4] = 1;
	    }
          // TIME6 only increments when it has been enabled via CH13 bit 15.
          // It increments 0.3125ms after TIME1/TIME3
          if (040000 & State->InputChannel[013] && (State->InputChannel[ChanSCALER1] & 01) == 01)
            {
              State->ExtraDelay++;
              if (CounterDINC (State, 0, &c(RegTIME6)))
                {
	          State->InterruptRequests[1] = 1;
                  // Triggering a T6RUPT disables T6 by clearing the CH13 bit
                  CpuWriteIO(State, 013, State->InputChannel[013] & 037777);
                }
            }

          // Check for HANDRUPT conditions (the actually timing is very slightly off
          // from this, but not enough to matter). The traps are reset upon triggering.
          if (State->Trap31A && ((State->InputChannel[031] & 000077) != 000077))
            {
              State->Trap31A = 0;
              State->InterruptRequests[10] = 1;
            }

          if (State->Trap31B && ((State->InputChannel[031] & 007700) != 007700))
            {
              State->Trap31B = 0;
              State->InterruptRequests[10] = 1;
            }

          if (State->Trap32 && ((State->InputChannel[032] & 001777) != 001777))
            {
              State->Trap32 = 0;
              State->InterruptRequests[10] = 1;
            }
        }


      // If we triggered any alarms, simulate a GOJAM
      if (TriggeredAlarm || State->ParityFail)
        {
          if (!InhibitAlarms) // ...but only if doing so isn't inhibited
            {
              int i;

              // Two single-MCT instruction sequences, GOJAM and TC 4000, are about to happen
              State->ExtraDelay += 2;

              // The net result of those two is Z = 4000. Interrupt state is cleared, and
              // interrupts are enabled. The TC 4000 has the beneficial side-effect of
              // storing the current Z in Q, where it can helpfully be recovered.
              c(RegQ) = c(RegZ);
              c(RegZ) = 04000;
              State->InIsr = 0;
              State->AllowInterrupt = 1;
              State->ParityFail = 0;

              // HANDRUPT traps are all disabled.
              State->Trap31A = 0;
              State->Trap31B = 0;
              State->Trap32 = 0;

              // All interrupt requests are cleared.
              for (i = 1; i <= NUM_INTERRUPT_TYPES; i++)
                State->InterruptRequests[i] = 0;

              // Clear channels 5, 6, 10, 11, 12, 13, and 14
              CpuWriteIO(State, 005, 0);
              CpuWriteIO(State, 006, 0);
              CpuWriteIO(State, 010, 0);
              CpuWriteIO(State, 011, 0);
              CpuWriteIO(State, 012, 0);
              CpuWriteIO(State, 013, 0);
              CpuWriteIO(State, 014, 0);

              // Clear the UPLINK TOO FAST bit (11) in channel 33
              State->InputChannel[033] |= 002000;

              // Clear channels 34 and 35, and don't let doing so generate a downrupt
              CpuWriteIO(State, 034, 0);
              CpuWriteIO(State, 035, 0);
              State->DownruptTimeValid = 0;

              // Light the RESTART light on the DSKY, if we're not going into standby
              if (!State->Standby)
                {
                  State->RestartLight = 1;
                  State->GeneratedWarning = 1;
                }

            }

          // Push the CH77 updates to the outside world
          ChannelOutput (State, 077, State->InputChannel[077]);
        }


      if (State->ExtraDelay)
        {
          // Return, so as to account for the time occupied by updating the
          // counters and/or GOJAM.
          State->ExtraDelay--;
          return (0);
        }
    }

  // If we're in standby mode, this is all we can accomplish --
  // everything else is switched off.
  if (State->Standby)
    return (0);

  //----------------------------------------------------------------------
  // Same principle as for the counter-timers (above), but for handling 
  // the 3200 pulse-per-second fictitious register 0177 I use to support
  // driving the gyro.

#ifdef GYRO_TIMING_SIMULATED
  // Update the 3200 pps gyro pulse counter.
  GyroTimer += GYRO_DIVIDER;
  while (GyroTimer >= GYRO_OVERFLOW)
    {
      GyroTimer -= GYRO_OVERFLOW;
      // We get to this point 3200 times per second.  We increment the 
      // pulse count only if the GYRO ACTIVITY bit in channel 014 is set.
      if (0 != (State->InputChannel[014] & 01000) &&
	  State->Erasable[0][RegGYROCTR] > 0)
	{
	  GyroCount++;
	  State->Erasable[0][RegGYROCTR]--;
	  if (State->Erasable[0][RegGYROCTR] == 0)
	  State->InputChannel[014] &= ~01000;
	}
    }

  // If 1/4 second (nominal gyro pulse count of 800 decimal) or the gyro 
  // bits in channel 014 have changed, output to channel 0177.
  i = (State->InputChannel[014] & 01740);// Pick off the gyro bits.
  if (i != OldChannel14 || GyroCount >= 800)
    {
      j = ((OldChannel14 & 0740) << 6) | GyroCount;
      OldChannel14 = i;
      GyroCount = 0;
      ChannelOutput (State, 0177, j);
    }
#else // GYRO_TIMING_SIMULATED
#define GYRO_BURST 800
#define GYRO_BURST2 1024
  if (0 != (State->InputChannel[014] & 01000))
    if (0 != State->Erasable[0][RegGYROCTR])
      {
	// If any torquing is still pending, do it all at once before
	// setting up a new torque counter.
	while (GyroCount)
	  {
	    j = GyroCount;
	    if (j > 03777)
	      j = 03777;
	    ChannelOutput (State, 0177, OldChannel14 | j);
	    GyroCount -= j;
	  }
	// Set up new torque counter.
	GyroCount = State->Erasable[0][RegGYROCTR];
	State->Erasable[0][RegGYROCTR] = 0;
	OldChannel14 = ((State->InputChannel[014] & 0740) << 6);
	GyroTimer = GYRO_OVERFLOW * GYRO_BURST - GYRO_DIVIDER;
      }
  // Update the 3200 pps gyro pulse counter.
  GyroTimer += GYRO_DIVIDER;
  while (GyroTimer >= GYRO_BURST * GYRO_OVERFLOW)
    {
      GyroTimer -= GYRO_BURST * GYRO_OVERFLOW;
      if (GyroCount)
	{
	  j = GyroCount;
	  if (j > GYRO_BURST2)
	    j = GYRO_BURST2;
	  ChannelOutput (State, 0177, OldChannel14 | j);
	  GyroCount -= j;
	}
    }
#endif // GYRO_TIMING_SIMULATED

  //----------------------------------------------------------------------
  // ... and somewhat similar principles for the IMU CDU drive for 
  // coarse alignment.

#if 0  
  i = (State->InputChannel[014] & 070000);	// Check IMU CDU drive bits.
  if (ImuChannel14 == 0 && i != 0)// If suddenly active, start drive.
  ImuCduCount = IMUCDU_BURST_CYCLES;
  if (i != 0 && ImuCduCount >= IMUCDU_BURST_CYCLES)// Time for next burst.
    {
      // Adjust the cycle counter.
      ImuCduCount -= IMUCDU_BURST_CYCLES;
      // Determine how many pulses are wanted on each axis this burst.
      ImuChannel14 = BurstOutput (State, 040000, RegCDUXCMD, 0174);
      ImuChannel14 |= BurstOutput (State, 020000, RegCDUYCMD, 0175);
      ImuChannel14 |= BurstOutput (State, 010000, RegCDUZCMD, 0176);
    }
  else
  ImuCduCount++;
#else // 0
  i = (State->InputChannel[014] & 070000);	// Check IMU CDU drive bits.
  if (ImuChannel14 == 0 && i != 0)	// If suddenly active, start drive.
    ImuCduCount = State->CycleCounter - IMUCDU_BURST_CYCLES;
  if (i != 0 && (State->CycleCounter - ImuCduCount) >= IMUCDU_BURST_CYCLES) // Time for next burst.
    {
      // Adjust the cycle counter.
      ImuCduCount += IMUCDU_BURST_CYCLES;
      // Determine how many pulses are wanted on each axis this burst.
      ImuChannel14 = BurstOutput (State, 040000, RegCDUXCMD, 0174);
      ImuChannel14 |= BurstOutput (State, 020000, RegCDUYCMD, 0175);
      ImuChannel14 |= BurstOutput (State, 010000, RegCDUZCMD, 0176);
    }
#endif // 0

  //----------------------------------------------------------------------
  // Finally, stuff for driving the optics shaft & trunnion CDUs.  Nothing
  // fancy like the fine-alignment and coarse-alignment stuff above.
  // Just grab the data from the counter and dump it out the appropriate 
  // fictitious port as a giant lump.

  if (State->Erasable[0][RegOPTX] && 0 != (State->InputChannel[014] & 02000))
    {
      ChannelOutput (State, 0172, State->Erasable[0][RegOPTX]);
      State->Erasable[0][RegOPTX] = 0;
    }
  if (State->Erasable[0][RegOPTY] && 0 != (State->InputChannel[014] & 04000))
    {
      ChannelOutput (State, 0171, State->Erasable[0][RegOPTY]);
      State->Erasable[0][RegOPTY] = 0;
    }

  //----------------------------------------------------------------------  
  // Okay, here's the stuff that actually has to do with decoding instructions.

  // Store the current value of several registers.
  CurrentEB = c(RegEB);
  CurrentFB = c(RegFB);
  CurrentBB = c(RegBB);
  // Reform 16-bit accumulator and test for overflow in accumulator.
  Accumulator = c (RegA)& 0177777;
  Overflow = (ValueOverflowed (Accumulator) != AGC_P0);
  //Qumulator = GetQ (State);
  //OverflowQ = (ValueOverflowed (Qumulator) != AGC_P0);

  // After each instruction is executed, the AGC's Z register is updated to 
  // indicate the next instruction to be executed. The Z register is 16
  // bits long, but its value is transferred to the 12-bit S regsiter for
  // addressing, so the upper bits are lost.
  ProgramCounter = c(RegZ) & 07777;
  WhereWord = FindMemoryWord (State, ProgramCounter);

  // Fetch the instruction itself.
  //Instruction = *WhereWord;
  if (State->SubstituteInstruction)
    Instruction = c(RegBRUPT);
  else
    {
      // The index is sometimes positive and sometimes negative.  What to
      // do if the result has overflow, I can't say.  I arbitrarily 
      // overflow-correct it.
      Instruction = OverflowCorrected (
         AddSP16 (SignExtend (State->IndexValue), SignExtend (*WhereWord)));
    }
  Instruction &= 077777;

  sExtraCode = State->ExtraCode;

  ExtendedOpcode = Instruction >> 9;	//2;
  if (sExtraCode)
    ExtendedOpcode |= 0100;

  QuarterCode = Instruction & ~MASK10;
  Address12 = Instruction & MASK12;
  Address10 = Instruction & MASK10;
  Address9 = Instruction & MASK9;

  // Handle interrupts.
  if ((DebuggerInterruptMasks[0] && !State->InIsr && State->AllowInterrupt
     && !State->ExtraCode && !State->PendFlag && !Overflow 
     && Instruction != 3 && Instruction != 4 && Instruction != 6)
     || ExtendedOpcode == 0107) // Always check if the instruction is EDRUPT.
    {
      int i;
      int InterruptRequested = 0;
      // Interrupt vectors are ordered by their priority, with the lowest
      // address corresponding to the highest priority interrupt. Thus,
      // we can simply search through them in order for the next pending
      // request. There's two extra MCTs associated with taking an
      // interrupt -- one each for filling ZRUPT and BRUPT.
      // Search for the next interrupt request.
      for (i = 1; i <= NUM_INTERRUPT_TYPES; i++)
        {
          if (State->InterruptRequests[i] && DebuggerInterruptMasks[i])
            {
              // Clear the interrupt request.
              State->InterruptRequests[i] = 0;
              State->InterruptRequests[0] = i;

              State->NextZ = 04000 + 4 * i;

              InterruptRequested = 1;
              break;
            }
        }

      // If no pending interrupts and we're dealing with EDRUPT, fall
      // back to address 0 (A) as the interrupt vector
      if (!InterruptRequested && ExtendedOpcode == 0107)
        {
          State->NextZ = 0;
          InterruptRequested = 1;
        }

      if (InterruptRequested)
        {
          BacktraceAdd (State, i);
          // Set up the return stuff.
          c (RegZRUPT)= ProgramCounter + 1;
          c (RegBRUPT)= Instruction;
          // Clear various metadata. Extracode is cleared (this can only
          // really happen with EDRUPT), and the index value and substituted
          // instruction were both applied earlier and their effects were
          // saved in BRUPT.
          State->ExtraCode = 0;
          State->IndexValue = AGC_P0;
          State->SubstituteInstruction = 0;
          // Vector to the interrupt.
          State->InIsr = 1;
          State->ExtraDelay++;
          goto AllDone;
        }
    }

  // Add delay for multi-MCT instructions.  Works for all instructions 
  // except EDRUPT, BZF, and BZMF.  For BZF and BZMF, an extra cycle is added
  // AFTER executing the instruction -- not because it's more logically
  // correct, just because it's easier. EDRUPT's timing is handled with
  // the interrupt logic.
  if (!State->PendFlag)
    {
      int i;
      i = QuarterCode >> 10;
      if (State->ExtraCode)
	i = ExtracodeTiming[i];
      else
	i = InstructionTiming[i];
      if (i)
	{
	  State->PendFlag = 1;
	  State->PendDelay = i-1;
	  return (0);
	}
    }
  else
    State->PendFlag = 0;

  // Now that the index value has been used, get rid of it.
  State->IndexValue = AGC_P0;
  // And similarly for the substitute instruction from a RESUME.
  State->SubstituteInstruction = 0;

  // Compute the next value of the instruction pointer. The Z register is
  // 16 bits long, even though in almost all cases only the lower 12 bits
  // are used. When the Z register is incremented between each instruction,
  // only the lower 12 bits are read into the adder, so if something sets
  // any of the 4 most significant bits of Z, they will be lost before
  // the next instruction sees them.
  State->NextZ = 1 + c(RegZ);
  // The contents of the Z register are updated before an instruction is
  // executed (really, it happens at the end of the previous instruction).
  c (RegZ)= State->NextZ;

  // A BZF followed by an instruction other than EXTEND causes a TCF0 transient
  if (State->TookBZF && !((ExtendedOpcode == 000) && (Address12 == 6)))
    ExecutedTC = 1;

  // Parse the instruction.  Refer to p.34 of 1689.pdf for an easy 
  // picture of what follows.
  switch (ExtendedOpcode)
    {
    case 000:			// TC.  
    case 001:
    case 002:
    case 003:
    case 004:
    case 005:
    case 006:
    case 007:
      // TC instruction (1 MCT).
      ValueK = Address12;// Convert AGC numerical format to native CPU format.
      if (ValueK == 3)		// RELINT instruction.
        {
	  State->AllowInterrupt = 1;

          if (State->TookBZF || State->TookBZMF)
            // RELINT after a single-cycle instruction causes a TC0 transient
            ExecutedTC = 1;
        }
      else if (ValueK == 4)	// INHINT instruction.
        {
	  State->AllowInterrupt = 0;

          if (State->TookBZF || State->TookBZMF)
            // INHINT after a single-cycle instruction causes a TC0 transient
            ExecutedTC = 1;
        }
      else if (ValueK == 6)	// EXTEND instruction.
	{
	  State->ExtraCode = 1;
	  // Normally, ExtraCode will be reset when agc_engine is finished.
	  // We inhibit that behavior with this flag.
	  KeepExtraCode = 1;
	}
      else
	{
	  BacktraceAdd (State, 0);
	  if (ValueK != RegQ)	// If not a RETURN instruction ...
	    c (RegQ)= 0177777 & State->NextZ;
	  State->NextZ = Address12;
          ExecutedTC = 1;
	}

      break;
    case 010:			// CCS. 
    case 011:
      // CCS instruction (2 MCT).  
      // Figure out where the data is stored, and fetch it.
      if (Address10 < REG16)
	{
	  ValueK = 0177777 & c(Address10);
	  Operand16 = OverflowCorrected (ValueK);
	  c (RegA)= odabs (ValueK);
	}
      else			// K!=accumulator.
	{
	  WhereWord = FindMemoryWord (State, Address10);
	  Operand16 = *WhereWord & 077777;
	  // Compute the "diminished absolute value", and save in accumulator.
	  c (RegA) = dabs (Operand16);
	  // Assign back the read data in case editing is needed
	  AssignFromPointer (State, WhereWord, Operand16);
	}
      // Now perform the actual comparison and jump on the basis
      // of it.  There's no explanation I can find as to what
      // happens if we're already at the end of the memory bank,
      // so I'll just pretend that that can't happen.  Note, 
      // by the way, that if the Operand is > +0, then NextZ
      // is already correct, and in the other cases we need to
      // increment it by 2 less because NextZ has already been 
      // incremented.
      if (Address10 < REG16
	  && ValueOverflowed (ValueK) == AGC_P1)
      State->NextZ += 0;
      else if (Address10 < REG16
	  && ValueOverflowed (ValueK) == AGC_M1)
      State->NextZ += 2;
      else if (Operand16 == AGC_P0)
      State->NextZ += 1;
      else if (Operand16 == AGC_M0)
      State->NextZ += 3;
      else if (0 != (Operand16 & 040000))
      State->NextZ += 2;
      break;
      case 012:// TCF.
      case 013:
      case 014:
      case 015:
      case 016:
      case 017:
      BacktraceAdd (State, 0);
      // TCF instruction (1 MCT).
      State->NextZ = Address12;
      // THAT was easy ... too easy ...
      ExecutedTC = 1;
      break;
      case 020:// DAS.
      case 021:
      //DasInstruction:
      // DAS instruction (3 MCT).  
	{
	  // We add the less-significant words (as SP values), and thus
	  // the sign of the lower word of the output does not necessarily
	  // match the sign of the upper word.
	  int Msw, Lsw;
	  if (IsL (Address10))// DDOUBL
	    {
	      Lsw = AddSP16 (0177777 & c (RegL), 0177777 & c (RegL));
	      Msw = AddSP16 (Accumulator, Accumulator);
	      if ((0140000 & Lsw) == 0040000)
	      Msw = AddSP16 (Msw, AGC_P1);
	      else if ((0140000 & Lsw) == 0100000)
	      Msw = AddSP16 (Msw, SignExtend (AGC_M1));
	      Lsw = OverflowCorrected (Lsw);
	      c (RegA) = 0177777 & Msw;
	      c (RegL) = 0177777 & SignExtend (Lsw);
	      break;
	    }
	  WhereWord = FindMemoryWord (State, Address10);
	  if (Address10 < REG16)
	  Lsw = AddSP16 (0177777 & c (RegL), 0177777 & c (Address10));
	  else
	  Lsw = AddSP16 (0177777 & c (RegL), SignExtend (*WhereWord));
	  if (Address10 < REG16 + 1)
	  Msw = AddSP16 (Accumulator, 0177777 & c (Address10 - 1));
	  else
	  Msw = AddSP16 (Accumulator, SignExtend (WhereWord[-1]));

	  if ((0140000 & Lsw) == 0040000)
	  Msw = AddSP16 (Msw, AGC_P1);
	  else if ((0140000 & Lsw) == 0100000)
	  Msw = AddSP16 (Msw, SignExtend (AGC_M1));
	  Lsw = OverflowCorrected (Lsw);

	  if ((0140000 & Msw) == 0100000)
	  c (RegA) = SignExtend (AGC_M1);
	  else if ((0140000 & Msw) == 0040000)
	  c (RegA) = AGC_P1;
	  else
	  c (RegA) = AGC_P0;
	  c (RegL) = AGC_P0;
	  // Save the results.
	  if (Address10 < REG16)
	  c (Address10) = SignExtend (Lsw);
	  else
	  AssignFromPointer (State, WhereWord, Lsw);
	  if (Address10 < REG16 + 1)
	  c (Address10 - 1) = Msw;
	  else
	  AssignFromPointer (State, WhereWord - 1, OverflowCorrected (Msw));
	}
      break;
      case 022:			// LXCH.
      case 023:
      // "LXCH K" instruction (2 MCT). 
      if (IsL (Address10))
      break;
      if (IsReg (Address10, RegZERO))// ZL
      c (RegL) = AGC_P0;
      else if (Address10 < REG16)
	{
	  Operand16 = c (RegL);
	  c (RegL) = c (Address10);
	  if (Address10 >= 020 && Address10 <= 023)
	  AssignFromPointer (State, WhereWord,
	      OverflowCorrected (0177777 & Operand16));
	  else
	  c (Address10) = Operand16;
	  if (Address10 == RegZ)
	  State->NextZ = c (RegZ);
	}
      else
	{
	  WhereWord = FindMemoryWord (State, Address10);
	  Operand16 = *WhereWord;
	  AssignFromPointer (State, WhereWord,
	      OverflowCorrected (0177777 & c (RegL)));
	  c (RegL) = SignExtend (Operand16);
	}
      break;
      case 024:			// INCR.
      case 025:
      // INCR instruction (2 MCT).
	{
	  int Sum;
	  WhereWord = FindMemoryWord (State, Address10);
	  if (Address10 < REG16)
	  c (Address10) = AddSP16 (AGC_P1, 0177777 & c (Address10));
	  else
	    {
	      Sum = AddSP16 (AGC_P1, SignExtend (*WhereWord));
	      AssignFromPointer (State, WhereWord, OverflowCorrected (Sum));
	      InterruptRequests (State, Address10, Sum);
	    }
	}
      break;
      case 026:			// ADS.  Reviewed against Blair-Smith.
      case 027:
      // ADS instruction (2 MCT).
	{
	  WhereWord = FindMemoryWord (State, Address10);
	  if (IsA (Address10))
	  Accumulator = AddSP16 (Accumulator, Accumulator);
	  else if (Address10 < REG16)
	  Accumulator = AddSP16 (Accumulator, 0177777 & c (Address10));
	  else
	  Accumulator = AddSP16 (Accumulator, SignExtend (*WhereWord));
	  c (RegA) = Accumulator;
	  if (IsA (Address10))
	    {
	    }
	  else if (Address10 < REG16)
	  c (Address10) = Accumulator;
	  else
	  AssignFromPointer (State, WhereWord,
	      OverflowCorrected (Accumulator));
	}
      break;
      case 030:			// CA
      case 031:
      case 032:
      case 033:
      case 034:
      case 035:
      case 036:
      case 037:
      if (IsA (Address12))// NOOP
      break;
      if (Address12 < REG16)
	{
	  c (RegA) = c (Address12);;
	  break;
	}
      WhereWord = FindMemoryWord (State, Address12);
      c (RegA) = SignExtend (*WhereWord);
      AssignFromPointer (State, WhereWord, *WhereWord);
      break;
      case 040:			// CS
      case 041:
      case 042:
      case 043:
      case 044:
      case 045:
      case 046:
      case 047:
      ExecutedTC = 1; // CS causes transients on the TC0 line

      if (IsA (Address12))// COM
	{
	  c (RegA) = ~Accumulator;;
	  break;
	}
      if (Address12 < REG16)
	{
	  c (RegA) = ~c (Address12);
	  break;
	}
      WhereWord = FindMemoryWord (State, Address12);
      c (RegA) = SignExtend (NegateSP (*WhereWord));
      AssignFromPointer (State, WhereWord, *WhereWord);
      break;
      case 050:			// INDEX
      case 051:
      if (Address10 == 017)
      goto Resume;
      if (Address10 < REG16)
      State->IndexValue = OverflowCorrected (c (Address10));
      else
	{
	  WhereWord = FindMemoryWord (State, Address10);
	  State->IndexValue = *WhereWord;
	}
      break;
      case 0150:			// INDEX (continued)
      case 0151:
      case 0152:
      case 0153:
      case 0154:
      case 0155:
      case 0156:
      case 0157:
      if (Address12 == 017 << 1)
	{
	  Resume:
	  if (State->InIsr)
	  BacktraceAdd (State, 255);
	  else
	  BacktraceAdd (State, 0);
	  State->NextZ = c (RegZRUPT) - 1;
	  State->InIsr = 0;
	  State->SubstituteInstruction = 1;
	}
      else
	{
	  if (Address12 < REG16)
	  State->IndexValue = OverflowCorrected (c (Address12));
	  else
	    {
	      WhereWord = FindMemoryWord (State, Address12);
	      State->IndexValue = *WhereWord;
	    }
	  KeepExtraCode = 1;
	}
      break;
      case 052:			// DXCH
      case 053:
      ExecutedTC = 1; // DXCH causes transients on the TCF0 line

      // Remember, in the following comparisons, that the address is pre-incremented.
      if (IsL (Address10))
	{
	  c (RegL) = SignExtend (OverflowCorrected (c (RegL)));
	  break;
	}
      WhereWord = FindMemoryWord (State, Address10);
      // Topmost word.
      if (Address10 < REG16)
	{
	  Operand16 = c (Address10);
	  c (Address10) = c (RegL);
	  c (RegL) = Operand16;
	  if (Address10 == RegZ)
	  State->NextZ = c (RegZ);
	}
      else
	{
	  Operand16 = SignExtend (*WhereWord);
	  AssignFromPointer (State, WhereWord, OverflowCorrected (c (RegL)));
	  c (RegL) = Operand16;
	}
      c (RegL) = SignExtend (OverflowCorrected (c (RegL)));
      // Bottom word.
      if (Address10 < REG16 + 1)
	{
	  Operand16 = c (Address10 - 1);
	  c (Address10 - 1) = c (RegA);
	  c (RegA) = Operand16;
	  if (Address10 == RegZ + 1)
	  State->NextZ = c (RegZ);
	}
      else
	{
	  Operand16 = SignExtend (WhereWord[-1]);
	  AssignFromPointer (State, WhereWord - 1,
	      OverflowCorrected (c (RegA)));
	  c (RegA) = Operand16;
	}
      break;
      case 054:			// TS
      case 055:
      ExecutedTC = 1; // TS causes transients on the TCF0 line
      if (IsA (Address10))// OVSK
	{
	  if (Overflow)
	  State->NextZ += AGC_P1;
	}
      else if (IsZ (Address10))	// TCAA
	{
	  State->NextZ = (077777 & Accumulator);
	  if (Overflow)
	  c (RegA) = SignExtend (ValueOverflowed (Accumulator));
	}
      else			// Not OVSK or TCAA.
	{
	  WhereWord = FindMemoryWord (State, Address10);
	  if (Address10 < REG16)
	  c (Address10) = Accumulator;
	  else
	  AssignFromPointer (State, WhereWord,
	      OverflowCorrected (Accumulator));
	  if (Overflow)
	    {
	      c (RegA) = SignExtend (ValueOverflowed (Accumulator));
	      State->NextZ += AGC_P1;
	    }
	}
      break;
      case 056:			// XCH
      case 057:
      ExecutedTC = 1; // XCH causes transients on the TCF0 line
      if (IsA (Address10))
      break;
      if (Address10 < REG16)
	{
	  c (RegA) = c (Address10);
	  c (Address10) = Accumulator;
	  if (Address10 == RegZ)
	  State->NextZ = c (RegZ);
	  break;
	}
      WhereWord = FindMemoryWord (State, Address10);
      c (RegA) = SignExtend (*WhereWord);
      AssignFromPointer (State, WhereWord, OverflowCorrected (Accumulator));
      break;
      case 060:			// AD
      case 061:
      case 062:
      case 063:
      case 064:
      case 065:
      case 066:
      case 067:
      if (IsA (Address12))// DOUBLE
      Accumulator = AddSP16 (Accumulator, Accumulator);
      else if (Address12 < REG16)
      Accumulator = AddSP16 (Accumulator, 0177777 & c (Address12));
      else
	{
	  WhereWord = FindMemoryWord (State, Address12);
	  Accumulator = AddSP16 (Accumulator, SignExtend (*WhereWord));
	  AssignFromPointer (State, WhereWord, *WhereWord);
	}
      c (RegA) = Accumulator;
      break;
      case 070:			// MASK
      case 071:
      case 072:
      case 073:
      case 074:
      case 075:
      case 076:
      case 077:
      if (Address12 < REG16)
      c (RegA) = Accumulator & c (Address12);
      else
	{
	  c (RegA) = OverflowCorrected (Accumulator);
	  WhereWord = FindMemoryWord (State, Address12);
	  c (RegA) = SignExtend (c (RegA) & *WhereWord);
	}
      break;
      case 0100:			// READ
      if (IsL (Address9) || IsQ (Address9))
      c (RegA) = c (Address9);
      else
      c (RegA) = SignExtend (ReadIO (State, Address9));
      break;
      case 0101:// WRITE
      if (IsL (Address9) || IsQ (Address9))
      c (Address9) = Accumulator;
      else
      CpuWriteIO (State, Address9, OverflowCorrected (Accumulator));
      break;
      case 0102:// RAND
      if (IsL (Address9) || IsQ (Address9))
      c (RegA) = (Accumulator & c (Address9));
      else
	{
	  Operand16 = OverflowCorrected (Accumulator);
	  Operand16 &= ReadIO (State, Address9);
	  c (RegA) = SignExtend (Operand16);
	}
      break;
      case 0103:			// WAND
      if (IsL (Address9) || IsQ (Address9))
      c (RegA) = c (Address9) = (Accumulator & c (Address9));
      else
	{
	  Operand16 = OverflowCorrected (Accumulator);
	  Operand16 &= ReadIO (State, Address9);
	  CpuWriteIO (State, Address9, Operand16);
	  c (RegA) = SignExtend (Operand16);
	}
      break;
      case 0104:			// ROR
      if (IsL (Address9) || IsQ (Address9))
      c (RegA) = (Accumulator | c (Address9));
      else
	{
	  Operand16 = OverflowCorrected (Accumulator);
	  Operand16 |= ReadIO (State, Address9);
	  c (RegA) = SignExtend (Operand16);
	}
      break;
      case 0105:			// WOR
      if (IsL (Address9) || IsQ (Address9))
      c (RegA) = c (Address9) = (Accumulator | c (Address9));
      else
	{
	  Operand16 = OverflowCorrected (Accumulator);
	  Operand16 |= ReadIO (State, Address9);
	  CpuWriteIO (State, Address9, Operand16);
	  c (RegA) = SignExtend (Operand16);
	}
      break;
      case 0106:			// RXOR
      if (IsL (Address9) || IsQ (Address9))
      c (RegA) = (Accumulator ^ c (Address9));
      else
	{
	  Operand16 = OverflowCorrected (Accumulator);
	  Operand16 ^= ReadIO (State, Address9);
	  c (RegA) = SignExtend (Operand16);
	}
      break;
      case 0107:			// EDRUPT
      // It shouldn't be possible to get here, since EDRUPT is treated
      // as an interrupt above.
      break;
      case 0110:			// DV
      case 0111:
	{
	  int16_t AccPair[2], AbsA, AbsL, AbsK, Div16;
	  int Dividend, Divisor, Quotient, Remainder;

	  AccPair[0] = OverflowCorrected (Accumulator);
	  AccPair[1] = c (RegL);
	  Dividend = SpToDecent (&AccPair[1]);
	  DecentToSp (Dividend, &AccPair[1]);
	  // Check boundary conditions.
	  AbsA = AbsSP (AccPair[0]);
	  AbsL = AbsSP (AccPair[1]);

	  if (IsA (Address10))
	    {
	      // DV modifies A before reading the divisor, so in this
	      // case the divisor is -|A|.
              Div16 = c(RegA);
	      if ((c(RegA) & 0100000) == 0)
	        Div16 = 0177777 & ~Div16;
	    }
          else if (IsL (Address10))
	    {
	      // DV modifies L before reading the divisor. L is first
	      // negated if the quotient A,L is negative according to
	      // DV sign rules. Then, 40000 is added to it.
	      Div16 = c(RegL);
              if (((AbsA == 0) && (0100000 & c(RegL))) || ((AbsA != 0) && (0100000 & c(RegA))))
	        Div16 = 0177777 & ~Div16;
              // Make sure to account for L's built-in overflow correction
              Div16 = SignExtend(OverflowCorrected(AddSP16((uint16_t)Div16, 040000)));
	    }
          else if (IsZ (Address10))
	    {
	      // DV modifies Z before reading the divisor. If the
	      // quotient A,L is negative according to DV sign rules,
	      // Z16 is set.
	      Div16 = c(RegZ);
              if (((AbsA == 0) && (0100000 & c(RegL))) || ((AbsA != 0) && (0100000 & c(RegA))))
	        Div16 |= 0100000;
	    }
	  else if (Address10 < REG16)
	    Div16 = c(Address10);
	  else
            Div16 = SignExtend(*FindMemoryWord(State, Address10));

	  // Fetch the values;
	  AbsK = AbsSP(OverflowCorrected(Div16));
	  if (AbsA > AbsK || (AbsA == AbsK && AbsL != AGC_P0) || ValueOverflowed(Div16) != AGC_P0)
	    {
	      // The divisor is smaller than the dividend, or the divisor has
	      // overflow. In both cases, we fall back on a slower simulation
	      // of the hardware registers, which will produce "total nonsense"
	      // (that nonetheless will match what the actual AGC would have gotten).
              SimulateDV(State, Div16);
	    }
	  else if (AbsA == 0 && AbsL == 0)
	    {
	      // The dividend is 0 but the divisor is not. The standard DV sign
	      // convention applies to A, and L remains unchanged.
	      if ((040000 & c (RegL)) == (040000 & OverflowCorrected(Div16)))
                {
                  if (AbsK == 0) Operand16 = 037777;	// Max positive value.
                  else Operand16 = AGC_P0;
                }
	      else
                {
                  if (AbsK == 0) Operand16 = (077777 & ~037777);	// Max negative value.
                  else Operand16 = AGC_M0;
                }

	      c (RegA) = SignExtend (Operand16);
	    }
	  else if (AbsA == AbsK && AbsL == AGC_P0)
	    {
	      // The divisor is equal to the dividend.
	      if (AccPair[0] == OverflowCorrected(Div16))// Signs agree?
		{
		  Operand16 = 037777;	// Max positive value.
		}
	      else
		{
		  Operand16 = (077777 & ~037777);	// Max negative value.
		}
	      c (RegL) = SignExtend(AccPair[0]);
	      c (RegA) = SignExtend(Operand16);
	    }
	  else
	    {
	      // The divisor is larger than the dividend.  Okay to actually divide!
	      // Fortunately, the sign conventions agree with those of the normal
	      // C operators / and %, so all we need to do is to convert the
	      // 1's-complement values to native CPU format to do the division,
	      // and then convert back afterward.  Incidentally, we know we
	      // aren't dividing by zero, since we know that the divisor is
	      // greater (in magnitude) than the dividend.
	      Dividend = agc2cpu2 (Dividend);
	      Divisor = agc2cpu (OverflowCorrected(Div16));
	      Quotient = Dividend / Divisor;
	      Remainder = Dividend % Divisor;
	      c (RegA) = SignExtend (cpu2agc (Quotient));
	      if (Remainder == 0)
		{
		  // In this case, we need to make an extra effort, because we
		  // might need -0 rather than +0.
		  if (Dividend >= 0)
		  c (RegL) = AGC_P0;
		  else
		  c (RegL) = SignExtend (AGC_M0);
		}
	      else
	      c (RegL) = SignExtend (cpu2agc (Remainder));
	    }
	}
      break;
      case 0112:			// BZF
      case 0113:
      case 0114:
      case 0115:
      case 0116:
      case 0117:
      //Operand16 = OverflowCorrected (Accumulator);
      //if (Operand16 == AGC_P0 || Operand16 == AGC_M0)
      if (Accumulator == 0 || Accumulator == 0177777)
	{
	  BacktraceAdd (State, 0);
	  State->NextZ = Address12;
          JustTookBZF = 1;
	}
      break;
      case 0120:			// MSU
      case 0121:
	{
	  unsigned ui, uj;
	  int diff;
	  WhereWord = FindMemoryWord (State, Address10);
	  if (Address10 < REG16)
	    {
	      ui = 0177777 & Accumulator;
	      uj = 0177777 & ~c (Address10);
	    }
	  else
	    {
	      ui = (077777 & OverflowCorrected (Accumulator));
	      uj = (077777 & ~*WhereWord);
	    }
	  diff = ui + uj + 1; // Two's complement subtraction -- add the complement plus one
	  // The AGC sign-extends the result from A15 to A16, then checks A16 to see if
	  // one needs to be subtracted. We'll go in the opposite order, which also works
	  if (diff & 040000)
	    {
	      diff |= 0100000; // Sign-extend A15 into A16
	      diff--; // Subtract one from the result
	    }
	  if (IsQ (Address10))
	  c (RegA) = 0177777 & diff;
	  else
	    {
	      Operand16 = (077777 & diff);
	      c (RegA) = SignExtend (Operand16);
	    }
	  if (Address10 >= 020 && Address10 <= 023)
	  AssignFromPointer (State, WhereWord, *WhereWord);
	}
      break;
      case 0122:			// QXCH
      case 0123:
      if (IsQ (Address10))
      break;
      if (IsReg (Address10, RegZERO))// ZQ
      c (RegQ) = AGC_P0;
      else if (Address10 < REG16)
	{
	  Operand16 = c (RegQ);
	  c (RegQ) = c (Address10);
	  c (Address10) = Operand16;
	  if (Address10 == RegZ)
	  State->NextZ = c (RegZ);
	}
      else
	{
	  WhereWord = FindMemoryWord (State, Address10);
	  Operand16 = OverflowCorrected (c (RegQ));
	  c (RegQ) = SignExtend (*WhereWord);
	  AssignFromPointer (State, WhereWord, Operand16);
	}
      break;
      case 0124:			// AUG
      case 0125:
	{
	  int Sum;
	  int Operand16, Increment;
	  WhereWord = FindMemoryWord (State, Address10);
	  if (Address10 < REG16)
	  Operand16 = c (Address10);
	  else
	  Operand16 = SignExtend (*WhereWord);
	  Operand16 &= 0177777;
	  if (0 == (0100000 & Operand16))
	  Increment = AGC_P1;
	  else
	  Increment = SignExtend (AGC_M1);
	  Sum = AddSP16 (0177777 & Increment, 0177777 & Operand16);
	  if (Address10 < REG16)
	  c (Address10) = Sum;
	  else
	    {
	      AssignFromPointer (State, WhereWord, OverflowCorrected (Sum));
	      InterruptRequests (State, Address10, Sum);
	    }
	}
      break;
      case 0126:			// DIM
      case 0127:
	{
	  int Sum;
	  int Operand16, Increment;
	  WhereWord = FindMemoryWord (State, Address10);
	  if (Address10 < REG16)
	  Operand16 = c (Address10);
	  else
	  Operand16 = SignExtend (*WhereWord);
	  Operand16 &= 0177777;
	  if (Operand16 == AGC_P0 || Operand16 == SignExtend (AGC_M0))
	  break;
	  if (0 == (0100000 & Operand16))
	  Increment = SignExtend (AGC_M1);
	  else
	  Increment = AGC_P1;
	  Sum = AddSP16 (0177777 & Increment, 0177777 & Operand16);
	  if (Address10 < REG16)
	  c (Address10) = Sum;
	  else
	  AssignFromPointer (State, WhereWord, OverflowCorrected (Sum));
	}
      break;
      case 0130:			// DCA
      case 0131:
      case 0132:
      case 0133:
      case 0134:
      case 0135:
      case 0136:
      case 0137:
      if (IsL (Address12))
	{
	  c (RegL) = SignExtend (OverflowCorrected (c (RegL)));
	  break;
	}
      WhereWord = FindMemoryWord (State, Address12);
      // Do topmost word first.
      if (Address12 < REG16)
      c (RegL) = c (Address12);
      else
      c (RegL) = SignExtend (*WhereWord);
      c (RegL) = SignExtend (OverflowCorrected (c (RegL)));
      // Now do bottom word.
      if (Address12 < REG16 + 1)
      c (RegA) = c (Address12 - 1);
      else
      c (RegA) = SignExtend (WhereWord[-1]);
      if (Address12 >= 020 && Address12 <= 023)
      AssignFromPointer (State, WhereWord, WhereWord[0]);
      if (Address12 >= 020 + 1 && Address12 <= 023 + 1)
      AssignFromPointer (State, WhereWord - 1, WhereWord[-1]);
      break;
      case 0140:// DCS
      case 0141:
      case 0142:
      case 0143:
      case 0144:
      case 0145:
      case 0146:
      case 0147:
      if (IsL (Address12))// DCOM
	{
	  c (RegA) = ~Accumulator;
	  c (RegL) = ~c (RegL);
	  c (RegL) = SignExtend (OverflowCorrected (c (RegL)));
	  break;
	}
      WhereWord = FindMemoryWord (State, Address12);
      // Do topmost word first.
      if (Address12 < REG16)
      c (RegL) = ~c (Address12);
      else
      c (RegL) = ~SignExtend (*WhereWord);
      c (RegL) = SignExtend (OverflowCorrected (c (RegL)));
      // Now do bottom word.
      if (Address12 < REG16 + 1)
      c (RegA) = ~c (Address12 - 1);
      else
      c (RegA) = ~SignExtend (WhereWord[-1]);
      if (Address12 >= 020 && Address12 <= 023)
      AssignFromPointer (State, WhereWord, WhereWord[0]);
      if (Address12 >= 020 + 1 && Address12 <= 023 + 1)
      AssignFromPointer (State, WhereWord - 1, WhereWord[-1]);
      break;
      // For 0150..0157 see the INDEX instruction above.
      case 0160:// SU
      case 0161:
      if (IsA (Address10))
      Accumulator = SignExtend (AGC_M0);
      else if (Address10 < REG16)
      Accumulator = AddSP16 (Accumulator, 0177777 & ~c (Address10));
      else
	{
	  WhereWord = FindMemoryWord (State, Address10);
	  Accumulator =
	  AddSP16 (Accumulator, SignExtend (NegateSP (*WhereWord)));
	  AssignFromPointer (State, WhereWord, *WhereWord);
	}
      c (RegA) = Accumulator;
      break;
      case 0162:			// BZMF
      case 0163:
      case 0164:
      case 0165:
      case 0166:
      case 0167:
      //Operand16 = OverflowCorrected (Accumulator);
      //if (Operand16 == AGC_P0 || IsNegativeSP (Operand16))
      if (Accumulator == 0 || 0 != (Accumulator & 0100000))
	{
	  BacktraceAdd (State, 0);
	  State->NextZ = Address12;
          JustTookBZMF = 1;
	}
      break;
      case 0170:			// MP
      case 0171:
      case 0172:
      case 0173:
      case 0174:
      case 0175:
      case 0176:
      case 0177:
	{
	  // For MP A (i.e., SQUARE) the accumulator is NOT supposed to
	  // be overflow-corrected.  I do it anyway, since I don't know
	  // what it would mean to carry out the operation otherwise.
	  // Fix later if it causes a problem.
	  // FIX ME: Accumulator is overflow-corrected before SQUARE.
	  int16_t MsWord, LsWord, OtherOperand16;
	  int Product;
	  WhereWord = FindMemoryWord (State, Address12);
	  Operand16 = OverflowCorrected (Accumulator);
	  if (Address12 < REG16)
	  OtherOperand16 = OverflowCorrected (c (Address12));
	  else
	  OtherOperand16 = *WhereWord;
	  if (OtherOperand16 == AGC_P0 || OtherOperand16 == AGC_M0)
	  MsWord = LsWord = AGC_P0;
	  else if (Operand16 == AGC_P0 || Operand16 == AGC_M0)
	    {
	      if ((Operand16 == AGC_P0 && 0 != (040000 & OtherOperand16)) ||
		  (Operand16 == AGC_M0 && 0 == (040000 & OtherOperand16)))
	      MsWord = LsWord = AGC_M0;
	      else
	      MsWord = LsWord = AGC_P0;
	    }
	  else
	    {
	      int16_t WordPair[2];
	      Product =
	      agc2cpu (SignExtend (Operand16)) *
	      agc2cpu (SignExtend (OtherOperand16));
	      Product = cpu2agc2 (Product);
	      // Sign-extend, because it's needed for DecentToSp.
	      if (02000000000 & Product)
	      Product |= 004000000000;
	      // Convert back to DP.
	      DecentToSp (Product, &WordPair[1]);
	      MsWord = WordPair[0];
	      LsWord = WordPair[1];
	    }
	  c (RegA) = SignExtend (MsWord);
	  c (RegL) = SignExtend (LsWord);
	}
      break;
      default:
      // Isn't possible to get here, but still ...
      //printf ("Unrecognized instruction %06o.\n", Instruction);
      break;
    }

  AllDone:
  // All done!
  if (!State->PendFlag)
    {
      c (RegZERO)= AGC_P0;
      State->InputChannel[7] = State->OutputChannel7 &= 0160;
      c (RegZ) = State->NextZ;
      // In all cases except for RESUME, Z will be truncated to
      // 12 bits between instructions
      if (!State->SubstituteInstruction)
        c (RegZ) = c(RegZ) & 07777;
      if (!KeepExtraCode)
      State->ExtraCode = 0;
      // Values written to EB and FB are automatically mirrored to BB,
      // and vice versa.
      if (CurrentBB != c (RegBB))
	{
	  c (RegFB) = (c (RegBB) & 076000);
	  c (RegEB) = (c (RegBB) & 07) << 8;
	}
      else if (CurrentEB != c (RegEB) || CurrentFB != c (RegFB))
      c (RegBB) = (c (RegFB) & 076000) | ((c (RegEB) & 03400) >> 8);
      c (RegEB) &= 03400;
      c (RegFB) &= 076000;
      c (RegBB) &= 076007;
      // Correct overflow in the L register (this is done on read in the original,
      // but is much easier here)
      c(RegL) = SignExtend (OverflowCorrected (c(RegL)));

      // Check ISR status, and clear the Rupt Lock flags accordingly
      if (State->InIsr) State->NoRupt = 0;
      else State->RuptLock = 0;

      // Update TC Trap flags according to the instruction we just executed
      if (ExecutedTC) State->NoTC = 0;
      else State->TCTrap = 0;

      State->TookBZF = JustTookBZF;
      State->TookBZMF = JustTookBZMF;
    }
  return (0);
}
