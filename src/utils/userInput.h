#ifndef USER_INPUT_H
#define USER_INPUT_H

int getIntFromString(char *str, int *value);
char *getStringFromInt(int value);

char *readAlfanumericString(int max_length);

#endif // USER_INPUT_H