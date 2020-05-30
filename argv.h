#pragma once

char **
argv_build (char* input);

void 
argv_free (char **vector);

int 
argv_argc(char** vector);

char* 
argv_reconstruct(char** vector);
