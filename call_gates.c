/*
	Author: Dreg aka David Reguera Garc�a 
			Dreg@fr33project.org - http://blog.48bits.com
	-
	License: MIT
	-
	Description: Callgate injector based in rootkit arsenal: fixed.
    -
	Geetz: GriYo, Slay, Tomac, Alon & Pluf.
*/

#include "call_gates.h"

#define DBG_TRACE( src, msg ) DbgPrint(" [%s 1: %s", src, msg)
#define DBG_PRINT2(fmt,arg1) DbgPrint(fmt, arg1)

DWORD calledFlag;
CALL_GATE_DESCRIPTOR oldCG;


/**/
void saySomething( void );
void CallGateProc( void );
CALL_GATE_DESCRIPTOR injectCallGate( CALL_GATE_DESCRIPTOR );
CALL_GATE_DESCRIPTOR buildCallGate( BYTE * );
DWORD getGDTSize( void );
PSEG_DESCRIPTOR getGDTBaseAddress( void );
void walkGDT( void );
void Unload( IN PDRIVER_OBJECT );
void printGDT(DWORD, PSEG_DESCRIPTOR );
/**/

void printGDT(DWORD selector, PSEG_DESCRIPTOR sd)
{
	DWORD baseAddress;
	DWORD limit;
	DWORD increment;
	char type[32][11] =
	{
		"Data RO \0",
		"Data RO AC \0",
		"Data RW \0",
		"Data RW Ac\0",
		"Data RO E \0",
		"Data RO EA\0" ,
		"Data RW E \0",
		"Data RW EA \0",
		"Code EO \0",
		"Code EO Ac \0",
		"Code RE \0",
		"Code RE Ac \0",
		"Code EO C \0",
		"Code EO CA \0",
		"Code RE C \0",
		"Code RE CA \0",
		"<Reserved> \0",
		"T5516 Avl \0",
		"LDT \0",
		"T5516 Busy \0",
		"CallGate16 \0" ,
		"Task Gate \0",
		"Int Gate16 \0",
		"TrapGate16 \0" ,
		"<Reserved> \0" ,
		"T5532 Avl \0",
		"<Reserved > \0" ,
		"T5532 Busy \0",
		"CallGate32 \0" ,
		"<Reserved> \0",
		"Int Gate32 \0",
		"TrapGate32 \0"
	};
	DWORD index;
	char present[2][3] = {"Np\0", "P \0"};
	char granularity[2][3] = {"By\0", "Pg\0"};

	baseAddress = 0;
	baseAddress = baseAddress + sd->baseAddress_24_31;
	baseAddress = baseAddress << 8;
	baseAddress = baseAddress + sd->baseAddress_16_23;
	baseAddress = baseAddress << 16;
	baseAddress = baseAddress + sd->baseAddress_00_15;

	limit = 0;
	limit = limit + sd->size_16_19;
	limit = limit << 16;
	limit = limit + sd->size_00_15;

	if ( sd->gFlag == 1 )
	{
		increment = 4096;
		limit++;
		limit = limit*increment;
		limit--;
	}

	index = 0;
	index = sd->type;
	
	if(sd->sFlag==0)
		index = index + 16;

	DbgPrint
	(
		"%04x %08x %08x %s %u - - %s %s %u\n" ,
		selector,
		baseAddress,
		limit,
		type[index] ,
		sd->dpl,
		granularity[sd->gFlag],
		present[sd->pFlag],
		sd->sFlag
	);
}

void walkGDT( void )
{
	DWORD nGDT;
	PSEG_DESCRIPTOR pgdt;
	DWORD i, j;
	SYSTEM_BASIC_INFORMATION system_basic_information;
	NTSTATUS Status;
	KAFFINITY AffinityMask;

	Status = ZwQuerySystemInformation( SystemBasicInformation, & system_basic_information, sizeof( system_basic_information ), NULL );
	if ( NT_SUCCESS(Status) )
	{
		DbgPrint( " Number of cores: %d\n", system_basic_information.NumberOfProcessors );

		for ( j = 0; j < system_basic_information.NumberOfProcessors; j++ )
		{
			AffinityMask = j + 1;

			DbgPrint( " Setting AffinityMask to: %d...:\n", AffinityMask );

			Status = ZwSetInformationThread( (HANDLE) -2, ThreadAffinityMask, & AffinityMask, sizeof( AffinityMask ) ); 
			if ( NT_SUCCESS(Status) )
			{
				DbgPrint( " Show GDT in core %d...:\n", AffinityMask );
				
				pgdt = getGDTBaseAddress();
				nGDT = getGDTSize();

				DbgPrint (" Sel Base Limit Type P Sz G Pr Sys\n");
				DbgPrint("-- -- -------- -------- ---- ------ - -- -- -- ---\n");
				for ( i = 0; i < nGDT; i++ )
				{
					printGDT( (i * 8), pgdt);

					pgdt++;
				}
			}
		}
	}
}

PSEG_DESCRIPTOR getGDTBaseAddress( void )
{
	GDTR gdtr;
	
	__asm
	{
		SGDT gdtr;
	}

	return (PSEG_DESCRIPTOR) gdtr.baseAddress;
}

DWORD getGDTSize( void )
{
	GDTR gdtr;

	__asm
	{
		SGDT gdtr;
	}

	return gdtr.nBytes/8; 
}

CALL_GATE_DESCRIPTOR buildCallGate(BYTE* procAddress)
{
	DWORD address;
	CALL_GATE_DESCRIPTOR cg;

	address = (DWORD) procAddress;
	cg.selector = KGDT_R0_CODE;
	cg.argCount = 0;
	cg. zeroes = 0; 
	cg.type = 0xC; 
	cg.sFlag = 0; 
	cg.dpl = 0x3; 
	cg.pFlag = 1; 
	cg.offset_00_15 = (WORD)(0x0000FFFF & address);
	address = address >> 16;
	cg.offset_16_31 = (WORD)(0x0000FFFF & address);
	
	return cg;
}

CALL_GATE_DESCRIPTOR injectCallGate(CALL_GATE_DESCRIPTOR cg)
{
	PSEG_DESCRIPTOR gdt;
	PSEG_DESCRIPTOR gdtEntry;
	PCALL_GATE_DESCRIPTOR oldCGPtr;
	CALL_GATE_DESCRIPTOR oldCG;

	gdt = getGDTBaseAddress();
	
	oldCGPtr = (PCALL_GATE_DESCRIPTOR)&(gdt[100]);
	oldCG    = *oldCGPtr;
	gdtEntry = (PSEG_DESCRIPTOR)&cg;
	gdt[100] = *gdtEntry;

	return oldCG;
}

void __declspec(naked) CallGateProc( void )
{
	__asm
	{
		pushad; 
		pushfd;

		cli; 
		push fs; 
		mov bx, 0x30; 
		mov fs, bx;
		push ds;
		push es;

		call saySomething;
	}

	calledFlag = 0xCAFEBABE;

	__asm
	{
		pop es;
		pop ds;
		pop fs;
		sti;
		popfd;
		popad;
		retf;
	}
}

void saySomething( void )
{
	DbgPrint( "you are dealing with hell while running ring0\n" );
}

NTSTATUS DriverEntry( IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING regPath )
{
	CALL_GATE_DESCRIPTOR cg;
	SYSTEM_BASIC_INFORMATION system_basic_information;
	NTSTATUS Status;
	KAFFINITY AffinityMask;

	calledFlag = 0;
	DBG_TRACE("Driver Entry","Establishing other DriverObject function pointers\n");

	(*pDriverObject).DriverUnload = Unload;
	
	walkGDT(); 
	
	DBG_TRACE("Driver Entry","Injecting new call gate\n");
	
	cg = buildCallGate((BYTE*)CallGateProc);
	
	oldCG = injectCallGate(cg);
	
	walkGDT();
	
	return STATUS_SUCCESS;
}

void Unload( IN PDRIVER_OBJECT pDriverObject )
{
	DBG_TRACE ( "Unload", "Received signal to unload the driver\n");
	DBG_TRACE("Unload", "Restoring old call gate\n");

	injectCallGate( oldCG);
	walkGDT();
	DBG_PRINT2(" [Unload]: calledFlag=%08x\n" ,calledFlag);
}
