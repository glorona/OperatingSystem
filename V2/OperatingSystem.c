#include "Simulator.h"
#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include "Clock.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Functions prototypes
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateExecutingProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_HandleClockInterrupt();

// The process table
// PCB processTable[PROCESSTABLEMAXSIZE];
PCB * processTable;

// Size of the memory occupied for the OS
int OS_MEMORY_SIZE=32;

// Address base for OS code in this version
int OS_address_base; 

// Identifier of the current executing process
int executingProcessID=NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation (Not assigned)
int initialPID=-1;

//States for processes
char * statesNames [5]={"NEW","READY","EXECUTING","BLOCKED","EXIT"};

// Begin indes for daemons in programList
// int baseDaemonsInProgramList; 

// Array that contains the identifiers of the READY processes
// heapItem readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
heapItem *readyToRunQueue[NUMBEROFQUEUES];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES]={0,0};
char * queueNames[NUMBEROFQUEUES] = {"USER","DAEMONS"};

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

// char DAEMONS_PROGRAMS_FILE[MAXFILENAMELENGTH]="teachersDaemons";

int MAINMEMORYSECTIONSIZE = 60;

extern int MAINMEMORYSIZE;

int PROCESSTABLEMAXSIZE = 4;

// In OperatingSystem.c Exercise 5-b of V2
// Heap with blocked processes sort by when to wakeup
heapItem *sleepingProcessesQueue;
int numberOfSleepingProcesses=0;



// Initial set of tasks of the OS
void OperatingSystem_Initialize(int programsFromFileIndex) {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code
	
// In this version, with original configuration of memory size (300) and number of processes (4)
// every process occupies a 60 positions main memory chunk 
// and OS code and the system stack occupies 60 positions 

	OS_address_base = MAINMEMORYSIZE - OS_MEMORY_SIZE;

	MAINMEMORYSECTIONSIZE = OS_address_base / PROCESSTABLEMAXSIZE;

	if (initialPID<0) // if not assigned in options...
		initialPID=PROCESSTABLEMAXSIZE-1; 
	
	// Space for the processTable
	processTable = (PCB *) malloc(PROCESSTABLEMAXSIZE*sizeof(PCB));
	
	// Space for the ready to run queues (Daemons y User)
	for(int i = 0;i<NUMBEROFQUEUES;i++){
		readyToRunQueue[i] = Heap_create(PROCESSTABLEMAXSIZE);
	}

	sleepingProcessesQueue = Heap_create(PROCESSTABLEMAXSIZE);

	programFile=fopen("OperatingSystemCode", "r");
	if (programFile==NULL){
		// Show red message "FATAL ERROR: Missing Operating System!\n"
		ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,99,SHUTDOWN,"FATAL ERROR: Missing Operating System!\n");
		exit(1);		
	}

	// Obtain the memory requirements of the program
	int processSize=OperatingSystem_ObtainProgramSize(programFile);

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);

	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE;i++){
		processTable[i].busy=0;
		processTable[i].programListIndex=-1;
		processTable[i].initialPhysicalAddress=-1;
		processTable[i].processSize=-1;
		processTable[i].copyOfSPRegister=-1;
		processTable[i].state=-1;
		processTable[i].priority=-1;
		processTable[i].copyOfPCRegister=-1;
		processTable[i].copyOfPSWRegister=0;
		processTable[i].programListIndex=-1;
		processTable[i].queueID=-1;
		processTable[i].copyOfAccumulator=0;
		processTable[i].copyOfRegA = -1;
		processTable[i].copyOfRegB = -1;
		processTable[i].copyOfRegC = -1;
	}
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base+2);
		
	// Include in program list all user or system daemon processes
	OperatingSystem_PrepareDaemons(programsFromFileIndex);
	
	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();

	if (numberOfNotTerminatedUserProcesses == 0){ //Solo los programas de usuario
		//The system must dispatch SIP before shutdown
		OperatingSystem_ReadyToShutdown();
	}
	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")
		&& processTable[sipID].state==READY) {
		// Show red message "FATAL ERROR: Missing SIP program!\n"
		ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,99,SHUTDOWN,"FATAL ERROR: Missing SIP program!\n");
		exit(1);		
	}

	// At least, one process has been created
	// Select the first process that is going to use the processor
	selectedProcess=OperatingSystem_ShortTermScheduler();

	Processor_SetSSP(MAINMEMORYSIZE-1);

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);

	OperatingSystem_PrintStatus();

}

// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the 
// 			command line and daemons programs
int OperatingSystem_LongTermScheduler() {
  
	int PID, i,
		numberOfSuccessfullyCreatedProcesses=0;
	
	for (i=0; programList[i]!=NULL && i<PROGRAMSMAXNUMBER ; i++) {
		PID=OperatingSystem_CreateProcess(i);
		if(PID == NOFREEENTRY){
			ComputerSystem_DebugMessage(TIMED_MESSAGE,103,ERROR,programList[i]->executableName);
		}
		else if(PID == PROGRAMDOESNOTEXIST){
			ComputerSystem_DebugMessage(TIMED_MESSAGE,104,ERROR,programList[i]->executableName, "it does not exist");
		}
		else if(PID == PROGRAMNOTVALID){
			ComputerSystem_DebugMessage(TIMED_MESSAGE,104,ERROR,programList[i]->executableName, "invalid priority or size");
		}
		else if(PID == TOOBIGPROCESS){
			ComputerSystem_DebugMessage(TIMED_MESSAGE,105,ERROR,programList[i]->executableName);
		}
		else{
		numberOfSuccessfullyCreatedProcesses++;
		if (programList[i]->type==USERPROGRAM) 
			numberOfNotTerminatedUserProcesses++;
		// Move process to the ready state
		OperatingSystem_MoveToTheREADYState(PID);
		OperatingSystem_PrintStatus();
		}
	}

	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}

// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram) {
  
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	int loadStatus;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram=programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID=OperatingSystem_ObtainAnEntryInTheProcessTable();
	if(PID == NOFREEENTRY){
		return NOFREEENTRY;
	}

	// Check if programFile exists
	programFile=fopen(executableProgram->executableName, "r");
	if(programFile == NULL){
		return PROGRAMDOESNOTEXIST;
	}

	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(programFile);	

	// Obtain the priority for the process
	priority=OperatingSystem_ObtainPriority(programFile);
	if(processSize == PROGRAMNOTVALID || priority == PROGRAMNOTVALID){
		return PROGRAMNOTVALID;
	}
	
	// Obtain enough memory space
 	loadingPhysicalAddress=OperatingSystem_ObtainMainMemory(processSize, PID);
	if(loadingPhysicalAddress == TOOBIGPROCESS){
		return TOOBIGPROCESS;
	}

	// Load program in the allocated memory
	loadStatus = OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);
	if(loadStatus == TOOBIGPROCESS){
		return TOOBIGPROCESS;
	}
	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);
	
	// Show message "Process [PID] created from program [executableName]\n"
	ComputerSystem_DebugMessage(TIMED_MESSAGE,111,SYSPROC,PID,statesNames[0],executableProgram->executableName);
	
	return PID;
}


// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID) {

 	if (processSize>MAINMEMORYSECTIONSIZE)
		return TOOBIGPROCESS;
	
 	return PID*MAINMEMORYSECTIONSIZE;
}


// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex) {

	processTable[PID].busy=1;
	processTable[PID].initialPhysicalAddress=initialPhysicalAddress;
	processTable[PID].processSize=processSize;
	processTable[PID].copyOfSPRegister=initialPhysicalAddress+processSize;
	processTable[PID].state=NEW;
	processTable[PID].priority=priority;
	processTable[PID].programListIndex=processPLIndex;
	processTable[PID].copyOfAccumulator=0;
	processTable[PID].copyOfRegA=0;
	processTable[PID].copyOfRegB=0;
	processTable[PID].copyOfRegC=0;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {
		processTable[PID].queueID = DAEMONSQUEUE;
		processTable[PID].copyOfPCRegister=initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister= ((unsigned int) 1) << EXECUTION_MODE_BIT;
	} 
	else {
		processTable[PID].copyOfPCRegister=0;
		processTable[PID].copyOfPSWRegister=0;
		processTable[PID].queueID = USERPROCESSQUEUE;
	}

}


// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID) {

	int qID = processTable[PID].queueID;

	
	if (Heap_add(PID, readyToRunQueue[qID],QUEUE_PRIORITY ,&(numberOfReadyToRunProcesses[qID]))>=0) {
		if(executingProcessID == PID){
			ComputerSystem_DebugMessage(TIMED_MESSAGE,110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[2],statesNames[1]);
		}
		else{
			ComputerSystem_DebugMessage(TIMED_MESSAGE,110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[0],statesNames[1]);
		}
		processTable[PID].state=READY;
	} 
	//OperatingSystem_PrintReadyToRunQueue();
}


// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {
	
	int selectedProcess;

	selectedProcess=OperatingSystem_ExtractFromReadyToRun();
	
	return selectedProcess;
}


// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun() {
  
	int selectedProcess=NOPROCESS;

	if(numberOfReadyToRunProcesses[USERPROCESSQUEUE] > 0){
		selectedProcess=Heap_poll(readyToRunQueue[USERPROCESSQUEUE],QUEUE_PRIORITY ,&(numberOfReadyToRunProcesses[USERPROCESSQUEUE]));
	}
	else{
		selectedProcess=Heap_poll(readyToRunQueue[DAEMONSQUEUE],QUEUE_PRIORITY ,&(numberOfReadyToRunProcesses[DAEMONSQUEUE]));
	}

	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}


// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {

	ComputerSystem_DebugMessage(TIMED_MESSAGE,110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[1],statesNames[2]);

	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	// Change the process' state
	processTable[PID].state=EXECUTING;
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}


// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) {
  
	// New values for the CPU registers are obtained from the PCB
	Processor_PushInSystemStack(processTable[PID].copyOfPCRegister);
	Processor_PushInSystemStack(processTable[PID].copyOfPSWRegister);
	Processor_SetRegisterSP(processTable[PID].copyOfSPRegister);
	//Restore new values
	Processor_SetAccumulator(processTable[PID].copyOfAccumulator);
	Processor_SetRegisterA(processTable[PID].copyOfRegA);
	Processor_SetRegisterB(processTable[PID].copyOfRegB);
	Processor_SetRegisterC(processTable[PID].copyOfRegC);

	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}


// Function invoked when the executing process leaves the CPU 
void OperatingSystem_PreemptRunningProcess() {

	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID=NOPROCESS;
}


// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {
	
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister=Processor_PopFromSystemStack();
	
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister=Processor_PopFromSystemStack();
	
	// Save RegisterSP 
	processTable[PID].copyOfSPRegister=Processor_GetRegisterSP();

	//Guarda todos los registros extra del procesador y el acumulador
	processTable[PID].copyOfAccumulator=Processor_GetAccumulator();
	processTable[PID].copyOfRegA = Processor_GetRegisterA();
	processTable[PID].copyOfRegB = Processor_GetRegisterB();
	processTable[PID].copyOfRegC = Processor_GetRegisterC();

}


// Exception management routine
void OperatingSystem_HandleException() {
  
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	ComputerSystem_DebugMessage(TIMED_MESSAGE,71,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
	
	OperatingSystem_TerminateExecutingProcess();

	OperatingSystem_PrintStatus();
}

// All tasks regarding the removal of the executing process
void OperatingSystem_TerminateExecutingProcess() {

	processTable[executingProcessID].state=EXIT;
	ComputerSystem_DebugMessage(TIMED_MESSAGE,110,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,statesNames[2],statesNames[4]);
	
	if (executingProcessID==sipID) {
		// finishing sipID, change PC to address of OS HALT instruction
		Processor_SetSSP(MAINMEMORYSIZE-1);
		Processor_PushInSystemStack(OS_address_base+1);
		Processor_PushInSystemStack(Processor_GetPSW());
		executingProcessID=NOPROCESS;
		ComputerSystem_DebugMessage(TIMED_MESSAGE,99,SHUTDOWN,"The system will shut down now...\n");
		return; // Don't dispatch any process
	}

	Processor_SetSSP(Processor_GetSSP()+2); // unstack PC and PSW stacked

	if (programList[processTable[executingProcessID].programListIndex]->type==USERPROGRAM) 
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;
	
	if (numberOfNotTerminatedUserProcesses==0) {
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	int selectedProcess=OperatingSystem_ShortTermScheduler();


	//ComputerSystem_DebugMessage(TIMED_MESSAGE,110,SYSPROC,selectedProcess,programList[processTable[selectedProcess].programListIndex]->executableName,statesNames[1],statesNames[2]);

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall() {

	//Para Yield
	int queue;
	int priorityExecuting;
	int processToExecute;
	int nextPriorityProcessID;
  
	int systemCallID;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterC();
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			ComputerSystem_DebugMessage(TIMED_MESSAGE,72,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,Processor_GetRegisterA(),Processor_GetRegisterB());
			break;
		case SYSCALL_YIELD:
			queue = processTable[executingProcessID].queueID;
			priorityExecuting = processTable[executingProcessID].priority;

			nextPriorityProcessID = Heap_getFirst(readyToRunQueue[queue],numberOfReadyToRunProcesses[queue]);
			if(nextPriorityProcessID > 0){
				if(processTable[nextPriorityProcessID].priority == priorityExecuting){
					ComputerSystem_DebugMessage(TIMED_MESSAGE,116,SHORTTERMSCHEDULE,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,nextPriorityProcessID,programList[processTable[nextPriorityProcessID].programListIndex]->executableName);
					OperatingSystem_PreemptRunningProcess();
					processToExecute = OperatingSystem_ShortTermScheduler();
					OperatingSystem_Dispatch(processToExecute);
					OperatingSystem_PrintStatus();
				}
				else{
					ComputerSystem_DebugMessage(TIMED_MESSAGE,117,SHORTTERMSCHEDULE,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
				}
			}else{
					ComputerSystem_DebugMessage(TIMED_MESSAGE,117,SHORTTERMSCHEDULE,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			}
			break;
		
		case SYSCALL_SLEEP:
			break;
		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			ComputerSystem_DebugMessage(TIMED_MESSAGE,73,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			OperatingSystem_TerminateExecutingProcess();
			OperatingSystem_PrintStatus();
			break;
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint){
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
		case CLOCKINT_BIT: // CLOCKINT_BIT=9
			OperatingSystem_HandleClockInterrupt();
			break;
		
	}

}

// Implementar Print Ready to Run Queue
void OperatingSystem_PrintReadyToRunQueue(){
	ComputerSystem_DebugMessage(TIMED_MESSAGE,106,SHORTTERMSCHEDULE);
	for(int queue=0;queue<NUMBEROFQUEUES;queue++){
		if(queue == USERPROCESSQUEUE){
			ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,113,SHORTTERMSCHEDULE);
		}
		else{
			ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,114,SHORTTERMSCHEDULE);
		}
		if(numberOfReadyToRunProcesses[queue] == 0){
				ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,118,SHORTTERMSCHEDULE);
		}
		for(int i = 0; i< numberOfReadyToRunProcesses[queue]; i++){
			int PID = readyToRunQueue[queue][i].info;
			int priority = processTable[PID].priority;
			if(numberOfReadyToRunProcesses[queue] == 0){
				ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,118,SHORTTERMSCHEDULE);
			}
			else if(numberOfReadyToRunProcesses[queue] == 1){
				ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,109,SHORTTERMSCHEDULE,PID,priority);
			}
			else{
				if(i == 0){
					ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,108,SHORTTERMSCHEDULE,PID,priority);
				}
				else if(i == numberOfReadyToRunProcesses[queue]-1){
					ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,109,SHORTTERMSCHEDULE,PID,priority);
				}
				else{
					ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,109,SHORTTERMSCHEDULE,PID,priority);
				}

			}
			
		}
	}


	
}

//Clock interruptions

// In OperatingSystem.c Exercise 1-b of V2
void OperatingSystem_HandleClockInterrupt(){ 

	int totalInterrupts;
	int currentTics;

	currentTics = Clock_GetTime();

	totalInterrupts = currentTics/intervalBetweenInterrupts;

	ComputerSystem_DebugMessage(TIMED_MESSAGE,120,INTERRUPT,totalInterrupts);
	
	}

