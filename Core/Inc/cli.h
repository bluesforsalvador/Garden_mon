#ifndef __CLI_H__
#define __CLI_H__

#include "main.h"

typedef struct {
    const char *name;
    const char *help;
    void (*handler)(int argc, char **argv);
} cli_command_t;

void CLI_Task(void *argument);
void CLI_RegisterCommands(const cli_command_t *table, size_t count);

#endif
