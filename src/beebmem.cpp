/****************************************************************************/
/*              Beebem - (c) David Alan Gilbert 1994                        */
/*              ------------------------------------                        */
/* This program may be distributed freely within the following restrictions:*/
/*                                                                          */
/* 1) You may not charge for this program or for any part of it.            */
/* 2) This copyright message must be distributed with all copies.           */
/* 3) This program must be distributed complete with source code.  Binary   */
/*    only distribution is not permitted.                                   */
/* 4) The author offers no warrenties, or guarentees etc. - you use it at   */
/*    your own risk.  If it messes something up or destroys your computer   */
/*    thats YOUR problem.                                                   */
/* 5) You may use small sections of code from this program in your own      */
/*    applications - but you must acknowledge its use.  If you plan to use  */
/*    large sections then please ask the author.                            */
/*                                                                          */
/* If you do not agree with any of the above then please do not use this    */
/* program.                                                                 */
/* Please report any problems to the author at gilbertd@cs.man.ac.uk        */
/****************************************************************************/
/* Beebemulator - memory subsystem - David Alan Gilbert 16/10/94 */
#include <windows.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "iostream.h"

#include "6502core.h"
#include "disc8271.h"
#include "main.h"
#include "sysvia.h"
#include "uservia.h"
#include "video.h"

static int PagedRomReg;

static int RomModified=0; /* Rom changed - needs copying back */
unsigned char WholeRam[65536];
static unsigned char Roms[16][16384];

/*----------------------------------------------------------------------------*/
/* Perform hardware address wrap around */
static unsigned int WrapAddr(int in) {
  unsigned int offsets[]={0x4000,0x6000,0x3000,0x5800};
  if (in<0x8000) return(in);
  in+=offsets[(IC32State & 0x30)>>4];
  in&=0x7fff;
  return(in);
}; /* WrapAddr */

/*----------------------------------------------------------------------------*/
/* This is for the use of the video routines.  It returns a pointer to
   a continuous area of 'n' bytes containing the contents of the
   'n' bytes of beeb memory starting at address 'a', with wrap around
   at 0x8000.  Potentially this routine may return a pointer into  a static
   buffer - so use the contents before recalling it
   'n' must be less than 1K in length.
   See 'BeebMemPtrWithWrapMo7' for use in Mode 7 - its a special case.
*/
char *BeebMemPtrWithWrap(int a, int n) {
  static char tmpBuf[1024];
  char *tmpBufPtr;
  int EndAddr=a+n-1;
  int toCopy;

  a=WrapAddr(a);
  EndAddr=WrapAddr(EndAddr);

  if (a<=EndAddr) {
    return((char *)WholeRam+a);
  };

  toCopy=0x8000-a;
  if (toCopy>n) toCopy=n;
  if (toCopy>0) memcpy(tmpBuf,WholeRam+a,toCopy);
  tmpBufPtr=tmpBuf+toCopy;
  toCopy=n-toCopy;
  if (toCopy>0) memcpy(tmpBufPtr,WholeRam+EndAddr-(toCopy-1),toCopy); /* Should that -1 be there ? */

  return(tmpBuf);
}; /* BeebMemPtrWithWrap */

/*----------------------------------------------------------------------------*/
/* Perform hardware address wrap around - for mode 7*/
static unsigned int WrapAddrMo7(int in) {
  if (in<0x8000) return(in);
  in+=0x7c00;
  in&=0x7fff;
  return(in);
}; /* WrapAddrMo7 */

/*----------------------------------------------------------------------------*/
/* Special case of BeebMemPtrWithWrap for use in mode 7
*/
char *BeebMemPtrWithWrapMo7(int a, int n) {
  static char tmpBuf[1024];
  char *tmpBufPtr;
  int EndAddr=a+n-1;
  int toCopy;

  a=WrapAddrMo7(a);
  EndAddr=WrapAddrMo7(EndAddr);

  if (a<=EndAddr) {
    return((char *)WholeRam+a);
  };

  toCopy=0x8000-a;
  if (toCopy>n) return((char *)WholeRam+a);
  if (toCopy>0) memcpy(tmpBuf,WholeRam+a,toCopy);
  tmpBufPtr=tmpBuf+toCopy;
  toCopy=n-toCopy;
  if (toCopy>0) memcpy(tmpBufPtr,WholeRam+EndAddr-(toCopy-1),toCopy); /* Should that -1 be there ? */

  return(tmpBuf);
}; /* BeebMemPtrWithWrapMo7 */

/*----------------------------------------------------------------------------*/
int BeebReadMem(int Address) {
  static int extracycleprompt=0;

 /* We now presume that the caller has checked to see if the address is below fc00
    and if so does a direct read */
 /* if (Address<0xfc00) return(WholeRam[Address]); */
  if (Address>=0xff00) return(WholeRam[Address]);

  Cycles++;
  extracycleprompt++;
  if (extracycleprompt & 8) Cycles++;
  /* OK - its IO space - lets check some system devices */
  /* VIA's first - games seem to do really heavy reaing of these */
  if ((Address & ~0xf)==0xfe40) return(SysVIARead(Address & 0xf));
  if ((Address & ~0xf)==0xfe60) return(UserVIARead(Address & 0xf));
  if ((Address & ~7)==0xfe00) return(CRTCRead(Address & 0x7));
  if ((Address & ~0xf)==0xfe20) return(VideoULARead(Address & 0xf));
  if ((Address & ~0x1f)==0xfe80) return(Disc8271_read(Address & 0x7));
  if ((Address & ~0x1f)==0xfea0) return(0xfe); /* Disable econet */
  if ((Address & ~0x1f)==0xfee0) return(0xfe); /* Disable tube */
  return(0);
} /* BeebReadMem */

/*----------------------------------------------------------------------------*/
static void DoRomChange(int NewBank) {
  /* Speed up hack - if we are switching to the same rom, then don't bother */
  if (NewBank==PagedRomReg) return;

  if (RomModified) {
    memcpy(Roms[PagedRomReg],WholeRam+0x8000,0x4000);
    RomModified=0;
  };

  PagedRomReg=NewBank;
  memcpy(WholeRam+0x8000,Roms[PagedRomReg],0x4000);
}; /* DoRomChange */

/*----------------------------------------------------------------------------*/
void BeebWriteMem(int Address, int Value) {
  static int extracycleprompt=0;
/*  fprintf(stderr,"Write %x to 0x%x\n",Value,Address); */

  /* Now we presume that the caller has validated the address as beingwithin
  main ram and hence the following line is not required */
  /*if (Address<0x8000) {
    WholeRam[Address]=Value;
    return;
  } */

  if (Address<0xc000) {
    WholeRam[Address]=Value;
    RomModified=1;
    return;
  }

  Cycles++;
  extracycleprompt++;
  if (extracycleprompt & 8) Cycles++;

  if ((Address>=0xfc00) && (Address<=0xfeff)) {
    /* Check for some hardware */
    if ((Address & ~0xf)==0xfe20) {
      VideoULAWrite(Address & 0xf, Value);
      return;
    }
    if ((Address & ~0xf)==0xfe40) {
      SysVIAWrite((Address & 0xf),Value);
      return;
    }

    if ((Address & ~0xf)==0xfe60) {
      UserVIAWrite((Address & 0xf),Value);
      return;
    }

    if (Address==0xfe30) {
      DoRomChange(Value & 0xf);
      return;
    }

    /*cerr << "Write *0x" << hex << Address << "=0x" << Value << dec << "\n"; */
    if ((Address & ~0x7)==0xfe00) {
      CRTCWrite(Address & 0x7, Value);
      return;
    }

    if ((Address & ~0x1f)==0xfe80) {
      Disc8271_write((Address & 7),Value);
      return;
    }

    if (Address==0xfc01) exit(0);
    return;
  }
}

/*----------------------------------------------------------------------------*/
static void ReadRom(char *name,int bank) {
  //FILE *InFile;
  HANDLE    InFile;
  DWORD     read;

  char fullname[256];

  sprintf(fullname,".\\beebfile\\%s",name);

  InFile = CreateFile(fullname,
                GENERIC_READ,  FILE_SHARE_READ, NULL,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                NULL);

  if(InFile == INVALID_HANDLE_VALUE)
  {
    MessageBox(NULL,fullname,"Not Found",MB_OK);
    abort();
  }

  if(!ReadFile(InFile, Roms[bank], 16384, &read, NULL))
  {
    MessageBox(NULL,fullname,"Could not read",MB_OK);
    abort();
  }

  if(read != 16384)
  {
    MessageBox(NULL,fullname,"Wrong Size",MB_OK);
    abort();
  }

  CloseHandle(InFile);

  /*if (InFile=fopen(fullname,"r"),InFile==NULL) {
    fprintf(stderr,"Could not open %s rom file\n",name);
    abort();
  }

  if (fread(Roms[bank],1,16384,InFile)!=16384) {
    fprintf(stderr,"Could not read %s\n",name);
    ///abort();   $NRM don't know why this fails...
  }

  fclose(InFile);
  */

} /* ReadRom */

/*----------------------------------------------------------------------------*/
void BeebMemInit(void) {
  //FILE *InFile;

  HANDLE InFile;
  DWORD read;

  ReadRom("basic",0xf);
  /* ReadRom("exmon",0xe);
  ReadRom("memdump_bottom",0x4);
  ReadRom("memdump_top",0x5);  */
  ReadRom("dnfs",0);

  /* Load O/S */
  InFile = CreateFile(".\\beebfile\\os12",
                GENERIC_READ,  FILE_SHARE_READ, NULL,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                NULL);

  if(InFile == INVALID_HANDLE_VALUE)
  {
    MessageBox(NULL,"Could not open Operating System","Beebem",MB_OK);
    abort();
  }

  if(!ReadFile(InFile, WholeRam+0xc000, 16384, &read, NULL))
  {
    MessageBox(NULL,"An error occurred reading the Operating System","Beebem",MB_OK);
    abort();
  }

  if(read != 16384)
  {
    MessageBox(NULL,"Operating System Wrong Size!","Beebem",MB_OK);
    abort();
  }

  CloseHandle(InFile);
  /* Load OS */
  //if (InFile=fopen("c:\\beebfile\\os12.rom","r"),InFile==NULL) {
  //  fprintf(stderr,"Could not open OS rom file\n");
    // abort(); $NRM dunno why this fails
  //}

  //if (fread(WholeRam+0xc000,1,16384,InFile)!=16384) {
  //  fprintf(stderr,"Could not read OS\n");
    // abort();
  //}
  //fclose(InFile);

  /* Put first ROM in */
  memcpy(WholeRam+0x8000,Roms[0xf],0x4000);
  PagedRomReg=0xf;
  RomModified=0;
} /* BeebMemInit */

/* dump the contents of mainram into 2 16 K files */
void beebmem_dumpstate(void) {
  FILE *bottom,*top;

  bottom=fopen("memdump_bottom","wb");
  top=fopen("memdump_top","wb");
  if ((bottom==NULL) || (top==NULL)) {
    cerr << "Couldn't open memory dump files\n";
    return;
  };

  fwrite(WholeRam,1,16384,bottom);
  fwrite(WholeRam+16384,1,16384,top);
  fclose(bottom);
  fclose(top);
}; /* beebmem_dumpstate */