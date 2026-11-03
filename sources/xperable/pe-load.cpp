/*
The MIT License (MIT)

Copyright (c) 2013 Andrew Ruef

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#include <cstring>
#include <iomanip>
#include <ios>
#include <iostream>
#include <sstream>

#include <pe-parse/parse.h>

#include "logging.h"
extern int verbosity;

using namespace peparse;

static VA image_base;
static uint8_t *image_buff;

static int fix_relocs(void *N, const VA &relocAddr, const reloc_type &type)
{
  uint64_t *ptr;
  uint64_t newbase = *static_cast<uint64_t *>(N);

  if (type == RELOC_ABSOLUTE)
    return 0;

#if 0
  std::cout << "TYPE: ";
  switch (type) {
    case RELOC_ABSOLUTE:
      std::cout << "ABSOLUTE";
      break;
    case RELOC_HIGH:
      std::cout << "HIGH";
      break;
    case RELOC_LOW:
      std::cout << "LOW";
      break;
    case RELOC_HIGHLOW:
      std::cout << "HIGHLOW";
      break;
    case RELOC_HIGHADJ:
      std::cout << "HIGHADJ";
      break;
    case RELOC_MIPS_JMPADDR:
      std::cout << "MIPS_JMPADDR";
      break;
    case RELOC_MIPS_JMPADDR16:
      std::cout << "MIPS_JMPADD16";
      break;
    case RELOC_DIR64:
      std::cout << "DIR64";
      break;
    default:
      std::cout << "UNKNOWN";
      break;
  }
#endif

  ptr = reinterpret_cast<uint64_t *>(&image_buff[relocAddr - image_base]);

//  std::cout << " VA: 0x" << std::hex << relocAddr << " [0x" << std::hex << *ptr;
  *ptr += newbase;
//  std::cout << " -> 0x" << std::hex << *ptr << "]\n";

  return 0;
}

static int find_code_boundary(void *N,
              const VA &secBase,
              const std::string &secName,
              const image_section_header &s,
              const bounded_buffer *data)
{
  static_cast<void>(s);
  int *code_boundary = static_cast <int *>(N);
  if (secName == ".text" && data != NULL) {
    *code_boundary = (int)secBase + data->bufLen;
  }
  return 0;
}

extern "C"
int load_pe(const char *fname, void *buff, int maxsize, uint64_t base_va,
            int *code_boundary)
{
    int i;
    int fd;
    int res;

    FILE *f = fopen(fname, "rb");
    if (f == NULL) {
        PRNO("fopen pe %s", fname);
        return -1;
    }
    res = fread(buff, 1, maxsize, f);
    fclose(f);

    parsed_pe *p = ParsePEFromFile(fname);

    if (p == nullptr) {
        PERR("load_pe of %s failed: %s\n", fname, GetPEErrString().c_str());
        return -1;
     }


    VA va = p->peHeader.nt.OptionalHeader64.ImageBase;
    VA va_end = va + p->peHeader.nt.OptionalHeader64.SizeOfImage;
    image_base = va;
    image_buff = (uint8_t *)buff;
    for (i = 0; va < va_end; va++, i++)
        ReadByteAtVA(p, va, image_buff[i]);

    IterRelocs(p, fix_relocs, &base_va);

    if (code_boundary != NULL) {
        *code_boundary = 0;
        IterSec(p, find_code_boundary, code_boundary);
        PDBG("Code boundary of %s is 0x%08x\n", fname, *code_boundary);
    }

    res = p->peHeader.nt.OptionalHeader64.SizeOfImage;

    DestructParsedPE(p);

    return res;
}
