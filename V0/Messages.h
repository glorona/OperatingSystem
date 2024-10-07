#ifndef MESSAGES_H
#define MESSAGES_H

#define NUMBEROFMSGS 64
#define MSGMAXIMUMLENGTH 132

typedef struct {
  int number;
  char format[MSGMAXIMUMLENGTH];
} DEBUG_MESSAGES;

extern DEBUG_MESSAGES DebugMessages[NUMBEROFMSGS];

int Messages_Get_Pos(int number);
int Messages_Load_Messages();

#endif

