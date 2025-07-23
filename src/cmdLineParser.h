typedef struct _argvParam{
    char *paramName;  // Name of the parameter

    char *paramValue; // Value of the parameter
    int isSet;        // Flag to indicate if the parameter is set

    struct _argvParam *next;
} ArgvParam;

void parseCmdLine(int argc, char *argv[], ArgvParam *argvParams);

ArgvParam *setArgvParams(char *paramsName);