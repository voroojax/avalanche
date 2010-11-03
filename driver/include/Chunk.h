
/*----------------------------------------------------------------------------------------*/
/*------------------------------------- AVALANCHE ----------------------------------------*/
/*------ Driver. Coordinates other processes, traverses conditional jumps tree.  ---------*/
/*-------------------------------------- Chunk.h -----------------------------------------*/
/*----------------------------------------------------------------------------------------*/

/*
   Copyright (C) 2010 Ildar Isaev
      iisaev@ispras.ru

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef __CHUNK__H__
#define __CHUNK__H__

#include <vector>
#include <string>

class FileBuffer;

class Chunk
{
private:
  FileBuffer* trace;
  std::vector<std::pair<int, int> > exploitGroups;
public:
  Chunk(FileBuffer* trace, int exploitNum, int inputNum);
  ~Chunk();

  void addGroup(int exploitNum, int inputNum);
  
  FileBuffer* getTrace();

  void print(std::string prefix, int chunkNum, bool dumpToFile = false);
};

#endif

