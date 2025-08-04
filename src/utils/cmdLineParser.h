#ifndef CMDLINEPARSER_H
#define CMDLINEPARSER_H

typedef struct _argvParam{
    char *paramName;  // Name of the parameter
    int isParamRequired;
    int isValueRequired;

    char *paramValue; // Value of the parameter
    int isSet;        // Flag to indicate if the parameter is set

    struct _argvParam *next;
} ArgvParam;

void parseCmdLine(int argc, char *argv[], ArgvParam *argvParams);

void printUsage(char *fileName, ArgvParam *argvParams);

ArgvParam *setArgvParams(char *paramsName);

char *getArgvParamValue(char *paramName, ArgvParam *argvParams);

void freeArgvParams(ArgvParam *head);

#endif // CMDLINEPARSER_H