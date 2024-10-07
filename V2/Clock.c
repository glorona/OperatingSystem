// V1
#include "Clock.h"
#include "Processor.h"
#include "ComputerSystemBase.h"

int tics=0;


void Clock_Update() {
	if(tics%intervalBetweenInterrupts==0){
		Processor_RaiseInterrupt(CLOCKINT_BIT);
	}
	tics++;
}


int Clock_GetTime() {

	return tics;
}
