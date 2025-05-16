#ifndef FILE_H_
#define FILE_H_


#include "line.h"

void openFile(char const* fileName, Text* text);
void saveFile(char const* fileName, Text* text);

#endif
