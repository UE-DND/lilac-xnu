#!/usr/bin/env python3
"""Run production drawRows on guarded 24/32bpp buffers, including scaling and clipping."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lilaclib.paths import XNU
KERNEL=XNU/'iokit/Kernel'
wire=(ROOT/'kernel/lilac-display-api.h').read_text()
limit=(KERNEL.parent/'IOKit/IOUserClient.h').read_text()
assert int(re.search(r'LILAC_DISPLAY_STATUS_COUNT\s+(\d+)',wire)[1]) <= int(re.search(r'kIOExternalMethodScalarOutputCountMax\s*=\s*(\d+)',limit)[1])
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,default=KERNEL/'MiniIOMobileFramebuffer.cpp')
text=parser.parse_args().source.read_text()
helpers=text[text.index('static inline uint32_t\nblend('):text.index('void\nIOMobileFramebuffer::drawRows(')]
draw=text[text.index('void\nIOMobileFramebuffer::drawRows('):text.index('/* A helper of the scaling')]
prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <algorithm>
#include "MiniFramebufferPixels.h"
struct {uint32_t v_depth;} vinfo;
struct RowJob;
class IOMobileFramebuffer {
public:
 uint32_t fPanelHeight,fPanelWidth,fRowBytes;
 uint8_t *fBase;
 uint32_t *fColumn;uint16_t *fColumnWeight;
 bool fDiff;
 uint64_t fDiffPixels=0;
 uint32_t fDiffLeft=~0u,fDiffRight=0,fDiffTop=~0u,fDiffBottom=0;
 void drawRows(const RowJob *,int32_t,int32_t,uint32_t *);
};
'''
tests=r'''
static uint32_t mix(uint32_t a,uint32_t b,unsigned w) {
 uint32_t out=0;for(unsigned c=0;c<4;c++)out|=((((a>>(c*8))&255)*(256-w)+((b>>(c*8))&255)*w)/256)<<(c*8);
 return out;
}
static uint32_t pixel(unsigned x,unsigned y) {
 return 0x80000000u | (x*23+y*7) | (x*13+y*29)<<8 | (x*19+y*31)<<16;
}
static int64_t position(int i,int in,int out) {
 int64_t n=((int64_t)(2*i+1)*in-out)*128/out;
 return std::max<int64_t>(0,std::min<int64_t>((in-1)*256,n));
}
static void check(unsigned depth,bool filter,bool unscaled,bool clip,unsigned padding) {
 const int sw=4,sh=3,sx=1,sy=1,sourceWidth=6;
 const int width=unscaled?4:7,height=unscaled?3:5;
 const int px0=clip?-2:0,py0=clip?-1:0,px1=width,py1=height;
 const unsigned stride=width*(depth/8)+padding,guard=64;
 std::vector<uint8_t> storage(guard+stride*height+guard,0xa5);
 std::vector<uint32_t> source(sourceWidth*5),column(width),blendRow(sourceWidth);
 std::vector<uint16_t> weights(width);
 for(unsigned y=0;y<5;y++)for(unsigned x=0;x<sourceWidth;x++)source[y*sourceWidth+x]=pixel(x,y);
 for(int x=0;x<width;x++) {
  int64_t p=position(x-px0,sw,px1-px0);
  column[x]=filter?(uint32_t)(p/256):sx+(x-px0)*sw/(px1-px0);
  weights[x]=column[x]+1<sw?p%256:0;
 }
 IOMobileFramebuffer fb;fb.fPanelWidth=width;fb.fPanelHeight=height;fb.fRowBytes=stride;
 fb.fBase=storage.data()+guard;fb.fColumn=column.data();fb.fColumnWeight=weights.data();fb.fDiff=true;
 vinfo.v_depth=depth;
 RowJob j={(uint8_t *)source.data(),sourceWidth*4,sx,sy,sw,sh,px0,py0,px1,py1,0,width,0,height,filter,unscaled};
 fb.drawRows(&j,0,height,blendRow.data());
 for(int y=0;y<height;y++)for(int x=0;x<width;x++) {
  uint32_t expected;
  if(unscaled)expected=source[(sy+y)*sourceWidth+sx+x];
  else if(!filter)expected=source[(sy+(y-py0)*sh/(py1-py0))*sourceWidth+column[x]];
  else {
   int64_t p=position(y-py0,sh,py1-py0);int r=p/256;unsigned wy=p%256;
   uint32_t c=column[x],a=source[(sy+r)*sourceWidth+sx+c];
   uint32_t b=source[(sy+r)*sourceWidth+sx+c+(weights[x]!=0)];
   if(wy && r+1<sh) {
    a=mix(a,source[(sy+r+1)*sourceWidth+sx+c],wy);
    b=mix(b,source[(sy+r+1)*sourceWidth+sx+c+(weights[x]!=0)],wy);
   }
   expected=mix(a,b,weights[x]);
  }
  assert(miniPanelRead(fb.fBase+y*stride,x,depth)==(expected&(depth==24?0xffffffu:~0u)));
 }
 for(unsigned i=0;i<guard;i++)assert(storage[i]==0xa5 && storage[storage.size()-1-i]==0xa5);
 for(int y=0;y<height;y++)for(unsigned i=width*(depth/8);i<stride;i++)assert(fb.fBase[y*stride+i]==0xa5);
 if(!unscaled) {
  assert(fb.fDiffPixels>0);fb.fDiffPixels=0;fb.drawRows(&j,0,height,blendRow.data());assert(fb.fDiffPixels==0);
  if(depth==24) {for(auto &value:source)value^=0xff000000u;fb.drawRows(&j,0,height,blendRow.data());assert(fb.fDiffPixels==0);}
 }
 for(int y=0;y<height;y++)miniPanelClear(fb.fBase+y*stride,width,depth);
 for(int y=0;y<height;y++)for(unsigned i=0;i<stride;i++)assert(fb.fBase[y*stride+i]==(i<width*(depth/8)?0:0xa5));
 for(unsigned i=0;i<guard;i++)assert(storage[i]==0xa5 && storage[storage.size()-1-i]==0xa5);
}
int main() {
 for(unsigned depth:{24,32}) for(unsigned padding:{0,11}) {
  check(depth,false,true,false,padding);check(depth,false,false,false,padding);check(depth,true,false,false,padding);
  check(depth,false,false,true,padding);check(depth,true,false,true,padding);
 }
 puts("PASS: production framebuffer rows; 24/32bpp, scaling/filtering/clipping, RGB, alpha-only diff, odd stride, clear and guard bytes");
}
'''
with tempfile.TemporaryDirectory(prefix='lilac-framebuffer-') as directory:
    path=Path(directory);(path/'test.cpp').write_text(prefix+helpers+draw+tests)
    subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer','-I'+str(KERNEL),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
