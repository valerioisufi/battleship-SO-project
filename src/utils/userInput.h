#ifndef USER_INPUT_H
#define USER_INPUT_H

#define fflush(stdin) while(getchar() != '\n')

int getIntFromString(char *str, int *value);
char *getStringFromInt(int value);

char *readAlfanumericString(int max_length);

void eof_handler();

#endif // USER_INPUT_H