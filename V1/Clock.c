// V1
#include "Clock.h"
#include "Processor.h"

int tics=0;

void Clock_Update() {
	tics++;
}


int Clock_GetTime() {

	return tics;
}
