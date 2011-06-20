
/*--------------------------------------------------------------------------------*/
/*-------------------------------- AVALANCHE -------------------------------------*/
/*--- Tracegring. Transforms IR tainted trace to STP declarations.   tg_main.c ---*/
/*--------------------------------------------------------------------------------*/

/*
   This file is part of Tracegrind, the Valgrind tool,
   which tracks tainted data coming from the specified file
   and converts IR trace to STP declarations.

   Copyright (C) 2009 Ildar Isaev
      iisaev@ispras.ru

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_tool_basics.h"
#include "pub_tool_options.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_hashtable.h"
#include "pub_tool_xarray.h"
#include "pub_tool_clientstate.h"
#include "pub_tool_aspacemgr.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_vki.h"
#include "pub_tool_vkiscnums.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_machine.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_stacktrace.h"
#include "libvex_ir.h"

#include <avalanche.h>

#include "buffer.h"
#include "copy.h"
#include "parser.h"

//#define TAINTED_TRACE_PRINTOUT
//#define CALL_STACK_PRINTOUT

enum {
      ARMCondEQ     = 0,  /* equal                         : Z=1 */
      ARMCondNE     = 1,  /* not equal                     : Z=0 */

      ARMCondHS     = 2,  /* >=u (higher or same)          : C=1 */
      ARMCondLO     = 3,  /* <u  (lower)                   : C=0 */

      ARMCondMI     = 4,  /* minus (negative)              : N=1 */
      ARMCondPL     = 5,  /* plus (zero or +ve)            : N=0 */

      ARMCondVS     = 6,  /* overflow                      : V=1 */
      ARMCondVC     = 7,  /* no overflow                   : V=0 */

      ARMCondHI     = 8,  /* >u   (higher)                 : C=1 && Z=0 */
      ARMCondLS     = 9,  /* <=u  (lower or same)          : C=0 || Z=1 */

      ARMCondGE     = 10, /* >=s (signed greater or equal) : N=V */
      ARMCondLT     = 11, /* <s  (signed less than)        : N!=V */

      ARMCondGT     = 12, /* >s  (signed greater)          : Z=0 && N=V */
      ARMCondLE     = 13, /* <=s (signed less or equal)    : Z=1 || N!=V */

      ARMCondAL     = 14, /* always (unconditional)        : 1 */
      ARMCondNV     = 15  /* never (unconditional):        : 0 */
      /* NB: ARM have deprecated the use of the NV condition code.
         You are now supposed to use MOV R0,R0 as a noop rather than
         MOVNV R0,R0 as was previously recommended.  Future processors
         may have the NV condition code reused to do other things.  */
   };

enum {
    X86CondO      = 0,  /* overflow           */
    X86CondNO     = 1,  /* no overflow        */

    X86CondB      = 2,  /* below              */
    X86CondNB     = 3,  /* not below          */

    X86CondZ      = 4,  /* zero               */
    X86CondNZ     = 5,  /* not zero           */

    X86CondBE     = 6,  /* below or equal     */
    X86CondNBE    = 7,  /* not below or equal */

    X86CondS      = 8,  /* negative           */
    X86CondNS     = 9,  /* not negative       */

    X86CondP      = 10, /* parity even        */
    X86CondNP     = 11, /* not parity even    */

    X86CondL      = 12, /* jump less          */
    X86CondNL     = 13, /* not less           */

    X86CondLE     = 14, /* less or equal      */
    X86CondNLE    = 15, /* not less or equal  */

    X86CondAlways = 16  /* HACK */
};

struct _taintedNode
{
  struct _taintedNode* next;
  HWord key;
  HChar* filename;
  ULong offset;
};

typedef struct _taintedNode taintedNode;

struct _size2Node
{
  UShort size;
  //Bool declared;
};

typedef struct _size2Node size2Node;

struct _sizeNode
{
  struct _sizeNode* next;
  UWord key;
  size2Node* temps;
  Int tempsnum;
  UInt visited;
};

typedef struct _sizeNode sizeNode;

Addr curIAddr;
Bool enableFiltering = False;

Bool inTaintedBlock = False;

Bool suppressSubcalls = False;

VgHashTable funcNames;

Char* diFunctionName;
Char* diVarName;

Bool newSB;
IRSB* printSB;

Int fdfuncFilter = -1;

Bool inputFilterEnabled;

VgHashTable taintedMemory;
VgHashTable taintedRegisters;
VgHashTable taintedTemps = NULL;

VgHashTable startAddr;

extern VgHashTable fds;
extern HChar* curfile;
extern Int cursocket;
extern Int curfilenum;
extern ULong curoffs;
extern ULong cursize;

VgHashTable tempSizeTable;
sizeNode* curNode;

ULong start = 0;

static Bool isInputFile = False;
static OffT fileOffset = 0;

Bool isRead = False;
Bool isOpen = False;
Bool isMap = False;
extern Bool curDeclared;
extern Bool accept;
extern Bool connect;
extern Bool socket;
extern Bool isRecv;
Bool checkPrediction = False;
extern Bool sockets;
extern Bool datagrams;
Bool replace = False;
static Int socketsNum = 0;
static Int socketsBoundary;
static replaceData* replace_data;
Bool dumpPrediction = False;
Bool divergence = False;
Bool* prediction;
Bool* actual;

UWord curblock = 0;

Int fdtrace;
Int fddanger;
extern VgHashTable inputfiles;
extern UShort port;
extern UChar ip1, ip2, ip3, ip4;

Int depth = 0;
Int invertdepth = 0;
Bool noInvertLimit = False;
Int curdepth;

Int memory = 0;
Int registers = 0;
UInt curvisited;

Bool checkDanger = False;

Bool protectArgName = False;

static
Bool getFunctionName(Addr addr, Bool onlyEntry, Bool showOffset)
{
  Bool continueFlag = False;
  if (onlyEntry)
  {
    continueFlag = VG_(get_fnname_if_entry) (addr, diFunctionName, 1024);
  }
  else if (showOffset)
  {
    continueFlag = VG_(get_fnname_w_offset) (addr, diFunctionName, 1024);
  }
  else
  {
    continueFlag = VG_(get_fnname) (addr, diFunctionName, 1024);
  }
  if (continueFlag)
  {
    return True;
  }
  return False;
}

static
Bool useFiltering()
{
  if (suppressSubcalls)
  {
    if (getFunctionName(curIAddr, False, False))
    {
      return (VG_(HT_lookup) (funcNames, hashCode(diFunctionName)) != NULL || checkWildcards(diFunctionName));
    }
    return False;
  }
#define STACK_LOOKUP_DEPTH 30
  Addr ips[STACK_LOOKUP_DEPTH];
  Addr sps[STACK_LOOKUP_DEPTH];
  Addr fps[STACK_LOOKUP_DEPTH];
  Int found = VG_(get_StackTrace) (VG_(get_running_tid) (), ips, STACK_LOOKUP_DEPTH, sps, fps, 0);
#undef STACK_LOOKUP_DEPTH
  Int i;
  for (i = 0; i < found; i ++)
  {
    if (getFunctionName(ips[i], False, False))
    {
      if (VG_(HT_lookup) (funcNames, hashCode(diFunctionName)) != NULL || checkWildcards(diFunctionName))
      {
        return True;
      }
    }
  }
  return False;
}

static
Bool dumpCall()
{
  if (getFunctionName(curIAddr, False, False))
  {
    if (cutAffixes(diFunctionName))
    {
      Char tmp[1024];
      VG_(strcpy) (tmp, diFunctionName);
      cutTemplates(tmp);
      if (VG_(HT_lookup) (funcNames, hashCode(tmp)) == NULL)
      {
        Char b[1024];
        Char obj[1024];
        Bool isStandard = False;
        if (VG_(get_objname) ((Addr)(curIAddr), obj, 1024))
        {
          isStandard = isStandardFunction(obj);
        }
        if (!isStandard)
        {
          Int l;
          l = VG_(sprintf) (b, "%s\n", diFunctionName);
          my_write(fdfuncFilter, b, l);
          fnNode* node;
          node = VG_(malloc)("fnNode", sizeof(fnNode));
          node->key = hashCode(tmp);
          node->data = NULL;
          VG_(HT_add_node) (funcNames, node);
        }
      }
    }
    return True;
  }
  return False;
}

static
ULong getDecimalValue(IRExpr* e, IRExpr* value)
{
  if (e->tag == Iex_Const)
  {
    IRConst* con = e->Iex.Const.con;
    switch (con->tag)
    {
      case Ico_U1:	return con->Ico.U1;
      case Ico_U8:	return con->Ico.U8;
      case Ico_U16:	return con->Ico.U16;
      case Ico_U32:	return con->Ico.U32;
      case Ico_U64:	return con->Ico.U64;
      default:		break;
    }
  }
  else
  {
    return (ULong) value;
  }
}

static
Addr64 getLongDecimalValue(IRExpr* e, Addr64 value)
{
  if (e->tag == Iex_Const)
  {
    IRConst* con = e->Iex.Const.con;
    switch (con->tag)
    {
      case Ico_U1:	return con->Ico.U1;
      case Ico_U8:	return con->Ico.U8;
      case Ico_U16:	return con->Ico.U16;
      case Ico_U32:	return con->Ico.U32;
      case Ico_U64:	return con->Ico.U64;
      default:		break;
    }
  }
  else
  {
    return value;
  }
}

static
void translateLongToPowerOfTwo(IRExpr* e, Addr64 value)
{
  Addr64 a = 0x1;
  Addr64 i = 1;
  Char s[256];
  for (; i < value; i++)
  {
    a <<= 1;
  }
  Int l = VG_(sprintf)(s, "0hex%016llx", a);
  my_write(fdtrace, s, l);
  my_write(fddanger, s, l);
}

static
void translateToPowerOfTwo(IRExpr* e, IRExpr* value, UShort size)
{
  Addr64 a = 0x1;
  ULong i = 1;
  ULong v = (ULong) value;
  Char s[256];
  Int l = 0;
  if (e->tag == Iex_Const)
  {
    IRConst* con = e->Iex.Const.con;
    switch (con->tag)
    {
      case Ico_U1:	v = con->Ico.U1;
			break;
      case Ico_U8:	v = con->Ico.U8;
			break;
      case Ico_U16:	v = con->Ico.U16;
			break;
      case Ico_U32:	v = con->Ico.U32;
			break;
      case Ico_U64:	v = con->Ico.U64;
			break;
      default:		break;
    }
  }
  for (; i < v; i++)
  {
    a <<= 1;
  }
  if (e->tag == Iex_Const)
  {
    IRConst* con = e->Iex.Const.con;
    switch (size)
    {
      case 1:		l = VG_(sprintf)(s, "0hex%lx", a);
			break;
      case 8:		l = VG_(sprintf)(s, "0hex%02llx", a);
			break;
      case 16:		l = VG_(sprintf)(s, "0hex%04llx", a);
			break;
      case 32:		l = VG_(sprintf)(s, "0hex%08llx", a);
			break;
      case 64:		l = VG_(sprintf)(s, "0hex%016llx", a);
			break;
      default:		break;
    }
  }
  else
  {
    switch (size)
    {
      case 1:	l = VG_(sprintf)(s, "0bin%lx", a);
                break;
      case 8:	l = VG_(sprintf)(s, "0hex%02llx", a);
                break;
      case 16:	l = VG_(sprintf)(s, "0hex%04llx", a);
                break;
      case 32:	l = VG_(sprintf)(s, "0hex%08llx", a);
                break;
      case 64:	l = VG_(sprintf)(s, "0hex%016llx", a);
                break;
      case 128:	l = VG_(sprintf)(s, "0hex%032llx", a);
                break;
      default: 	break;
    }
  }
  my_write(fdtrace, s, l);
  my_write(fddanger, s, l);
}

static
void translateLongValue(IRExpr* e, Addr64 value)
{
  Char s[256];
  Int l = VG_(sprintf)(s, "0hex%016llx", value);
  my_write(fdtrace, s, l);
  my_write(fddanger, s, l);
}

static
void translateValue(IRExpr* e, IRExpr* value)
{
  Char s[256];
  Int l = 0;
  if (e->tag == Iex_Const)
  {
    IRConst* con = e->Iex.Const.con;
    switch (con->tag)
    {
      case Ico_U1:	l = VG_(sprintf)(s, "0hex%x", con->Ico.U1);
			break;
      case Ico_U8:	l = VG_(sprintf)(s, "0hex%02x", con->Ico.U8);
			break;
      case Ico_U16:	l = VG_(sprintf)(s, "0hex%04x", con->Ico.U16);
			break;
      case Ico_U32:	l = VG_(sprintf)(s, "0hex%08x", con->Ico.U32);
			break;
      case Ico_U64:	l = VG_(sprintf)(s, "0hex%016lx", con->Ico.U64);
			break;
      default:		break;
    }
  }
  else
  {
    switch (curNode->temps[e->Iex.RdTmp.tmp].size)
    {
      case 1:	l = VG_(sprintf)(s, "0bin%lx", value);
                break;
      case 8:	l = VG_(sprintf)(s, "0hex%02lx", value);
                break;
      case 16:	l = VG_(sprintf)(s, "0hex%04lx", value);
                break;
      case 32:	l = VG_(sprintf)(s, "0hex%08lx", value);
                break;
      case 64:	l = VG_(sprintf)(s, "0hex%016lx", value);
                break;
      case 128:	l = VG_(sprintf)(s, "0hex%032lx", value);
                break;
      default: 	break;
    }
  }
  my_write(fdtrace, s, l);
  my_write(fddanger, s, l);
}

static
void translateIRTmp(IRExpr* e)
{
  Char s[256];
  Int l = VG_(sprintf)(s, "t_%lx_%u_%u", curblock, e->Iex.RdTmp.tmp, curvisited);
  my_write(fdtrace, s, l);
  my_write(fddanger, s, l);
}

static
Int stranslateValue(Char* s, IRExpr* e, IRExpr* value)
{
  if (e->tag == Iex_Const)
  {
    IRConst* con = e->Iex.Const.con;
    switch (con->tag)
    {
      case Ico_U1:	return VG_(sprintf)(s, "0hex%x", con->Ico.U1);
      case Ico_U8:	return VG_(sprintf)(s, "0hex%02x", con->Ico.U8);
      case Ico_U16:	return VG_(sprintf)(s, "0hex%04x", con->Ico.U16);
      case Ico_U32:	return VG_(sprintf)(s, "0hex%08x", con->Ico.U32);
      case Ico_U64:	return VG_(sprintf)(s, "0hex%016lx", con->Ico.U64);
      default:		break;
    }
  }
  else
  {
    switch (curNode->temps[e->Iex.RdTmp.tmp].size)
    {
      case 1:	return VG_(sprintf)(s, "0bin%lx", value);
      case 8:	return VG_(sprintf)(s, "0hex%02lx", value);
      case 16:	return VG_(sprintf)(s, "0hex%04lx", value);
      case 32:	return VG_(sprintf)(s, "0hex%08lx", value);
      case 64:	return VG_(sprintf)(s, "0hex%016lx", value);
      case 128:	return VG_(sprintf)(s, "0hex%032lx", value);
      default: 	break;
    }
  }
}
static
Int stranslateIRTmp(Char* s, IRExpr* e)
{
  return VG_(sprintf)(s, "t_%lx_%u_%u", curblock, e->Iex.RdTmp.tmp, curvisited);
}

static
void instrumentIMark(UInt iaddrLowerBytes/*, UInt iaddrUpperBytes*/, UInt basicBlockLowerBytes/*, UInt basicBlockUpperBytes*/, Int types_used)
{
  Addr64 addr = /*(((Addr64) iaddrUpperBytes) << 32) ^*/ iaddrLowerBytes;
  Addr64 bbaddr = /*(((Addr64) basicBlockUpperBytes) << 32) ^*/ basicBlockLowerBytes;
  curIAddr = addr;
#ifdef CALL_STACK_PRINTOUT
  printName = getFunctionName(addr, True, False);
  if (printName)
  {
    VG_(printf) ("%s\n", diFunctionName);
  }
#endif
#ifdef TAINTED_TRACE_PRINTOUT
  VG_(printf)("------ IMark(0x%lx) ------\n", addr);
#endif
}

static
void taintMemoryFromArgv(HWord key, HWord offset)
{
  SizeT s = sizeof(taintedNode);
  taintedNode* node;
  node = VG_(malloc)("taintMemoryNode", s);
  node->key = key;
  node->filename = "argv_dot_log";
  node->offset = offset;
  VG_(HT_add_node) (taintedMemory, node);
  Char ss[256];
  Char format[256];
#if defined(VGA_x86)
  VG_(sprintf)(format, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%%08x] := file_argv_dot_log[0hex%%08x];\n", memory + 1, memory);
#elif defined(VGA_amd64)
  VG_(sprintf)(format, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%%016lx] := file_argv_dot_log[0hex%%08x];\n", memory + 1, memory);
#endif
  memory++;
  Int l = VG_(sprintf)(ss, format, key, offset);
  my_write(fdtrace, ss, l);
  my_write(fddanger, ss, l);
}

static
void taintMemoryFromFile(HWord key, HWord offset)
{
  SizeT s = sizeof(taintedNode);
  taintedNode* node;
  node = VG_(malloc)("taintMemoryNode", s);
  node->key = key;
  node->filename = curfile;
  node->offset = offset;
  VG_(HT_add_node)(taintedMemory, node);
  Char ss[256];
  Char format[256];
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
  VG_(sprintf)(format, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%%08x] := file_%s[0hex%%08x];\n", memory + 1, memory, curfile);
#elif defined(VGP_amd64_linux)
  VG_(sprintf)(format, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%%016lx] := file_%s[0hex%%08x];\n", memory + 1, memory, curfile);
#else
#  error Unknown arch
#endif
  memory++;
  Int l = VG_(sprintf)(ss, format, key, offset);
  my_write(fdtrace, ss, l);
  my_write(fddanger, ss, l);
}

static
void taintMemoryFromSocket(HWord key, HWord offset)
{
  SizeT s = sizeof(taintedNode);
  taintedNode* node;
  node = VG_(malloc)("taintMemoryNode", s);
  node->key = key;
  node->filename = NULL;
  node->offset = offset;
  VG_(HT_add_node)(taintedMemory, node);
  Char ss[256];
  Char format[256];
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
  VG_(sprintf)(format, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%%08lx] := socket_%d[0hex%%08x];\n", memory + 1, memory, cursocket);
#elif defined(VGP_amd64_linux)
  VG_(sprintf)(format, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%%016lx] := socket_%d[0hex%%08x];\n", memory + 1, memory, cursocket);
#endif
  memory++;
  Int l = VG_(sprintf)(ss, format, key, offset);
  my_write(fdtrace, ss, l);
  my_write(fddanger, ss, l);
}

static
void taintMemory(HWord key, UShort size)
{
  taintedNode* node;
  switch (size)
  {
    case 8:	if (VG_(HT_lookup)(taintedMemory, key) == NULL)
                {
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
                }
		return;
    case 16:	if (VG_(HT_lookup)(taintedMemory, key) == NULL)
                {
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
                }
		if (VG_(HT_lookup)(taintedMemory, key + 1) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 1;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		return;
    case 32:	if (VG_(HT_lookup)(taintedMemory, key) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 1) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 1;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 2) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 2;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 3) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 3;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
                return;
    case 64:	if (VG_(HT_lookup)(taintedMemory, key) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 1) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 1;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 2) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 2;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 3) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 3;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 4) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 4;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 5) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 5;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 6) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 6;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		if (VG_(HT_lookup)(taintedMemory, key + 7) == NULL)
		{
		  node = VG_(malloc)("taintMemoryNode", sizeof(taintedNode));
  		  node->key = key + 7;
		  node->filename = NULL;
		  VG_(HT_add_node)(taintedMemory, node);
		}
		return;
  }
}

static
void untaintMemory(HWord key, UShort size)
{
  taintedNode* node;
  switch (size)
  {
    case 8:	node = VG_(HT_remove)(taintedMemory, key);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		return;
    case 16:	node = VG_(HT_remove)(taintedMemory, key);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 1);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		return;
    case 32:	node = VG_(HT_remove)(taintedMemory, key);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 1);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 2);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 3);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
                return;
    case 64:	node = VG_(HT_remove)(taintedMemory, key);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 1);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 2);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 3);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 4);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 5);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 6);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedMemory, key + 7);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		return;
  }
}

static
void taintRegister(HWord key, UShort size)
{
  taintedNode* node;
  switch (size)
  {
    case 8:	if (VG_(HT_lookup)(taintedRegisters, key) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
                return;
    case 16:	if (VG_(HT_lookup)(taintedRegisters, key) == NULL)
		{
		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 1) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 1;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		return;
    case 32:	if (VG_(HT_lookup)(taintedRegisters, key) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 1) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 1;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 2) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 2;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 3) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 3;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		return;
    case 64:	if (VG_(HT_lookup)(taintedRegisters, key) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 1) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 1;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 2) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 2;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 3) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 3;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 4) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 4;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 5) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 5;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 6) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 6;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		if (VG_(HT_lookup)(taintedRegisters, key + 7) == NULL)
		{
  		  node = VG_(malloc)("taintRegisterNode", sizeof(taintedNode));
  		  node->key = key + 7;
  		  VG_(HT_add_node)(taintedRegisters, node);
		}
		return;
  }
}

static
void untaintRegister(HWord key, UShort size)
{
  taintedNode* node;
  switch (size)
  {
    case 8:	node = VG_(HT_remove)(taintedRegisters, key);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		return;
    case 16:	node = VG_(HT_remove)(taintedRegisters, key);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 1);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		return;
    case 32:	node = VG_(HT_remove)(taintedRegisters, key);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 1);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 2);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 3);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
                return;
    case 64:	node = VG_(HT_remove)(taintedRegisters, key);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 1);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 2);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 3);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 4);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 5);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 6);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		node = VG_(HT_remove)(taintedRegisters, key + 7);
		if (node != NULL)
		{
		  VG_(free)(node);
		}
		return;
  }
}

static
void taintTemp(HWord key)
{
  taintedNode* node = VG_(malloc)("taintTempNode", sizeof(taintedNode));
  node->key = key;
  VG_(HT_add_node)(taintedTemps, node);
  Char s[256];
  Int l = VG_(sprintf)(s, "t_%lx_%u_%u : BITVECTOR(%u);\n", curblock, key, curvisited, curNode->temps[key].size);
  my_write(fdtrace, s, l);
  my_write(fddanger, s, l);
}

static void tg_post_clo_init(void)
{
}

static
void pre_call(ThreadId tid, UInt syscallno)
{
  if (syscallno == __NR_read)
  {
    isRead = True;
  }
  else if (syscallno == __NR_open)
  {
    isOpen = True;
  }
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
  else if ((syscallno == __NR_mmap) || (syscallno == __NR_mmap2))
  {
    isMap = True;
  }
#elif defined(VGP_amd64_linux)
  else if (syscallno == __NR_mmap)
  {
    isMap = True;
  }
#endif
}

static
void post_call(ThreadId tid, UInt syscallno, SysRes res)
{
//  VG_(printf) ("post_call, syscallno = %u\n", syscallno);
  if (syscallno == __NR_read)
  {
    isRead = False;
  }
  else if ((syscallno == __NR_clone) && !sr_isError(res) && (sr_Res(res) == 0))
  {
    VG_(printf)("__NR_clone\n");
    //VG_(exit)(0);
  }
  else if (syscallno == __NR_open)
  {
    isOpen = False;
    if ((curfile != NULL) && !curDeclared)
    {
      Char s[256];
      Int l = VG_(sprintf)(s, "file_%s : ARRAY BITVECTOR(32) OF BITVECTOR(8);\n", curfile);
      my_write(fdtrace, s, l);
      my_write(fddanger, s, l);
    }
  }
#if defined(VGP_x86_linux)
  else if ((syscallno == __NR_socketcall) && (accept || connect || socket) && (cursocket != -1))
#elif defined(VGP_amd64_linux) || defined(VGP_arm_linux)
  else if (((syscallno == __NR_accept) || (syscallno == __NR_connect) || socket) && (cursocket != -1))
#endif
//XXX: "caught connect" message produces __NR_connect message for VGP_arm_linux
  {
    Char s[256];
    Int l = VG_(sprintf)(s, "socket_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8);\n", cursocket);
    my_write(fdtrace, s, l);
    my_write(fddanger, s, l);
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
    accept = False;
    connect = False;
#endif
    socket = False;
  }
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
  else if ((syscallno == __NR_mmap) || (syscallno == __NR_mmap2))
#elif defined(VGP_amd64_linux)
  else if (syscallno == __NR_mmap)
#endif
  {
    isMap = False;
  }
}

static
void tg_track_post_mem_write(CorePart part, ThreadId tid, Addr a, SizeT size)
{
  UWord index;
  Char curMaskByte;
  if (isRead && (curfile != NULL))
  {
    for (index = a; (index < (a + size)) && (curoffs + (index - a) < cursize); index += 1)
    {
      if (inputFilterEnabled)
      {
        if (checkInputOffset(curfilenum, curoffs + (index - a)))
        {
          taintMemoryFromFile(index, curoffs + (index - a));
        }
      }
      else taintMemoryFromFile(index, curoffs + (index - a));
    }
  }
  else if ((isRead || isRecv) && (sockets || datagrams) && (cursocket != -1))
  {
    if (replace)
    {
      if (cursocket >= socketsNum)
      {
        if (replace_data == NULL)
        {
          replace_data = (replaceData*) VG_(malloc)("replace_data", (cursocket + 1) * sizeof(replaceData));
        }
        else
        {
          replace_data = (replaceData*) VG_(realloc)("replace_data", replace_data, (cursocket + 1) * sizeof(replaceData));
        }
        Int i = socketsNum;
        for (; i <= cursocket; i++)
        {
          replace_data[i].length = 0;
          replace_data[i].data = NULL;
        }
        socketsNum = cursocket + 1;
      }
      Int oldlength = replace_data[cursocket].length;
      if (replace_data[cursocket].length < curoffs + size)
      {
        replace_data[cursocket].data = (UChar*) VG_(realloc)("replace_data", replace_data[cursocket].data, curoffs + size);
        VG_(memset)(replace_data[cursocket].data + replace_data[cursocket].length, 0, curoffs + size - replace_data[cursocket].length);
        replace_data[cursocket].length = curoffs + size;
      }
      for (index = a; index < a + size; index++)
      {
        if ((cursocket < socketsBoundary) && (curoffs + (index - a) < oldlength))
        {
          *((UChar*) index) = replace_data[cursocket].data[curoffs + (index - a)];
        }
        else
        {
          replace_data[cursocket].data[curoffs + (index - a)] = *((UChar*) index);
        }
        if (inputFilterEnabled)
        {
          if (checkInputOffset(cursocket, curoffs + (index - a)))
          {
            taintMemoryFromSocket(index, curoffs + (index - a));
          }
        }
        else
        {
          taintMemoryFromSocket(index, curoffs + (index - a));
        }
      }
    }
    else
    {
      for (index = a; index < a + size; index++)
      {
        if (inputFilterEnabled)
        {
          if (checkInputOffset(cursocket, curoffs + (index - a)))
          {
            taintMemoryFromSocket(index, curoffs + (index - a));
          }
        }
        else
        {
          taintMemoryFromSocket(index, curoffs + (index - a));
        }
      }
    }
  }
}

static
void tg_track_mem_mmap(Addr a, SizeT size, Bool rr, Bool ww, Bool xx, ULong di_handle)
{
  Addr index = a;
  Char curMaskByte;
  if (isMap && (curfile != NULL))
  {
    for (index = a; (index < (a + size)) && (index < (a + cursize)); index += 1)
    {
      if (inputFilterEnabled)
      {
        if (checkInputOffset(curfilenum, index - a))
        {
          taintMemoryFromFile(index, index - a);
        }
      }
      else taintMemoryFromFile(index, index - a);
    }
  }
}

//unlikely to be ever used
static
void instrumentPutLoad(IRStmt* clone, UInt offset, IRExpr* loadAddr)
{
  UShort size;
  switch (clone->Ist.Put.data->Iex.Load.ty)
  {
    case Ity_I8:	size = 8;
			break;
    case Ity_I16:	size = 16;
			break;
    case Ity_I32:	size = 32;
			break;
    case Ity_I64:	size = 64;
			break;
    default:		break;
  }
  if (VG_(HT_lookup)(taintedMemory, (UWord) loadAddr) != NULL)
  {
    taintRegister(offset, size);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    Char ss[256];
    Int l = 0;
    UWord addr = (UWord) loadAddr;
    switch (size)
    {
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
      case 8:	l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%08x];\n", registers + 1, registers, offset, memory, addr);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers++;
                break;
      case 16:	l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%08x];\n", registers + 1, registers, offset, memory, addr);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%08x];\n", registers + 2, registers + 1, offset + 1, memory, addr + 1);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 2;
                break;
      case 32:  l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%08x];\n", registers + 1, registers, offset, memory, loadAddr);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%08x];\n", registers + 2, registers + 1, offset + 1, memory, addr + 1);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%08x];\n", registers + 3, registers + 2, offset + 2, memory, addr + 2);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%08x];\n", registers + 4, registers + 3, offset + 3, memory, addr + 3);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 4;
                break;
#elif defined(VGP_amd64_linux)
      case 8:	l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 1, registers, offset, memory, addr);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers++;
                break;
      case 16:	l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 1, registers, offset, memory, addr);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 2, registers + 1, offset + 1, memory, addr + 1);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 2;
                break;
      case 32:  l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 1, registers, offset, memory, loadAddr);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 2, registers + 1, offset + 1, memory, addr + 1);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 3, registers + 2, offset + 2, memory, addr + 2);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 4, registers + 3, offset + 3, memory, addr + 3);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 4;
                break;
      case 64:  l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 1, registers, offset, memory, loadAddr);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 2, registers + 1, offset + 1, memory, addr + 1);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 3, registers + 2, offset + 2, memory, addr + 2);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 4, registers + 3, offset + 3, memory, addr + 3);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 5, registers + 4, offset + 4, memory, addr + 4);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 6, registers + 5, offset + 5, memory, addr + 5);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 7, registers + 6, offset + 6, memory, addr + 6);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := memory_%d[0hex%016lx];\n", registers + 8, registers + 7, offset + 7, memory, addr + 7);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 8;
                break;
#endif
      default:	break;
    }
  }
  else
  {
    untaintRegister(offset, size);
  }
}

//unlikely to be ever used
static
void instrumentPutGet(IRStmt* clone, UInt putOffset, UInt getOffset)
{
  UShort size;
  switch (clone->Ist.Put.data->Iex.Get.ty)
  {
    case Ity_I8:	size = 8;
			break;
    case Ity_I16:	size = 16;
			break;
    case Ity_I32:	size = 32;
			break;
    case Ity_I64:	size = 64;
			break;
    default:		break;
  }
  if (VG_(HT_lookup)(taintedRegisters, getOffset) != NULL)
  {
    taintRegister(putOffset, size);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    Char ss[256];
    Int l = 0;
    switch (size)
    {
      case 8:	l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 1, registers, putOffset, registers, getOffset);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers++;
                break;
      case 16:	l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 1, registers, putOffset, registers, getOffset);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 2, registers + 1, putOffset + 1, registers + 1, getOffset + 1);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 2;
                break;
      case 32:  l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 1, registers, putOffset, registers, getOffset);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 2, registers + 1, putOffset + 1, registers + 1, getOffset + 1);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 3, registers + 2, putOffset + 2, registers + 2, getOffset + 2);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 4, registers + 3, putOffset + 3, registers + 3, getOffset + 3);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 4;
                break;
      case 64:  l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 1, registers, putOffset, registers, getOffset);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 2, registers + 1, putOffset + 1, registers + 1, getOffset + 1);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 3, registers + 2, putOffset + 2, registers + 2, getOffset + 2);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 4, registers + 3, putOffset + 3, registers + 3, getOffset + 3);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 5, registers + 4, putOffset + 4, registers + 4, getOffset + 4);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 6, registers + 5, putOffset + 5, registers + 5, getOffset + 5);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 7, registers + 6, putOffset + 6, registers + 6, getOffset + 6);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := registers_%d[0hex%02x];\n", registers + 8, registers + 7, putOffset + 7, registers + 7, getOffset + 7);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 8;
                break;
      default:	break;
    }
  }
  else
  {
    untaintRegister(putOffset, size);
  }
}

static
void instrumentPutRdTmp(IRStmt* clone, UInt offset, UInt tmp)
{
  if (VG_(HT_lookup)(taintedTemps, tmp) != NULL)
  {
    taintRegister(offset, curNode->temps[tmp].size);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    Char ss[256];
    Int l = 0;
    switch (curNode->temps[tmp].size)
    {
      case 8:	l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u;\n", registers + 1, registers, offset, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers++;
                break;
      case 16:	l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[7:0];\n", registers + 1, registers, offset, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[15:8];\n", registers + 2, registers + 1, offset + 1, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 2;
                break;
      case 32:  l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[7:0];\n", registers + 1, registers, offset, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[15:8];\n", registers + 2, registers + 1, offset + 1, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[23:16];\n", registers + 3, registers + 2, offset + 2, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[31:24];\n", registers + 4, registers + 3, offset + 3, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 4;
                break;
      case 64:  l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[7:0];\n", registers + 1, registers, offset, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[15:8];\n", registers + 2, registers + 1, offset + 1, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[23:16];\n", registers + 3, registers + 2, offset + 2, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[31:24];\n", registers + 4, registers + 3, offset + 3, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
		l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[39:32];\n", registers + 5, registers + 4, offset + 4, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[47:40];\n", registers + 6, registers + 5, offset + 5, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[55:48];\n", registers + 7, registers + 6, offset + 6, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "registers_%d : ARRAY BITVECTOR(8) OF BITVECTOR(8) = registers_%d WITH [0hex%02x] := t_%lx_%u_%u[63:56];\n", registers + 8, registers + 7, offset + 7, curblock, tmp, curvisited);
  		my_write(fdtrace, ss, l);
  		my_write(fddanger, ss, l);
                registers += 8;
                break;
      default:	break;
    }
  }
  else
  {
    untaintRegister(offset, curNode->temps[tmp].size);
  }
}

static
void instrumentPutConst(IRStmt* clone, UInt offset)
{
  switch (clone->Ist.Put.data->Iex.Const.con->tag)
  {
    case Ico_U8:	untaintRegister(offset, 8);
			break;
    case Ico_U16:	untaintRegister(offset, 16);
			break;
    case Ico_U32:	untaintRegister(offset, 32);
 			break;
    case Ico_U64:	untaintRegister(offset, 64);
 			break;
    default:		break;
  }
}

static
void instrumentWrTmpLoad(IRStmt* clone, IRExpr* loadAddr)
{
  UInt tmp = clone->Ist.WrTmp.tmp;
  IRType ty = clone->Ist.WrTmp.data->Iex.Load.ty;
  UInt rtmp = clone->Ist.WrTmp.data->Iex.Load.addr->Iex.RdTmp.tmp;
  if (checkDanger && (VG_(HT_lookup)(taintedTemps, rtmp) != NULL) && (!enableFiltering || useFiltering()))
  {
    Char s[256];
    Int l = 0;
    Addr addrs[256];
    //Int segs = VG_(am_get_client_segment_starts)(addrs, 256);
    NSegment* seg = VG_(am_find_nsegment)(addrs[0]);
    Char format[256];
    VG_(sprintf)(format, "ASSERT(BVLT(t_%%lx_%%u_%%u, 0hex%%0%ux));\nQUERY(FALSE);\n",
                 curNode->temps[rtmp].size / 4);
    if (fdfuncFilter >= 0)
    {
      dumpCall();
    }
    l = VG_(sprintf)(s, format, curblock, rtmp, curvisited, seg->start);
    my_write(fddanger, s, l);

  }
  taintedNode* t = VG_(HT_lookup)(taintedMemory, loadAddr);
  if (t != NULL)
  {
    Char s[1024];
    Int l = 0;
    taintTemp(tmp);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    UWord addr = (UWord) loadAddr;
 
    switch (curNode->temps[tmp].size)
    {
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
      case 8:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=memory_%d[0hex%08x]);\n", curblock, tmp, curvisited, memory, addr);
		break;
      case 16:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((memory_%d[0hex%08x] @ 0hex00) | (0hex00 @ memory_%d[0hex%08x])));\n", curblock, tmp, curvisited, memory, addr + 1, memory, addr);
		break;
      case 32:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((memory_%d[0hex%08x] @ 0hex000000) | (0hex00 @ memory_%d[0hex%08x] @ 0hex0000) | (0hex0000 @ memory_%d[0hex%08x] @ 0hex00) | (0hex000000 @ memory_%d[0hex%08x])));\n", curblock, tmp, curvisited, memory, addr + 3, memory, addr + 2, memory, addr + 1, memory, addr);
		break;
      case 64:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((memory_%d[0hex%08x] @ 0hex00000000000000) | (0hex00 @ memory_%d[0hex%08x] @ 0hex000000000000) | (0hex0000 @ memory_%d[0hex%08x] @ 0hex0000000000) | (0hex000000 @ memory_%d[0hex%08x] @ 0hex00000000) | (0hex00000000 @ memory_%d[0hex%08x] @ 0hex000000) | (0hex0000000000 @ memory_%d[0hex%08x] @ 0hex0000) | (0hex000000000000 @ memory_%d[0hex%08x] @ 0hex00) | (0hex00000000000000 @ memory_%d[0hex%08x])));\n", curblock, tmp, curvisited, memory, addr + 7, memory, addr + 6, memory, addr + 5, memory, addr + 4, memory, addr + 3, memory, addr + 2, memory, addr + 1, memory, addr);
#elif defined(VGP_amd64_linux)
      case 8:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=memory_%d[0hex%016lx]);\n", curblock, tmp, curvisited, memory, addr);
		break;
      case 16:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((memory_%d[0hex%016lx] @ 0hex00) | (0hex00 @ memory_%d[0hex%016lx])));\n", curblock, tmp, curvisited, memory, addr + 1, memory, addr);
		break;
      case 32:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((memory_%d[0hex%016lx] @ 0hex000000) | (0hex00 @ memory_%d[0hex%016lx] @ 0hex0000) | (0hex0000 @ memory_%d[0hex%016lx] @ 0hex00) | (0hex000000 @ memory_%d[0hex%016lx])));\n", curblock, tmp, curvisited, memory, addr + 3, memory, addr + 2, memory, addr + 1, memory, addr);
		break;
      case 64:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((memory_%d[0hex%016lx] @ 0hex00000000000000) | (0hex00 @ memory_%d[0hex%016lx] @ 0hex000000000000) | (0hex0000 @ memory_%d[0hex%016lx] @ 0hex0000000000) | (0hex000000 @ memory_%d[0hex%016lx] @ 0hex00000000) | (0hex00000000 @ memory_%d[0hex%016lx] @ 0hex000000) | (0hex0000000000 @ memory_%d[0hex%016lx] @ 0hex0000) | (0hex000000000000 @ memory_%d[0hex%016lx] @ 0hex00) | (0hex00000000000000 @ memory_%d[0hex%016lx])));\n", curblock, tmp, curvisited, memory, addr + 7, memory, addr + 6, memory, addr + 5, memory, addr + 4, memory, addr + 3, memory, addr + 2, memory, addr + 1, memory, addr);
		break;
#endif
    }
    my_write(fdtrace, s, l);
    my_write(fddanger, s, l);
  }
}

static
void instrumentWrTmpGet(IRStmt* clone, UInt tmp, UInt offset)
{
  if (VG_(HT_lookup)(taintedRegisters, offset) != NULL)
  {
    Char s[1024];
    Int l = 0;
    taintTemp(tmp);
    switch (curNode->temps[tmp].size)
    {
      case 8:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=registers_%d[0hex%02x]);\n", curblock, tmp, curvisited, registers, offset);
                break;
      case 16:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((0hex00 @ registers_%d[0hex%02x]) | (registers_%d[0hex%02x] @ 0hex00)));\n", curblock, tmp, curvisited, registers, offset, registers, offset + 1);
		break;
      case 32:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((0hex000000 @ registers_%d[0hex%02x]) | (0hex0000 @ registers_%d[0hex%02x] @ 0hex00) | (0hex00 @ registers_%d[0hex%02x] @ 0hex0000) | (registers_%d[0hex%02x] @ 0hex000000)));\n", curblock, tmp, curvisited, registers, offset, registers, offset + 1, registers, offset + 2, registers, offset + 3);
		break;
      case 64:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((0hex00000000000000 @ registers_%d[0hex%02x]) | (0hex000000000000 @ registers_%d[0hex%02x] @ 0hex00) | (0hex0000000000 @ registers_%d[0hex%02x] @ 0hex0000) | (0hex00000000 @ registers_%d[0hex%02x] @ 0hex000000) | (0hex000000 @ registers_%d[0hex%02x] @ 0hex00000000) | (0hex0000 @ registers_%d[0hex%02x] @ 0hex0000000000) | (0hex00 @ registers_%d[0hex%02x] @ 0hex000000000000) | (registers_%d[0hex%02x] @ 0hex00000000000000)));\n", curblock, tmp, curvisited, registers, offset, registers, offset + 1, registers, offset + 2, registers, offset + 3, registers, offset + 4, registers, offset + 5, registers, offset + 6, registers, offset + 7);
		break;
      default:	break;
    }
    my_write(fdtrace, s, l);
    my_write(fddanger, s, l);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
  }
}

static
void instrumentWrTmpRdTmp(IRStmt* clone, UInt ltmp, UInt rtmp)
{
  if (VG_(HT_lookup)(taintedTemps, rtmp) != NULL)
  {
    Char s[256];
    taintTemp(ltmp);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    Int l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
    my_write(fdtrace, s, l);
    my_write(fddanger, s, l);
  }
}

static
void instrumentWrTmpUnop(IRStmt* clone, UInt ltmp, UInt rtmp, IROp op)
{
  if (VG_(HT_lookup)(taintedTemps, rtmp) != NULL)
  {
    taintTemp(ltmp);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    size2Node* node;
    Char s[256];
    Int l = 0;
    switch (op)
    {
      case Iop_1Uto8:   	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0bin0000000@t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_1Uto32:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0bin0000000000000000000000000000000@t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_1Uto64:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0bin000000000000000000000000000000000000000000000000000000000000000@t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_8Uto16:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0hex00@t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_8Uto32:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0hex000000@t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_8Uto64:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0hex00000000000000@t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_16Uto32: 	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0hex0000@t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_16Uto64: 	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0hex000000000000@t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_32Uto64: 	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0hex00000000@t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_1Sto8:  		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 8));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_1Sto16: 		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 16));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_1Sto32:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 32));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_1Sto64:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 64));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_8Sto16:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 16));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_8Sto32:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 32));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_8Sto64:  	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 64));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_16Sto32: 	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 32));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_16Sto64: 	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 64));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_32Sto64: 	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSX(t_%lx_%u_%u, 64));\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_16to8:
      case Iop_32to8:
      case Iop_64to8:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=t_%lx_%u_%u[7:0]);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_32to16:
      case Iop_64to16:
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=t_%lx_%u_%u[15:0]);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_64to32:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=t_%lx_%u_%u[31:0]);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_32to1:
      case Iop_64to1:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=t_%lx_%u_%u[0:0]);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_16HIto8:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=t_%lx_%u_%u[15:8]);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_32HIto16:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=t_%lx_%u_%u[31:16]);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_64HIto32:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=t_%lx_%u_%u[63:32]);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;


      case Iop_Not1:
      case Iop_Not8:
      case Iop_Not16:
      case Iop_Not32:
      case Iop_Not64:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=~t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      case Iop_ReinterpF32asI32:
      case Iop_ReinterpI32asF32:
      case Iop_ReinterpF64asI64:
      case Iop_ReinterpI64asF64:l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=t_%lx_%u_%u);\n", curblock, ltmp, curvisited, curblock, rtmp, curvisited);
				break;
      default:		break;
    }
    my_write(fdtrace, s, l);
    my_write(fddanger, s, l);
  }
}

static
UShort isPropagation2(IRExpr* arg1, IRExpr* arg2)
{
  if ((arg1->tag == Iex_RdTmp) && (arg2->tag != Iex_RdTmp))
  {
    //VG_(printf)("checking temp %d\n", arg1->Iex.RdTmp.tmp);
    return (VG_(HT_lookup)(taintedTemps, arg1->Iex.RdTmp.tmp) != NULL) ? 1 : 0;
  }
  else if ((arg1->tag != Iex_RdTmp) && (arg2->tag == Iex_RdTmp))
  {
    //VG_(printf)("checking temp %d\n", arg2->Iex.RdTmp.tmp);
    UShort b2 = (VG_(HT_lookup)(taintedTemps, arg2->Iex.RdTmp.tmp) != NULL) ? 1 : 0;
    return b2 << 1;
  }
  else if ((arg1->tag == Iex_RdTmp) && (arg2->tag == Iex_RdTmp))
  {
    //VG_(printf)("checking temp %d\n", arg1->Iex.RdTmp.tmp);
    //VG_(printf)("checking temp %d\n", arg2->Iex.RdTmp.tmp);
    UShort b1 = (VG_(HT_lookup)(taintedTemps, arg1->Iex.RdTmp.tmp) != NULL) ? 1 : 0;
    UShort b2 = (VG_(HT_lookup)(taintedTemps, arg2->Iex.RdTmp.tmp) != NULL) ? 1 : 0;
    return (b2 << 1) ^ b1;
  }
  else
  {
    return 0;
  }
}

static
UShort isPropagation3(IRExpr* arg1, IRExpr* arg2, IRExpr* arg3)
{
  UShort res = 0;
  if (arg1->tag == Iex_RdTmp)
  {
    res = (VG_(HT_lookup)(taintedTemps, arg1->Iex.RdTmp.tmp) != NULL) ? 1 : 0;
  }
  if (arg2->tag == Iex_RdTmp)
  {
    res |= (VG_(HT_lookup)(taintedTemps, arg2->Iex.RdTmp.tmp) != NULL) ? 0x2 : 0;
  }
  if (arg3->tag == Iex_RdTmp)
  {
    res |= (VG_(HT_lookup)(taintedTemps, arg3->Iex.RdTmp.tmp) != NULL) ? 0x4 : 0;
  }
  return res;
}

static
Bool firstTainted(UShort res)
{
  return res & 0x1;
}

static
Bool secondTainted(UShort res)
{
  return res & 0x2;
}

static 
Bool thirdTainted(UShort res)
{
  return res & 0x4;
}

static
void translateLong1(IRExpr* arg, Addr64 value, UShort taintedness)
{
  if (firstTainted(taintedness))
  {
    translateIRTmp(arg);
  }
  else
  {
    translateLongValue(arg, value);
  }
}

static
void translateLong2(IRExpr* arg, Addr64 value, UShort taintedness)
{
  if (secondTainted(taintedness))
  {
    translateIRTmp(arg);
  }
  else
  {
    translateLongValue(arg, value);
  }
}

static
void translate1(IRExpr* arg, IRExpr* value, UShort taintedness)
{
  if (firstTainted(taintedness))
  {
    translateIRTmp(arg);
  }
  else
  {
    translateValue(arg, value);
  }
}

static
void translate2(IRExpr* arg, IRExpr* value, UShort taintedness)
{
  if (secondTainted(taintedness))
  {
    translateIRTmp(arg);
  }
  else
  {
    translateValue(arg, value);
  }
}

static
void translate3(IRExpr* arg, IRExpr* value, UShort taintedness)
{
  if (thirdTainted(taintedness))
  {
    translateIRTmp(arg);
  }
  else
  {
    translateValue(arg, value);
  }
}

static
void printSizedTrue(UInt ltmp, Int fd)
{
  Char s[256];
  Int l = 0;
  switch (curNode->temps[ltmp].size)
  {
    case 1:	l = VG_(sprintf)(s, "0bin1");
		break;
    case 8:	l = VG_(sprintf)(s, "0hex01");
		break;
    case 16:	l = VG_(sprintf)(s, "0hex0001");
		break;
    case 32:	l = VG_(sprintf)(s, "0hex00000001");
		break;
    case 64:	l = VG_(sprintf)(s, "0hex0000000000000001");
		break;
    default:	break;
  }
  my_write(fd, s, l);
}

static
void printSizedFalse(UInt ltmp, Int fd)
{
  Char s[256];
  Int l = 0;
  switch (curNode->temps[ltmp].size)
  {
    case 1:	l = VG_(sprintf)(s, "0bin0");
		break;
    case 8:	l = VG_(sprintf)(s, "0hex00");
		break;
    case 16:	l = VG_(sprintf)(s, "0hex0000");
		break;
    case 32:	l = VG_(sprintf)(s, "0hex00000000");
		break;
    case 64:	l = VG_(sprintf)(s, "0hex0000000000000000");
		break;
    default:	break;
  }
  my_write(fd, s, l);
}

static
void instrumentWrTmpMux0X(IRStmt* clone, IRExpr* condValue, IRExpr* value0, IRExpr* valueX)
{
  IRExpr *cond, *arg0, *argX;
  cond = clone->Ist.WrTmp.data->Iex.Mux0X.cond;
  arg0 = clone->Ist.WrTmp.data->Iex.Mux0X.expr0;
  argX = clone->Ist.WrTmp.data->Iex.Mux0X.exprX;
  UShort r = isPropagation3(cond, arg0, argX);
  if (firstTainted(r) ||
      ( (cond == 0) && secondTainted(r) ) ||
      ( (cond != 0) && thirdTainted(r) ))
  {
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    UInt l, ltmp = clone->Ist.WrTmp.tmp;
    Char s[256];
    taintTemp(ltmp);
    l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF ", curblock, ltmp, curvisited);
    my_write(fdtrace, s, l);
    my_write(fddanger, s, l);
    translate1(cond, condValue, r);
    my_write(fdtrace, "=", 1);
    my_write(fddanger, "=", 1);
    printSizedFalse(cond->Iex.RdTmp.tmp, fdtrace);
    printSizedFalse(cond->Iex.RdTmp.tmp, fddanger);
    my_write(fdtrace, " THEN ", 6);
    my_write(fddanger, " THEN ", 6);
    translate2(arg0, value0, secondTainted(r));
    my_write(fdtrace, " ELSE ", 6);
    my_write(fddanger, " ELSE ", 6);
    translate3(argX, valueX, thirdTainted(r));
    my_write(fdtrace, " ENDIF);\n", 9);
    my_write(fddanger, " ENDIF);\n", 9);
  }
}

static
void instrumentWrTmpCCallArm(IRStmt* clone, IRExpr* value0, IRExpr* value1, IRExpr* value2)
{
  IRExpr *arg1, *arg2;
  arg1 = clone->Ist.WrTmp.data->Iex.CCall.args[1];
  arg2 = clone->Ist.WrTmp.data->Iex.CCall.args[2];
  UInt r = isPropagation2(arg1, arg2);
  if (r)
  {
//    UInt arg0 = clone->Ist.WrTmp.data->Iex.CCall.args[0]->Iex.Const.con->Ico.U32;
    UInt op = ((UInt) value0) >> 4;
    UInt ltmp = clone->Ist.WrTmp.tmp;
    Char s[256];
    Int l = 0;
    taintTemp(ltmp);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    switch (op)
    {
      case ARMCondLO:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVLT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, ",", 1);
			my_write(fddanger, ",", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, ") THEN ", 7);
			my_write(fddanger, ") THEN ", 7);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      case ARMCondHS:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVGE(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, ",", 1);
			my_write(fddanger, ",", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, ") THEN ", 7);
			my_write(fddanger, ") THEN ", 7);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      case ARMCondEQ:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF ", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, "=", 1);
			my_write(fddanger, "=", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, " THEN ", 6);
			my_write(fddanger, " THEN ", 6);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      case ARMCondNE:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF NOT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, "=", 1);
			my_write(fddanger, "=", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, ") THEN ", 7);
			my_write(fddanger, ") THEN ", 7);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      case ARMCondLS:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVLE(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, ",", 1);
			my_write(fddanger, ",", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, ") THEN ", 7);
			my_write(fddanger, ") THEN ", 7);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      case ARMCondHI:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVGT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, ",", 1);
			my_write(fddanger, ",", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, ") THEN ", 7);
			my_write(fddanger, ") THEN ", 7);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      case ARMCondLT:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVLT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, ",", 1);
			my_write(fddanger, ",", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, ") THEN ", 7);
			my_write(fddanger, ") THEN ", 7);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      case ARMCondGE:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVGE(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, ",", 1);
			my_write(fddanger, ",", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, ") THEN ", 7);
			my_write(fddanger, ") THEN ", 7);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      case ARMCondLE:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVLE(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, ",", 1);
			my_write(fddanger, ",", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, ") THEN ", 7);
			my_write(fddanger, ") THEN ", 7);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      case ARMCondGT:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVGT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
			my_write(fdtrace, ",", 1);
			my_write(fddanger, ",", 1);
      			translate2(arg2, value2, r);
			my_write(fdtrace, ") THEN ", 7);
			my_write(fddanger, ") THEN ", 7);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
			my_write(fdtrace, " ELSE ", 6);
			my_write(fddanger, " ELSE ", 6);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
			my_write(fdtrace, " ENDIF);\n", 9);
			my_write(fddanger, " ENDIF);\n", 9);
                	break;
      /* other conditions */
      default:  	break;
    }
  }
}

static
void instrumentWrTmpCCall(IRStmt* clone, IRExpr* arg1, IRExpr* arg2, UWord size, IRExpr* value1, IRExpr* value2)
{
  UShort r = isPropagation2(arg1, arg2);
  UInt op = clone->Ist.WrTmp.data->Iex.CCall.args[0]->Iex.Const.con->Ico.U32;
  UInt ltmp = clone->Ist.WrTmp.tmp;
  if (r)
  {
    Char s[256];
    Int l = 0;
    taintTemp(ltmp);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
    size %= 3;
#elif defined(VGP_amd64_linux)
    size %= 4;
#endif
    switch (op)
    {
      case X86CondB:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVLT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0],");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0],");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0],");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]) THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]) THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]) THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
                	break;
      case X86CondNB:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVGE(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0],");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0],");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0],");
			}
 			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]) THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]) THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]) THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
                	break;
      case X86CondZ:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF ", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]=");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]=");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]=");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]=");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]=");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0] THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0] THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0] THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0] THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0] THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
                	break;
      case X86CondNZ:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF NOT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
		      	translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]=");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]=");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]=");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]=");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]=");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]) THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]) THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]) THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
                	break;
      case X86CondBE:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVLE(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0],");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0],");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0],");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]) THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]) THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]) THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
			break;
      case X86CondNBE:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVGT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0],");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0],");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0],");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]) THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]) THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]) THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
			break;
      case X86CondL:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVLT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0],");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0],");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0],");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]) THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]) THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]) THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			break;
      case X86CondNL:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVGE(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0],");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0],");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0],");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]) THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]) THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]) THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			break;
      case X86CondLE:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVLE(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0],");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0],");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0],");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]) THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]) THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]) THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			break;
      case X86CondNLE:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVGT(", curblock, ltmp, curvisited);
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate1(arg1, value1, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0],");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0],");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0],");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0],");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			translate2(arg2, value2, r);
#if defined(VGP_x86_linux) || defined(VGP_arm_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#elif defined(VGP_amd64_linux)
			if (size == 0)
			{
			  l = VG_(sprintf)(s, "[63:0]) THEN ");
			}
			else if (size == 3)
			{
			  l = VG_(sprintf)(s, "[31:0]) THEN ");
			}
#endif
			else if (size == 1)
			{
		     	  l = VG_(sprintf)(s, "[7:0]) THEN ");
			}
			else if (size == 2)
			{
			  l = VG_(sprintf)(s, "[15:0]) THEN ");
			}
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedTrue(ltmp, fdtrace);
      			printSizedTrue(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ELSE ");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			printSizedFalse(ltmp, fdtrace);
      			printSizedFalse(ltmp, fddanger);
      			l = VG_(sprintf)(s, " ENDIF);\n");
			my_write(fdtrace, s, l);
			my_write(fddanger, s, l);
      			break;
      /*case X86CondS:
           X86CondNS:
           X86CondP:
           X86CondNP: */
      default:  	break;
    }
  }
}

static void instrumentWrTmpLongBinop(IRStmt* clone, IRExpr* diCounter, ULong value)
{
  IRExpr* arg1 = clone->Ist.WrTmp.data->Iex.Binop.arg1;
  IRExpr* arg2 = clone->Ist.WrTmp.data->Iex.Binop.arg2;
  UShort r = isPropagation2(arg1, arg2);
  if (!r) {
    return;
  }
  Bool firstT, secondT;
  ULong value1, value2;
  if (firstTainted(r))
  {
    firstT = True;
    value1 = 0;
    value2 = value;
  }
  if (secondTainted(r))
  {
    secondT = True;
    value1 = value;
    value2 = 1;
  }
  UInt ltmp = clone->Ist.WrTmp.tmp;
  UInt oprt = clone->Ist.WrTmp.data->Iex.Binop.op;
  if ((firstT && (diCounter == 1)) || (secondT && (diCounter == 2) && (!firstT)))
  {
    Char s[256];
    Int l = 0;
    if ((oprt != Iop_Shr64) && (oprt != Iop_Sar64) && (oprt != Iop_Shl64))
    {
      taintTemp(ltmp);
    }
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    switch (oprt)
    {
      case Iop_CmpEQ64:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF ", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translateLong1(arg1, value1, r);
    				l = VG_(sprintf)(s, "=");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translateLong2(arg2, value2, r);
    				l = VG_(sprintf)(s, " THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
      				printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
				l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_CmpLT64S:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVLT(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translateLong1(arg1, value1, r);
    				l = VG_(sprintf)(s, ", ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translateLong2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
      				printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_CmpLT64U:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVLT(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translateLong1(arg1, value1, r);
    				l = VG_(sprintf)(s, ", ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translateLong2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
      				printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_CmpLE64S:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVLE(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translateLong1(arg1, value1, r);
    				l = VG_(sprintf)(s, ", ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translateLong2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
      				printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_CmpLE64U:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVLE(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translateLong1(arg1, value1, r);
    				l = VG_(sprintf)(s, ", ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translateLong2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
      				printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_CmpNE64:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF NOT(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translateLong1(arg1, value1, r);
    				l = VG_(sprintf)(s, "=");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translateLong2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
      				printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Add64:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVPLUS(64,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Sub64: 		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSUB(64,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Mul64: 		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVMULT(64,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Or64: 		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, "|");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong2(arg2, value2, r);
				l = VG_(sprintf)(s, ");\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_And64: 		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, "&");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong2(arg2, value2, r);
				l = VG_(sprintf)(s, ");\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Xor64: 		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVXOR(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Sar64:		if (!firstT)
				{
				  break;
				}
                                taintTemp(ltmp);
                                l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=SBVDIV(64,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLongToPowerOfTwo(arg2, value2);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Shl64: 		if (!firstT)
				{
				  return;
				}
                                value2 = getDecimalValue(arg2, value2);
				if (value2 == 0)
                                {
                                  taintTemp(ltmp);
                                  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, ltmp, curvisited);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
                                  translateLong1(arg1, value1, r);
                                  l = VG_(sprintf)(s, ");\n");
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
                                }
/*				else if (value2 >= 64)
				{
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=0hex0000000000000000);\n", curblock, ltmp, curvisited);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				}*/
                                else if (value2 < 64)
                                {
                                  taintTemp(ltmp);
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=(", curblock, ltmp, curvisited);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				  translateLong1(arg1, value1, r);
				  l = VG_(sprintf)(s, " << %llu)[63:0]);\n", value2);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				}
				break;
      case Iop_Shr64: 		if (!firstT)
				{
				  return;
				}
                                value2 = getDecimalValue(arg2, value2);
				if (value2 == 0)
                                {
                                  taintTemp(ltmp);
                                  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, ltmp, curvisited);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
                                  translateLong1(arg1, value1, r);
                                  l = VG_(sprintf)(s, ");\n");
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
                                }
                                else
                                {
                                  taintTemp(ltmp);
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=(", curblock, ltmp, curvisited);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				  translateLong1(arg1, value1, r);
				  l = VG_(sprintf)(s, ">> %llu));\n", value2);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				}
				break;
      case Iop_DivU64:		if (checkDanger && (arg2->tag == Iex_RdTmp) && secondTainted(r) && (!enableFiltering || useFiltering()))
				{
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, arg2->Iex.RdTmp.tmp, curvisited);
				  my_write(fddanger, s, l);
				  printSizedFalse(arg2->Iex.RdTmp.tmp, fddanger);
				  l = VG_(sprintf)(s, ");\nQUERY(FALSE);\n");
                                  if (fdfuncFilter >= 0)
                                  {
                                    dumpCall();
                                  }
				  my_write(fddanger, s, l);
				}
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVDIV(64,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_DivS64:		if (checkDanger && (arg2->tag == Iex_RdTmp) && secondTainted(r) && (!enableFiltering || useFiltering()))
				{
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, arg2->Iex.RdTmp.tmp, curvisited);
				  my_write(fddanger, s, l);
				  printSizedFalse(arg2->Iex.RdTmp.tmp, fddanger);
				  l = VG_(sprintf)(s, ");\nQUERY(FALSE);\n");
                                  if (fdfuncFilter >= 0)
                                  {
                                    dumpCall();
                                  }
				  my_write(fddanger, s, l);
				}
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=SBVDIV(64,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
    }
  }
}

/* We need to pass 64-bit value through either r0;r1 or r2;r3 - 
   that's why formal params are value2 and then value1.
*/

#ifdef VGP_arm_linux
static
void instrumentWrTmpDivisionBinop(IRStmt* clone, IRExpr* value2, ULong value1)
{
  UInt ltmp = clone->Ist.WrTmp.tmp;
  UInt oprt = clone->Ist.WrTmp.data->Iex.Binop.op;
  IRExpr* arg1, *arg2;
  arg1 = clone->Ist.WrTmp.data->Iex.Binop.arg1;
  arg2 = clone->Ist.WrTmp.data->Iex.Binop.arg2;
  UShort r = isPropagation2(arg1, arg2);
#else
static
void instrumentWrTmpDivisionBinop(IRStmt* clone, IRExpr* arg1, IRExpr* arg2, Addr64 value1, IRExpr* value2)
{
  UShort r = isPropagation2(arg1, arg2);
  UInt ltmp = clone->Ist.WrTmp.tmp;
  UInt oprt = clone->Ist.WrTmp.data->Iex.Binop.op;
#endif
  if (r)
  {
    Char s[256];
    Int l = 0;
    ULong sarg;
    UShort size;
    taintTemp(ltmp);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    switch (oprt)
    {
      case Iop_DivModU64to32:	if (checkDanger && (arg2->tag == Iex_RdTmp) && secondTainted(r) && (!enableFiltering || useFiltering()))
				{
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, arg2->Iex.RdTmp.tmp, curvisited);
				  my_write(fddanger, s, l);
				  printSizedFalse(arg2->Iex.RdTmp.tmp, fddanger);
				  l = VG_(sprintf)(s, ");\nQUERY(FALSE);\n");
                                  if (fdfuncFilter >= 0)
                                  {
                                    dumpCall();
                                  }
				  my_write(fddanger, s, l);
				}
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVMOD(64,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",(0hex00000000 @ ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, ")) | (BVDIV(64,");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",(0hex00000000 @ ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, ")) @ 0hex00000000)[63:0]);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_DivModS64to32:	if (checkDanger && (arg2->tag == Iex_RdTmp) && secondTainted(r) && (!enableFiltering || useFiltering()))
				{
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, arg2->Iex.RdTmp.tmp, curvisited);
				  my_write(fddanger, s, l);
				  printSizedFalse(arg2->Iex.RdTmp.tmp, fddanger);
				  l = VG_(sprintf)(s, ");\nQUERY(FALSE);\n");
                                  if (fdfuncFilter >= 0)
                                  {
                                    dumpCall();
                                  }
				  my_write(fddanger, s, l);
				}
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=SBVMOD(64,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",(0hex00000000 @ ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, ")) | (SBVDIV(64,");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateLong1(arg1, value1, r);
				l = VG_(sprintf)(s, ",(0hex00000000 @ ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, ")) @ 0hex00000000)[63:0]);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      default: 			break;
    }
  }
}

static
void instrumentWrTmpBinop(IRStmt* clone, IRExpr* value1, IRExpr* value2)
{
  IRExpr* arg1 = clone->Ist.WrTmp.data->Iex.Binop.arg1;
  IRExpr* arg2 = clone->Ist.WrTmp.data->Iex.Binop.arg2;
  UShort r = isPropagation2(arg1, arg2);
  UInt ltmp = clone->Ist.WrTmp.tmp;
  UInt oprt = clone->Ist.WrTmp.data->Iex.Binop.op;
  if (r)
  {
    Char s[256];
    Int l = 0;
    ULong sarg;
    UShort size;
    taintTemp(ltmp);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    switch (oprt)
    {
      case Iop_CmpEQ8:
      case Iop_CmpEQ16:
      case Iop_CmpEQ32:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF ", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translate1(arg1, value1, r);
    				l = VG_(sprintf)(s, "=");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translate2(arg2, value2, r);
    				l = VG_(sprintf)(s, " THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_CmpLT32S:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVLT(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translate1(arg1, value1, r);
    				l = VG_(sprintf)(s, ", ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translate2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_CmpLT32U:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVLT(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translate1(arg1, value1, r);
    				l = VG_(sprintf)(s, ", ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translate2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_CmpLE32S:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF SBVLE(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translate1(arg1, value1, r);
    				l = VG_(sprintf)(s, ", ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translate2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_CmpLE32U:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF BVLE(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translate1(arg1, value1, r);
    				l = VG_(sprintf)(s, ", ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translate2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;

      case Iop_CmpNE8:
      case Iop_CmpNE16:
      case Iop_CmpNE32:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=IF NOT(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    			 	translate1(arg1, value1, r);
    				l = VG_(sprintf)(s, "=");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
    				translate2(arg2, value2, r);
    				l = VG_(sprintf)(s, ") THEN ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedTrue(ltmp, fdtrace);
      				printSizedTrue(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ELSE ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
                                printSizedFalse(ltmp, fdtrace);
      				printSizedFalse(ltmp, fddanger);
                                l = VG_(sprintf)(s, " ENDIF);\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Add8:
      case Iop_Add16:
      case Iop_Add32:		if (oprt == Iop_Add8) size = 8;
				else if (oprt == Iop_Add16) size = 16;
				else size = 32;
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVPLUS(%u,", curblock, ltmp, curvisited, size);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Sub8:
      case Iop_Sub16:
      case Iop_Sub32:		if (oprt == Iop_Sub8) size = 8;
				else if (oprt == Iop_Sub16) size = 16;
				else size = 32;
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVSUB(%u,", curblock, ltmp, curvisited, size);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Mul8:
      case Iop_Mul16:
      case Iop_Mul32:		if (oprt == Iop_Mul8) size = 8;
				else if (oprt == Iop_Mul16) size = 16;
				else size = 32;
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVMULT(%u,", curblock, ltmp, curvisited, size);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Or8:
      case Iop_Or16:
      case Iop_Or32:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, "|");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, ");\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_And8:
      case Iop_And16:
      case Iop_And32:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, "&");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, ");\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Xor8:
      case Iop_Xor16:
      case Iop_Xor32:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVXOR(", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;

      case Iop_Sar8:
      case Iop_Sar16:
      case Iop_Sar32:		if (secondTainted(r))
				{
				  //break;
				}
				if (oprt == Iop_Sar8) size = 8;
				else if (oprt == Iop_Sar16) size = 16;
				else if (oprt == Iop_Sar32) size = 32;
				else size = 64;
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=SBVDIV(%u,", curblock, ltmp, curvisited, size);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translateToPowerOfTwo(arg2, value2, size);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_Shl8:
      case Iop_Shl16:
      case Iop_Shl32:		if (secondTainted(r))
				{
				  //break;
				}
				if (oprt == Iop_Shl8) size = 8;
				else if (oprt == Iop_Shl16) size = 16;
				else size = 32;
				sarg = getDecimalValue(arg2, value2);
				if (sarg == 0)
                                {
                                  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, ltmp, curvisited);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
                                  translate1(arg1, value1, r);
                                  l = VG_(sprintf)(s, ");\n");
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
                                }
                                else
                                {
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=(", curblock, ltmp, curvisited);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				  translate1(arg1, value1, r);
				  l = VG_(sprintf)(s, " << %lu)", sarg);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				  l = VG_(sprintf)(s, "[%u:0]);\n", size - 1);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				}
				break;
      case Iop_Shr8:
      case Iop_Shr16:
      case Iop_Shr32:		if (secondTainted(r))
				{
				  //break;
				}
				sarg = getDecimalValue(arg2, value2);
				if (sarg == 0)
                                {
                                  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, ltmp, curvisited);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
                                  translate1(arg1, value1, r);
                                  l = VG_(sprintf)(s, ");\n");
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
                                }
                                else
                                {
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=(", curblock, ltmp, curvisited);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				  translate1(arg1, value1, r);
				  l = VG_(sprintf)(s, ">> %lu));\n", sarg);
				  my_write(fdtrace, s, l);
				  my_write(fddanger, s, l);
				}
				break;
      case Iop_DivU32:		if (checkDanger && (arg2->tag == Iex_RdTmp) && secondTainted(r) && (!enableFiltering || useFiltering()))
				{
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, arg2->Iex.RdTmp.tmp, curvisited);
				  my_write(fddanger, s, l);
				  printSizedFalse(arg2->Iex.RdTmp.tmp, fddanger);
				  l = VG_(sprintf)(s, ");\nQUERY(FALSE);\n");
                                  if (fdfuncFilter >= 0)
                                  {
                                    dumpCall();
                                  }
				  my_write(fddanger, s, l);
				}
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=BVDIV(32,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_DivS32:		if (checkDanger && (arg2->tag == Iex_RdTmp) && secondTainted(r) && (!enableFiltering || useFiltering()))
				{
				  l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, arg2->Iex.RdTmp.tmp, curvisited);
				  my_write(fddanger, s, l);
				  printSizedFalse(arg2->Iex.RdTmp.tmp, fddanger);
				  l = VG_(sprintf)(s, ");\nQUERY(FALSE);\n");
                                  if (fdfuncFilter >= 0)
                                  {
                                    dumpCall();
                                  }
      				  my_write(fddanger, s, l);
				}
				l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=SBVDIV(32,", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, ",");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, "));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_8HLto16:		l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, " @ 0hex00) | (0hex00 @ ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, ")));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_16HLto32:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, " @ 0hex0000) | (0hex0000 @ ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, ")));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      case Iop_32HLto64:	l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=((", curblock, ltmp, curvisited);
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate1(arg1, value1, r);
				l = VG_(sprintf)(s, " @ 0hex00000000) | (0hex00000000 @ ");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				translate2(arg2, value2, r);
				l = VG_(sprintf)(s, ")));\n");
				my_write(fdtrace, s, l);
				my_write(fddanger, s, l);
				break;
      default: 			break;
    }
  }
}

//unlikely to be ever used
static
void instrumentStoreGet(IRStmt* clone, IRExpr* storeAddr, UInt offset)
{
  UShort size;
  switch (clone->Ist.Store.data->Iex.Get.ty)
  {
    case Ity_I8:	size = 8;
			break;
    case Ity_I16:	size = 16;
			break;
    case Ity_I32:	size = 32;
			break;
    case Ity_I64:	size = 64;
			break;
  }
  UWord addr = (UWord) storeAddr;
  if (VG_(HT_lookup)(taintedRegisters, offset) != NULL)
  {
    taintMemory(addr, size);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    Char ss[256];
    Int l = 0;
    switch (size)
    {
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
      case 8:	l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := registers_%d[0hex%02x];\n", memory + 1, memory, addr, registers, offset);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory++;
                break;
      case 16:	l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := registers_%d[0hex%02x];\n", memory + 1, memory, addr, registers, offset);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := registers_%d[0hex%02x];\n", memory + 2, memory + 1, addr + 1, registers, offset + 1);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 2;
                break;
      case 32:  l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := registers_%d[0hex%02x];\n", memory + 1, memory, addr, registers, offset);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := registers_%d[0hex%02x];\n", memory + 2, memory + 1, addr + 1, registers, offset + 1);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := registers_%d[0hex%02x];\n", memory + 3, memory + 2, addr + 2, registers, offset + 2);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := registers_%d[0hex%02x];\n", memory + 4, memory + 3, addr + 3, registers, offset + 3);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 4;
                break;
#elif defined(VGP_amd64_linux)
      case 8:	l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 1, memory, addr, registers, offset);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory++;
                break;
      case 16:	l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 1, memory, addr, registers, offset);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 2, memory + 1, addr + 1, registers, offset + 1);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 2;
                break;
      case 32:  l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 1, memory, addr, registers, offset);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 2, memory + 1, addr + 1, registers, offset + 1);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 3, memory + 2, addr + 2, registers, offset + 2);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 4, memory + 3, addr + 3, registers, offset + 3);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 4;
                break;
      case 64:  l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 1, memory, addr, registers, offset);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 2, memory + 1, addr + 1, registers, offset + 1);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 3, memory + 2, addr + 2, registers, offset + 2);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 4, memory + 3, addr + 3, registers, offset + 3);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 5, memory + 4, addr + 4, registers, offset + 4);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 6, memory + 5, addr + 5, registers, offset + 5);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 7, memory + 6, addr + 6, registers, offset + 6);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := registers_%d[0hex%02x];\n", memory + 8, memory + 7, addr + 7, registers, offset + 7);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 8;
                break;
#endif
      default:	break;
    }
  }
  else
  {
    untaintMemory(addr, size);
  }
}

static
void instrumentStoreRdTmp(IRStmt* clone, IRExpr* storeAddr, UInt tmp, UInt ltmp)
{
  UShort size = curNode->temps[tmp].size;
  UWord addr = (UWord) storeAddr;
  if (checkDanger && (VG_(HT_lookup)(taintedTemps, ltmp) != NULL) && (!enableFiltering || useFiltering()))
  {
    Char s[256];
    Int l = 0;
    Addr addrs[256];
    //Int segs = VG_(am_get_client_segment_starts)(addrs, 256);
    NSegment* seg = VG_(am_find_nsegment)(addrs[0]);
    Char format[256];
    VG_(sprintf)(format, "ASSERT(BVLT(t_%%lx_%%u_%%u, 0hex%%0%ux));\nQUERY(FALSE);\n",
                 curNode->temps[ltmp].size / 4);
    if (fdfuncFilter >= 0)
    {
      dumpCall();
    }
    l = VG_(sprintf)(s, format, curblock, ltmp, curvisited, seg->start);
    my_write(fddanger, s, l);
  }
  if (VG_(HT_lookup)(taintedTemps, tmp) != NULL)
  {
    taintMemory(addr, size);
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    Char ss[256];
    Int l = 0;
    switch (curNode->temps[tmp].size)
    {
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
      case 8:	l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u;\n", memory + 1, memory, addr, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory++;
                break;
      case 16:	l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[7:0];\n", memory + 1, memory, addr, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[15:8];\n", memory + 2, memory + 1, addr + 1, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 2;
                break;
      case 32:  l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[7:0];\n", memory + 1, memory, addr, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[15:8];\n", memory + 2, memory + 1, addr + 1, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[23:16];\n", memory + 3, memory + 2, addr + 2, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[31:24];\n", memory + 4, memory + 3, addr + 3, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 4;
                break;
      case 64:  l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[7:0];\n", memory + 1, memory, addr, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[15:8];\n", memory + 2, memory + 1, addr + 1, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[23:16];\n", memory + 3, memory + 2, addr + 2, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[31:24];\n", memory + 4, memory + 3, addr + 3, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[39:32];\n", memory + 5, memory + 4, addr + 4, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[47:40];\n", memory + 6, memory + 5, addr + 5, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[55:48];\n", memory + 7, memory + 6, addr + 6, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(32) OF BITVECTOR(8) = memory_%d WITH [0hex%08x] := t_%lx_%u_%u[63:56];\n", memory + 8, memory + 7, addr + 7, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 8;
#elif defined(VGP_amd64_linux)
      case 8:	l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u;\n", memory + 1, memory, addr, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory++;
                break;
      case 16:	l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[7:0];\n", memory + 1, memory, addr, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[15:8];\n", memory + 2, memory + 1, addr + 1, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 2;
                break;
      case 32:  l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[7:0];\n", memory + 1, memory, addr, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[15:8];\n", memory + 2, memory + 1, addr + 1, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[23:16];\n", memory + 3, memory + 2, addr + 2, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[31:24];\n", memory + 4, memory + 3, addr + 3, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 4;
                break;
      case 64:  l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[7:0];\n", memory + 1, memory, addr, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[15:8];\n", memory + 2, memory + 1, addr + 1, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[23:16];\n", memory + 3, memory + 2, addr + 2, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[31:24];\n", memory + 4, memory + 3, addr + 3, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[39:32];\n", memory + 5, memory + 4, addr + 4, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[47:40];\n", memory + 6, memory + 5, addr + 5, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[55:48];\n", memory + 7, memory + 6, addr + 6, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                l = VG_(sprintf)(ss, "memory_%d : ARRAY BITVECTOR(64) OF BITVECTOR(8) = memory_%d WITH [0hex%016lx] := t_%lx_%u_%u[63:56];\n", memory + 8, memory + 7, addr + 7, curblock, tmp, curvisited);
                my_write(fdtrace, ss, l);
		my_write(fddanger, ss, l);
                memory += 8;
                break;
#endif
      default:	break;
    }
  }
  else
  {
    untaintMemory(addr, size);
  }
}

static
void instrumentStoreConst(IRStmt* clone, IRExpr* addr)
{
  UShort size;
  switch (clone->Ist.Store.data->Iex.Const.con->tag)
  {
    case Ico_U8:	size = 8;
			break;
    case Ico_U16:	size = 16;
			break;
    case Ico_U32:	size = 32;
			break;
    case Ico_U64:	size = 64;
			break;
  }
  untaintMemory((UWord) addr, size);
}

static
void instrumentExitRdTmp(IRStmt* clone, IRExpr* guard, UInt tmp, ULong dst)
{
  if ((VG_(HT_lookup)(taintedTemps, tmp) != NULL) && (!enableFiltering || useFiltering()))
  {
#ifdef TAINTED_TRACE_PRINTOUT
    ppIRStmt(clone);
    VG_(printf) ("\n");
#endif
    Char s[256];
    Int l = VG_(sprintf)(s, "ASSERT(t_%lx_%u_%u=", curblock, tmp, curvisited);
    my_write(fdtrace, s, l);
    my_write(fddanger, s, l);
    if (dumpPrediction)
    {
      actual[curdepth] = (Bool) guard;
    }
    if (guard == 1)
    {
      printSizedTrue(tmp, fdtrace);
      printSizedTrue(tmp, fddanger);
    }
    else
    {
      printSizedFalse(tmp, fdtrace);
      printSizedFalse(tmp, fddanger);
    }
    if (checkPrediction && !divergence && (curdepth < depth) && ((Bool) guard != prediction[curdepth]))
    {
      SysRes fd = VG_(open)("divergence.log", VKI_O_RDWR | VKI_O_TRUNC | VKI_O_CREAT, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO);
      divergence = True;
      VG_(write)(sr_Res(fd), &divergence, sizeof(Bool));
      VG_(write)(sr_Res(fd), &curdepth, sizeof(Int));
      VG_(close)(sr_Res(fd));
    }
    l = VG_(sprintf)(s, ");\n");
    my_write(fdtrace, s, l);
    my_write(fddanger, s, l);
    if (checkPrediction && (curdepth == depth) && !divergence)
    {
      SysRes fd = VG_(open)("divergence.log", VKI_O_RDWR | VKI_O_TRUNC | VKI_O_CREAT, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO);
      divergence = False;
      VG_(write)(sr_Res(fd), &divergence, sizeof(Bool));
      VG_(close)(sr_Res(fd));
    }
    if (curdepth >= depth)
    {
      l = VG_(sprintf)(s, "QUERY(FALSE);\n");
      if (fdfuncFilter >= 0)
      {
        dumpCall();
      }
      my_write(fdtrace, s, l);
    }
    curdepth++;
    if (!noInvertLimit && (curdepth > depth + invertdepth) && (fdfuncFilter == -1))
    {
      dump(fdtrace);
      dump(fddanger);
      if (dumpPrediction)
      {
        SysRes fd = VG_(open)("actual.log", VKI_O_RDWR | VKI_O_TRUNC | VKI_O_CREAT, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO);
        VG_(write)(sr_Res(fd), actual, (depth + invertdepth) * sizeof(Bool));
        VG_(close)(sr_Res(fd));
      }
      if (replace)
      {
        Int fd = sr_Res(VG_(open)("replace_data", VKI_O_RDWR, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO));
        VG_(write)(fd, &socketsNum, 4);
        Int i;
        for (i = 0; i < socketsNum; i++)
        {
          VG_(write)(fd, &(replace_data[i].length), sizeof(Int));
          VG_(write)(fd, replace_data[i].data, replace_data[i].length);
        }
        VG_(close)(fd);
      }
      VG_(exit)(0);
    }
  }
}

static
void instrumentPut(IRStmt* clone, IRSB* sbOut)
{
  IRDirty* di;
  UInt offset = clone->Ist.Put.offset;
  IRExpr* data = clone->Ist.Put.data;
  switch (data->tag)
  {
    case Iex_Load: 	di = unsafeIRDirty_0_N(0, "instrumentPutLoad",
						VG_(fnptr_to_fnentry)(&instrumentPutLoad), 
						mkIRExprVec_3(mkIRExpr_HWord((HWord)  clone), 
								mkIRExpr_HWord(offset), data->Iex.Load.addr));
                   	addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                   	break;
    case Iex_Get:      	di = unsafeIRDirty_0_N(0, "instrumentPutGet",
						VG_(fnptr_to_fnentry)(&instrumentPutGet), 
						mkIRExprVec_3(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord(offset), 
								mkIRExpr_HWord(data->Iex.Get.offset)));
                   	addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                   	break;
    case Iex_RdTmp:    	di = unsafeIRDirty_0_N(0, "instrumentPutRdTmp", 
						VG_(fnptr_to_fnentry)(&instrumentPutRdTmp), 
						mkIRExprVec_3(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord(offset), 
								mkIRExpr_HWord(data->Iex.RdTmp.tmp)));
                   	addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                   	break;
    case Iex_Const:	di = unsafeIRDirty_0_N(0, "instrumentPutConst",
						VG_(fnptr_to_fnentry)(&instrumentPutConst), 
						mkIRExprVec_2(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord(offset)));
                   	addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                   	break;
    default:		break;
  }
}

#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
static
IRExpr* adjustSize(IRSB* sbOut, IRTypeEnv* tyenv, IRExpr* arg)
{
  IRTemp tmp;
  IRExpr* e;
  IRType argty = typeOfIRExpr(tyenv, arg);
  switch (argty)
  {
    case Ity_I1:	tmp = newIRTemp(tyenv, Ity_I32);
			e = IRExpr_Unop(Iop_1Uto32, arg);
			addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			return IRExpr_RdTmp(tmp);
    case Ity_I8:	tmp = newIRTemp(tyenv, Ity_I32);
			e = IRExpr_Unop(Iop_8Uto32, arg);
			addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			return IRExpr_RdTmp(tmp);
    case Ity_I16:	tmp = newIRTemp(tyenv, Ity_I32);
			e = IRExpr_Unop(Iop_16Uto32, arg);
			addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			return IRExpr_RdTmp(tmp);
    case Ity_I32:	return arg;
    case Ity_I64:	if (arg->tag == Iex_Const)
			{
			  tmp = newIRTemp(tyenv, Ity_I64);
			  e = IRExpr_Const(arg->Iex.Const.con);
			  addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			  return IRExpr_RdTmp(tmp);
			}
			else
			{
			  return arg;
			}
    default:		break;
  }
}
#elif defined(VGP_amd64_linux)
static
IRExpr* adjustSize(IRSB* sbOut, IRTypeEnv* tyenv, IRExpr* arg)
{
  IRTemp tmp;
  IRExpr* e;
  IRType argty = typeOfIRExpr(tyenv, arg);
  switch (argty)
  {
    case Ity_I1:	tmp = newIRTemp(tyenv, Ity_I64);
			e = IRExpr_Unop(Iop_1Uto64, arg);
			addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			return IRExpr_RdTmp(tmp);
    case Ity_I8:	tmp = newIRTemp(tyenv, Ity_I64);
			e = IRExpr_Unop(Iop_8Uto64, arg);
			addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			return IRExpr_RdTmp(tmp);
    case Ity_I16:	tmp = newIRTemp(tyenv, Ity_I64);
			e = IRExpr_Unop(Iop_16Uto64, arg);
			addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			return IRExpr_RdTmp(tmp);
    case Ity_I32:	tmp = newIRTemp(tyenv, Ity_I64);
			e = IRExpr_Unop(Iop_32Uto64, arg);
			addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			return IRExpr_RdTmp(tmp);
    case Ity_I64:	if (arg->tag == Iex_Const)
			{
			  tmp = newIRTemp(tyenv, Ity_I64);
			  e = IRExpr_Const(arg->Iex.Const.con);
			  addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			  return IRExpr_RdTmp(tmp);
			}
			else
			{
			  return arg;
			}
    case Ity_I128:	if (arg->tag == Iex_Const)
			{
			  tmp = newIRTemp(tyenv, Ity_I128);
			  e = IRExpr_Const(arg->Iex.Const.con);
			  addStmtToIRSB(sbOut, IRStmt_WrTmp(tmp, e));
			  return IRExpr_RdTmp(tmp);
			}
			else
			{
			  return arg;
			}
    default:		break;
  }
}
#endif

static
void instrumentWrTmp(IRStmt* clone, IRSB* sbOut, IRTypeEnv* tyenv)
{
  IRDirty* di, *di2;
  IRExpr* arg0, * arg1,* arg2,* arg3;
  UInt tmp = clone->Ist.WrTmp.tmp;
  IRExpr* data = clone->Ist.WrTmp.data;
  IRExpr* value0, *value1,* value2;
  switch (data->tag)
  {
    case Iex_Load: 	if (data->Iex.Load.addr->tag == Iex_RdTmp)
			{
			  di = unsafeIRDirty_0_N(0, "instrumentWrTmpLoad", 
						 VG_(fnptr_to_fnentry)(&instrumentWrTmpLoad), 
						 mkIRExprVec_2(mkIRExpr_HWord((HWord) clone), data->Iex.Load.addr));
                          addStmtToIRSB(sbOut, IRStmt_Dirty(di));
			}
                   	break;
    case Iex_Get:  	di = unsafeIRDirty_0_N(0, "instrumentWrTmpGet", 
						VG_(fnptr_to_fnentry)(&instrumentWrTmpGet), 
						mkIRExprVec_3(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord(tmp),
								mkIRExpr_HWord(data->Iex.Get.offset)));
                   	addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                   	break;
    case Iex_RdTmp:	di = unsafeIRDirty_0_N(0, "instrumentWrTmpRdTmp",
						VG_(fnptr_to_fnentry)(&instrumentWrTmpRdTmp), 
						mkIRExprVec_3(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord(tmp), 
								mkIRExpr_HWord(data->Iex.RdTmp.tmp)));
                   	addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                   	break;
    case Iex_Unop:	if (data->Iex.Unop.arg->tag == Iex_RdTmp)
                        {
			  di = unsafeIRDirty_0_N(0, "instrumentWrTmpUnop", 
						 VG_(fnptr_to_fnentry)(&instrumentWrTmpUnop), 
						 mkIRExprVec_4(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord(tmp), 
								mkIRExpr_HWord(data->Iex.Unop.arg->Iex.RdTmp.tmp),
								mkIRExpr_HWord(data->Iex.Unop.op)));
                   	  addStmtToIRSB(sbOut, IRStmt_Dirty(di));
 			}
 			break;
    case Iex_Binop:	arg1 = data->Iex.Binop.arg1;
			arg2 = data->Iex.Binop.arg2;
                        value1 = adjustSize(sbOut, tyenv, arg1);
                        value2 = adjustSize(sbOut, tyenv, arg2);
                        if ((data->Iex.Binop.op == Iop_CmpEQ64) ||
      			    (data->Iex.Binop.op == Iop_CmpLT64S) ||
      			    (data->Iex.Binop.op == Iop_CmpLT64U) ||
      			    (data->Iex.Binop.op == Iop_CmpLE64S) ||
      			    (data->Iex.Binop.op == Iop_CmpLE64U) ||
      			    (data->Iex.Binop.op == Iop_CmpNE64) ||
      			    (data->Iex.Binop.op == Iop_Add64) ||
      			    (data->Iex.Binop.op == Iop_Sub64) ||
      			    (data->Iex.Binop.op == Iop_Mul64) ||
      			    (data->Iex.Binop.op == Iop_Or64) ||
      			    (data->Iex.Binop.op == Iop_And64) ||
      			    (data->Iex.Binop.op == Iop_Xor64) ||
      			    (data->Iex.Binop.op == Iop_Sar64) ||
      			    (data->Iex.Binop.op == Iop_Shl64) ||
      			    (data->Iex.Binop.op == Iop_Shr64) ||
      			    (data->Iex.Binop.op == Iop_DivU64) ||
      			    (data->Iex.Binop.op == Iop_DivS64))
			{
/*
     Each LongBinop statement is instrumented two times consequently with
   (diCounter = 1, value = arg2_value) and (diCounter = 2, value = arg1_value).
   instrumentWrTmpLongBinop only does actual work if either
     diCounter == 1 AND arg1 is tainted
   OR
     diCounter == 2 AND arg2 is tainted AND arg1 is NOT tainted
   to ensure that no duplicates appear in tainted trace log.
*/

                          di = unsafeIRDirty_0_N(0, "instrumentWrTmpLongBinop",
						 VG_(fnptr_to_fnentry)(&instrumentWrTmpLongBinop),
						 mkIRExprVec_3(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord(1), value2));
                          di2 = unsafeIRDirty_0_N(0, "instrumentWrTmpLongBinop",
						  VG_(fnptr_to_fnentry)(&instrumentWrTmpLongBinop),
						  mkIRExprVec_3(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord(2), value1));
                          addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                          addStmtToIRSB(sbOut, IRStmt_Dirty(di2));
			}
			else if ((data->Iex.Binop.op == Iop_DivModU64to32) ||
				 (data->Iex.Binop.op == Iop_DivModS64to32))
			{
#ifdef VGP_arm_linux
			  di = unsafeIRDirty_0_N(0, "instrumentWrTmpDivisionBinop", 
						 VG_(fnptr_to_fnentry)(&instrumentWrTmpDivisionBinop),
						 mkIRExprVec_3(mkIRExpr_HWord((HWord) clone), value2, value1));
#else
			  di = unsafeIRDirty_0_N(0, "instrumentWrTmpDivisionBinop", VG_(fnptr_to_fnentry)(&instrumentWrTmpDivisionBinop), mkIRExprVec_5(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord((HWord) arg1), mkIRExpr_HWord((HWord) arg2), value1, value2));
#endif
                     	  addStmtToIRSB(sbOut, IRStmt_Dirty(di));
			}
			else if ((data->Iex.Binop.op - Iop_INVALID <= 128) &&
                                 (typeOfIRExpr(tyenv, arg1) - Ity_INVALID <= 5) &&
                                 (typeOfIRExpr(tyenv, arg2) - Ity_INVALID <= 5))
                        //do not try to instrument floating point operations
			{
                          di = unsafeIRDirty_0_N(0, "instrumentWrTmpBinop", 
						 VG_(fnptr_to_fnentry)(&instrumentWrTmpBinop), 
						 mkIRExprVec_3(mkIRExpr_HWord((HWord) clone), value1, value2));
                   	  addStmtToIRSB(sbOut, IRStmt_Dirty(di));
			}
			break;
    case Iex_Mux0X:	arg0 = data->Iex.Mux0X.cond;
			arg1 = data->Iex.Mux0X.expr0;
			arg2 = data->Iex.Mux0X.exprX;
			value0 = adjustSize(sbOut, tyenv, arg0);
			value1 = adjustSize(sbOut, tyenv, arg1);
			value2 = adjustSize(sbOut, tyenv, arg2);
			di = unsafeIRDirty_0_N(0, "instrumentWrTmpMux0X",
						VG_(fnptr_to_fnentry)(&instrumentWrTmpMux0X),
						mkIRExprVec_4(mkIRExpr_HWord((HWord) clone), value0, value1, value2));
			addStmtToIRSB(sbOut, IRStmt_Dirty(di));
			break;
    case Iex_CCall:	if (!VG_(strcmp)(data->Iex.CCall.cee->name, "armg_calculate_condition")) 
			{
			  arg0 = data->Iex.CCall.args[0]; //ARMCondcode << 4 | c_op
			  arg1 = data->Iex.CCall.args[1]; //condition arg1
			  arg2 = data->Iex.CCall.args[2]; //condition arg2
			  arg3 = data->Iex.CCall.args[3]; //something
			  value1 = adjustSize(sbOut, tyenv, arg1);
			  value2 = adjustSize(sbOut, tyenv, arg2);
			  value0 = adjustSize(sbOut, tyenv, arg0);
			  di = unsafeIRDirty_0_N(0, "instrumentWrTmpCCallArm", 
						 VG_(fnptr_to_fnentry)(&instrumentWrTmpCCallArm), 
						 mkIRExprVec_4(mkIRExpr_HWord((HWord) clone), value0, value1, value2));
			  addStmtToIRSB(sbOut, IRStmt_Dirty(di));
			}
                        else if (!VG_(strcmp)(data->Iex.CCall.cee->name, "x86g_calculate_condition") ||
                                 !VG_(strcmp)(data->Iex.CCall.cee->name, "amd64g_calculate_condition"))
			{
                          IRExpr* arg0 = data->Iex.CCall.args[0];
			  IRExpr* arg1 = data->Iex.CCall.args[1];
			  IRExpr* arg2 = data->Iex.CCall.args[2];
			  IRExpr* arg3 = data->Iex.CCall.args[3];
			  IRExpr* arg4 = data->Iex.CCall.args[4];
			  {
                            IRExpr* value2 = adjustSize(sbOut, tyenv, arg2);
                            IRExpr* value3 = adjustSize(sbOut, tyenv, arg3);
			    IRExpr* value1 = adjustSize(sbOut, tyenv, arg1);
			    di = unsafeIRDirty_0_N(0, "instrumentWrTmpCCall", VG_(fnptr_to_fnentry)(&instrumentWrTmpCCall), mkIRExprVec_6(mkIRExpr_HWord((HWord) clone), mkIRExpr_HWord((HWord) arg2), mkIRExpr_HWord((HWord) arg3), value1, value2, value3));
                   	    addStmtToIRSB(sbOut, IRStmt_Dirty(di));
			  }
			}
			break;
    default:		break;
  }
}

static
void instrumentStore(IRStmt* clone, IRSB* sbOut)
{
  IRDirty* di;
  IRExpr* addr = clone->Ist.Store.addr;
  IRExpr* data = clone->Ist.Store.data;
  switch (data->tag)
  {
    case Iex_Get:  	di = unsafeIRDirty_0_N(0, "instrumentStoreGet", VG_(fnptr_to_fnentry)(&instrumentStoreGet), mkIRExprVec_3(mkIRExpr_HWord((HWord) clone), addr, mkIRExpr_HWord(data->Iex.Get.offset)));
                   	addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                   	break;
    case Iex_RdTmp:	if (addr->tag == Iex_RdTmp)
			{
			  di = unsafeIRDirty_0_N(0, "instrumentStoreRdTmp", VG_(fnptr_to_fnentry)(&instrumentStoreRdTmp), mkIRExprVec_4(mkIRExpr_HWord((HWord) clone), addr, mkIRExpr_HWord(data->Iex.RdTmp.tmp), mkIRExpr_HWord(addr->Iex.RdTmp.tmp)));
                   	  addStmtToIRSB(sbOut, IRStmt_Dirty(di));
			}
                   	break;
    case Iex_Const:	di = unsafeIRDirty_0_N(0, "instrumentStoreConst", VG_(fnptr_to_fnentry)(&instrumentStoreConst), mkIRExprVec_2(mkIRExpr_HWord((HWord) clone), addr));
                   	addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                   	break;
    default:		break;
  }
}

static
void instrumentExit(IRStmt* clone, IRSB* sbOut, IRTypeEnv* tyenv)
{
  IRDirty* di;
  IRExpr* etemp;
  IRExpr* guard = clone->Ist.Exit.guard;
  ULong dst = clone->Ist.Exit.dst->Ico.U64;
  switch (guard->tag)
  {
    case Iex_RdTmp:	etemp = adjustSize(sbOut, tyenv, guard);
                	di = unsafeIRDirty_0_N(0, "instrumentExitRdTmp", VG_(fnptr_to_fnentry)(&instrumentExitRdTmp), mkIRExprVec_4(mkIRExpr_HWord((HWord) clone), etemp, mkIRExpr_HWord(guard->Iex.RdTmp.tmp), mkIRExpr_HWord(dst)));
                   	addStmtToIRSB(sbOut, IRStmt_Dirty(di));
                   	break;
    default:		break;
  }
}

static
void createTaintedTemp(UInt basicBlockLowerBytes, UInt basicBlockUpperBytes)
{
  //VG_(printf)("4126\n");
  //Addr64 bbaddr = (((Addr64) basicBlockUpperBytes) << 32) ^ basicBlockLowerBytes;
  //VG_(printf)("4128\n");
  //VG_(printf)("looking for %x\n", basicBlockLowerBytes);
  curNode = VG_(HT_lookup)(tempSizeTable, basicBlockLowerBytes);
  //VG_(printf)("curNode=%x\n", curNode);
  //VG_(printf)("4130\n");
  curNode->visited++;
  //VG_(printf)("4132\n");
  curvisited = curNode->visited - 1;
  //VG_(printf)("4134\n");
  curblock = basicBlockLowerBytes;
  //VG_(printf)("4136\n");
  if (taintedTemps != NULL)
  {
  //VG_(printf)("4139\n");
    VG_(HT_destruct)(taintedTemps);
  //VG_(printf)("4141\n");
  }
  //VG_(printf)("4143\n");
  taintedTemps = VG_(HT_construct)("taintedTemps");
  //VG_(printf)("4145\n");
}

static
IRSB* tg_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn,
                      VexGuestLayout* layout,
                      VexGuestExtents* vge,
                      IRType gWordTy, IRType hWordTy )
{
   IRTypeEnv* tyenv = sbIn->tyenv;
   IRType* types = tyenv->types;
   UInt used = tyenv->types_used;
   Int i = 0;
   IRDirty*   di;
   IRSB*      sbOut;
   Char       fnname[100];
   IRType     type;
   Addr       iaddr = 0, dst;
   UInt       ilen = 0;
   Bool       condition_inverted = False;

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   /* Set up SB */
   sbOut = deepCopyIRSBExceptStmts(sbIn);

   curblock = vge->base[0];

    curNode = VG_(malloc)("taintMemoryNode", sizeof(sizeNode));
    curNode->key = curblock;
    curNode->temps = VG_(malloc)("temps", tyenv->types_used * sizeof(size2Node));
    for (i = 0; i < tyenv->types_used; i++)
    {
      switch (tyenv->types[i])
      {
        case Ity_I1:	curNode->temps[i].size = 1;
			break;
        case Ity_I8:	curNode->temps[i].size = 8;
			break;
        case Ity_I16:	curNode->temps[i].size = 16;
			break;
        case Ity_I32:	curNode->temps[i].size = 32;
			break;
        case Ity_I64:	curNode->temps[i].size = 64;
			break;
        case Ity_I128:	curNode->temps[i].size = 128;
			break;
        case Ity_F32:	curNode->temps[i].size = 32;
			break;
        case Ity_F64:	curNode->temps[i].size = 64;
			break;
        case Ity_V128:	curNode->temps[i].size = 128;
			break;
      }
    }
    curNode->visited = 0;
 //   VG_(printf)("added key=%x curNode=%x\n", curNode->key, curNode);
    VG_(HT_add_node)(tempSizeTable, curNode);
//    VG_(printf)("lookup: %x\n", VG_(HT_lookup)(tempSizeTable, curblock));

   curvisited = 0;
   UInt iaddrUpperBytes, iaddrLowerBytes, basicBlockUpperBytes, basicBlockLowerBytes;
   basicBlockUpperBytes = (vge->base[0] & ((long long int) 0xffffffff00000000)) >> 32;
   basicBlockLowerBytes = vge->base[0] & ((long long int) 0x00000000ffffffff);

   i = 0;
   di = unsafeIRDirty_0_N(0, "createTaintedTemp", VG_(fnptr_to_fnentry)(&createTaintedTemp), mkIRExprVec_2(mkIRExpr_HWord(vge->base[0]), mkIRExpr_HWord(basicBlockUpperBytes)));
   addStmtToIRSB(sbOut, IRStmt_Dirty(di));
   for (;i < sbIn->stmts_used; i++)
   {
     IRStmt* clone = deepMallocIRStmt((IRStmt*) sbIn->stmts[i]);
     switch (sbIn->stmts[i]->tag)
     {
       case Ist_NoOp:
         break;
       case Ist_IMark:
         iaddrUpperBytes = (sbIn->stmts[i]->Ist.IMark.addr & ((long long int) 0xffffffff00000000)) >> 32;
         iaddrLowerBytes = sbIn->stmts[i]->Ist.IMark.addr & ((long long int) 0x00000000ffffffff);
         basicBlockUpperBytes = (vge->base[0] & ((long long int) 0xffffffff00000000)) >> 32;
         basicBlockLowerBytes = vge->base[0] & ((long long int) 0x00000000ffffffff);
	 di = unsafeIRDirty_0_N(0, "instrumentIMark", VG_(fnptr_to_fnentry)(&instrumentIMark), mkIRExprVec_3(mkIRExpr_HWord(iaddrLowerBytes)/*, mkIRExpr_HWord(iaddrUpperBytes)*/, mkIRExpr_HWord(basicBlockLowerBytes)/*, mkIRExpr_HWord(basicBlockUpperBytes)*/, mkIRExpr_HWord(tyenv->types_used)));
         addStmtToIRSB(sbOut, IRStmt_Dirty(di));
         break;
       case Ist_AbiHint:
         break;
       case Ist_Put:
         instrumentPut(clone, sbOut);
         break;
       case Ist_PutI:
         break;
       case Ist_WrTmp:
         instrumentWrTmp(clone, sbOut, sbOut->tyenv);
         break;
       case Ist_Store:
         instrumentStore(clone, sbOut);
         break;
       case Ist_Dirty:
         break;
       case Ist_MBE:
         break;
       case Ist_Exit:
         instrumentExit(clone, sbOut, sbOut->tyenv);
         break;
     }
     addStmtToIRSB(sbOut, sbIn->stmts[i]);
   }
   return sbOut;
}

static void tg_fini(Int exitcode)
{
  dump(fdtrace);
  dump(fddanger);
  if (fdfuncFilter >= 0)
  {
    dump(fdfuncFilter);
  }
  if (dumpPrediction)
  {
    SysRes fd = VG_(open)("actual.log", VKI_O_RDWR | VKI_O_TRUNC | VKI_O_CREAT, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO);
    if (noInvertLimit)
    {
      curdepth --;
      VG_(write) (sr_Res(fd), &curdepth, sizeof(Int));
    }
    VG_(write)(sr_Res(fd), actual, (depth + invertdepth) * sizeof(Bool));
    VG_(close)(sr_Res(fd));
  }
  if (checkPrediction && !divergence)
  {
    SysRes fd = VG_(open)("divergence.log", VKI_O_RDWR | VKI_O_TRUNC | VKI_O_CREAT, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO);
    divergence = False;
    VG_(write)(sr_Res(fd), &divergence, sizeof(Bool));
    VG_(close)(sr_Res(fd));
  }
  if (replace)
  {
    Int fd = sr_Res(VG_(open)("replace_data", VKI_O_RDWR, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO));
    VG_(write)(fd, &socketsNum, 4);
    Int i;
    for (i = 0; i < socketsNum; i++)
    {
      VG_(write)(fd, &(replace_data[i].length), sizeof(Int));
      VG_(write)(fd, replace_data[i].data, replace_data[i].length);
    }
    VG_(close)(fd);
  }
  if (fdfuncFilter >= 0) 
  {
    VG_(close)(fdfuncFilter);
  }
//  if (inputFilterEnabled) 
//  {
//    VG_(deleteXA)(inputFilter);
//  }
}

Int filenum = 0;

static Bool tg_process_cmd_line_option(Char* arg)
{
  Char* inputfile;
  Char* addr;
  Char* filtertype;
  Char* funcname;
  Char* funcfilterfile;
  Char* inputfilterfile;
  Char* dumpfile;
  Char* checkArgv;
  if (VG_INT_CLO(arg, "--startdepth", depth))
  {
    depth -= 1;
    return True;
  }
  else if (VG_INT_CLO(arg, "--invertdepth", invertdepth))
  {
    if (invertdepth == 0)
    {
      noInvertLimit = True;
    }
    return True;
  }
  else if (VG_STR_CLO(arg, "--port", addr))
  {
    port = (UShort) VG_(strtoll10)(addr, NULL);
    return True;
  }
  else if (VG_STR_CLO(arg, "--func-name", funcname))
  {
    parseFnName(funcname);
    enableFiltering = True;
    return True;
  }
  else if (VG_STR_CLO(arg, "--func-filter-file", funcfilterfile))
  {
    Int fd = sr_Res(VG_(open)(funcfilterfile, VKI_O_RDWR, 0));
    parseFuncFilterFile(fd);
    enableFiltering = True;
    VG_(close)(fd);
    return True;
  }
  else if (VG_STR_CLO(arg, "--mask", inputfilterfile))
  {
    if (!parseMask(inputfilterfile))
    {
      VG_(printf)("couldn't parse mask file\n");
      VG_(exit)(1);
    }
    //VG_(printf)("printInputOffsets\n");
    //printInputOffsets();
    inputFilterEnabled = True;
    return True;
  }
  else if (VG_STR_CLO(arg, "--host", addr))
  {
    Char* dot = VG_(strchr)(addr, '.');
    *dot = '\0';
    ip1 = (UShort) VG_(strtoll10)(addr, NULL);
    addr = dot + 1;
    dot = VG_(strchr)(addr, '.');
    *dot = '\0';
    ip2 = (UShort) VG_(strtoll10)(addr, NULL);
    addr = dot + 1;
    dot = VG_(strchr)(addr, '.');
    *dot = '\0';
    ip3 = (UShort) VG_(strtoll10)(addr, NULL);
    addr = dot + 1;
    ip4 = (UShort) VG_(strtoll10)(addr, NULL);
    return True;
  }
  else if (VG_STR_CLO(arg, "--file", inputfile))
  {
    if (inputfiles == NULL)
    {
      inputfiles = VG_(HT_construct)("inputfiles");
    }
    stringNode* node;
    node = VG_(malloc)("stringNode", sizeof(stringNode));
    node->key = hashCode(inputfile);
    node->filename = inputfile;
    node->declared = False;
    node->filenum = filenum++;
    VG_(HT_add_node)(inputfiles, node);
    return True;
  }
  else if (VG_STR_CLO(arg, "--check-argv", checkArgv))
  {
    Int fdargv = sr_Res(VG_(open) ("argv.log", VKI_O_WRONLY | VKI_O_TRUNC | VKI_O_CREAT, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO));
    my_write(fdtrace, "file_argv_dot_log : ARRAY BITVECTOR(32) OF BITVECTOR(8);\n", 57);
    my_write(fddanger, "file_argv_dot_log : ARRAY BITVECTOR(32) OF BITVECTOR(8);\n", 57);
    HChar** argv = VG_(client_argv);
    Int i, j, argvSize = 0;
    Char buf[10];
    Int eqPos;
    Int argc = VG_(sizeXA) (VG_(args_for_client));
    Int fdargl = sr_Res(VG_(open) ("arg_lengths", VKI_O_RDONLY, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO));
    Int* argLength = VG_(malloc) ("argLength", argc * sizeof(Int));
    for (i = 0; i < argc; i ++)
    {
      VG_(read) (fdargl, &(argLength[i]), sizeof(Int));
    }
    Int *argFilterUnits = VG_(malloc) ("argFilterUnits", argc * sizeof(Int));
    for (i = 0; i < argc; i ++)
    {
      argFilterUnits[i] = -1;
    }
    if (!VG_(strcmp) (checkArgv, "all"))
    {
      for (i = 0; i < argc; i ++)
      {
        argFilterUnits[i] = 1;
      }
    }
    else
    {
      parseArgvMask(checkArgv, argFilterUnits);
    }
    for (i = 0; i < argc; i ++)
    {
      if (argFilterUnits[i] > 0)
      {
        eqPos = -1;
        if (protectArgName)
        {
          HChar* eqPosC = VG_(strchr) (argv[i], '=');
          if (eqPosC != NULL)
          {
            eqPos = eqPosC - argv[i];
          }
        }
        for (j = 0; j < VG_(strlen) (argv[i]) + 1; j ++)
        {
          if (j > eqPos)
          { 
            taintMemoryFromArgv(argv[i] + j, argvSize);
          }
          argvSize ++;
          VG_(write) (fdargv, argv[i] + j, 1);
        }
      }
      else
      {
        for (j = 0; j < VG_(strlen) (argv[i]) + 1; j ++)
        {
          argvSize ++;
          VG_(write) (fdargv, argv[i] + j, 1);
        }
      }
      for (j = VG_(strlen) (argv[i]) + 1; j < argLength[i] + 1; j ++)
      {
        argvSize ++;
        VG_(write) (fdargv, "\0", 1);
      }
    }
    VG_(free) (argFilterUnits);
    VG_(free) (argLength);
    VG_(close) (fdargv);
    VG_(close) (fdargl);
    return True;
  }
  else if (VG_BOOL_CLO(arg, "--check-danger", checkDanger))
  {
    return True;
  }
  else if (VG_STR_CLO(arg, "--dump-file", dumpfile))
  {
    fdfuncFilter = sr_Res(VG_(open) (dumpfile, VKI_O_WRONLY | VKI_O_CREAT | VKI_O_TRUNC, 
                              VKI_S_IRUSR | VKI_S_IWUSR | VKI_S_IRGRP | VKI_S_IWGRP | VKI_S_IROTH | VKI_S_IWOTH));
    return True;
  }
  else if (VG_BOOL_CLO(arg, "--suppress-subcalls", suppressSubcalls))
  {
    return True;
  }
  else if (VG_BOOL_CLO(arg, "--protect-arg-name", protectArgName))
  {
    return True;
  }
  else if (VG_BOOL_CLO(arg, "--sockets",  sockets))
  {
    return True;
  }
  else if (VG_BOOL_CLO(arg, "--datagrams",  datagrams))
  {
    return True;
  }
  else if (VG_BOOL_CLO(arg, "--replace",  replace))
  {
    Int fd = sr_Res(VG_(open)("replace_data", VKI_O_RDWR, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO));
    VG_(read)(fd, &socketsNum, 4);
    socketsBoundary = socketsNum;
    if (socketsNum > 0)
    {
      replace_data = (replaceData*) VG_(malloc)("replace_data", socketsNum * sizeof(replaceData));
      Int i;
      for (i = 0; i < socketsNum; i++)
      {
        VG_(read)(fd, &(replace_data[i].length), sizeof(Int));
        replace_data[i].data = (Char*) VG_(malloc)("replace_data", replace_data[i].length);
        VG_(read)(fd, replace_data[i].data, replace_data[i].length);
      }
    }
    else
    {
      replace_data = NULL;
    }
    VG_(close)(fd);
    return True;
  }
  else if VG_BOOL_CLO(arg, "--check-prediction",  checkPrediction)
  {
    if (depth > 0)
    {
      checkPrediction = True;
      SysRes fd = VG_(open)("prediction.log", VKI_O_RDWR, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO);
      prediction = VG_(malloc)("prediction", depth * sizeof(Bool));
      VG_(read)(sr_Res(fd), prediction, depth * sizeof(Bool));
      VG_(close)(sr_Res(fd));
    }
    else
    {
      checkPrediction = False;
    }
    return True;
  }
  else if VG_BOOL_CLO(arg, "--dump-prediction",  dumpPrediction)
  {
    if (dumpPrediction)
    {
      actual = VG_(malloc)("prediction", (depth + invertdepth) * sizeof(Bool));
    }
    return True;
  }
  else
  {
    return False;
  }
}

static void tg_print_usage()
{
  VG_(printf)(
	"    --startdepth=<number>		the number of conditional jumps after\n"
	"					which the queries for the invertation of\n"
	"					consequent conditional jumps are emitted\n"
	"    --invertdepth=<number>		number of queries to be emitted\n"
	"    --filename=<name>			the name of the file with the input data\n"
	"    --dump-prediction=<yes, no>	indicates whether the file with conditional\n"
	"		 			jumps outcome prediction should be dumped\n"
	"    --check-prediction=<yes, no>	indicates whether the file with\n"
	" 					previously dumped prediction should\n"
	"					be used to check for the occurence\n"
	"					of divergence\n"
        "  special options for sockets:\n"
        "    --sockets=<yes, no>                mark data read from TCP sockets as tainted\n"
        "    --datagrams=<yes, no>              mark data read from UDP sockets as tainted\n"
        "    --host=<IPv4 address>              IP address of the network connection (for TCP sockets only)\n"
        "    --port=<number>                    port number of the network connection (for TCP sockets only)\n"
        "    --replace=<name>                   name of the file with data for replacement\n"
  );
}

static void tg_print_debug_usage()
{
  VG_(printf)("");
}

static void tg_pre_clo_init(void)
{
  VG_(details_name)            ("Tracegrind");
  VG_(details_version)         ("1.0");
  VG_(details_description)     ("valgrind IR to STP declarations converter");
  VG_(details_copyright_author)("Copyright (C) iisaev");
  VG_(details_bug_reports_to)  ("iisaev@ispras.ru");
  VG_(basic_tool_funcs)        (tg_post_clo_init,
                                tg_instrument,
                                tg_fini);
  VG_(needs_syscall_wrapper)(pre_call,
			      post_call);
  VG_(track_post_mem_write)(tg_track_post_mem_write);
  VG_(track_new_mem_mmap)(tg_track_mem_mmap);

  VG_(needs_core_errors) ();
  VG_(needs_var_info) ();

  VG_(needs_command_line_options)(tg_process_cmd_line_option,
                                  tg_print_usage,
                                  tg_print_debug_usage);

  taintedMemory = VG_(HT_construct)("taintedMemory");
  taintedRegisters = VG_(HT_construct)("taintedRegisters");

  tempSizeTable = VG_(HT_construct)("tempSizeTable");
 
  funcNames = VG_(HT_construct)("funcNames");
  
  diFunctionName = VG_(malloc) ("diFunctionName", 1024 * sizeof(Char));
      
  fdtrace = sr_Res(VG_(open)("trace.log", VKI_O_RDWR | VKI_O_TRUNC | VKI_O_CREAT, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO));
  fddanger = sr_Res(VG_(open)("dangertrace.log", VKI_O_RDWR | VKI_O_TRUNC | VKI_O_CREAT, VKI_S_IRWXU | VKI_S_IRWXG | VKI_S_IRWXO));
#if defined(VGP_arm_linux) || defined(VGP_x86_linux)
  my_write(fdtrace, "memory_0 : ARRAY BITVECTOR(32) OF BITVECTOR(8);\nregisters_0 : ARRAY BITVECTOR(8) OF BITVECTOR(8);\n", 98);
  my_write(fddanger, "memory_0 : ARRAY BITVECTOR(32) OF BITVECTOR(8);\nregisters_0 : ARRAY BITVECTOR(8) OF BITVECTOR(8);\n", 98);
#elif defined(VGP_amd64_linux)
  my_write(fdtrace, "memory_0 : ARRAY BITVECTOR(64) OF BITVECTOR(8);\nregisters_0 : ARRAY BITVECTOR(8) OF BITVECTOR(8);\n", 98);
  my_write(fddanger, "memory_0 : ARRAY BITVECTOR(64) OF BITVECTOR(8);\nregisters_0 : ARRAY BITVECTOR(8) OF BITVECTOR(8);\n", 98);
#endif
  curdepth = 0;
}

VG_DETERMINE_INTERFACE_VERSION(tg_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
