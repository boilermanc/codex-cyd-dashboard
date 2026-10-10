from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=['#pragma once','#include <stdint.h>','struct MicroGlyph { uint32_t offset; uint8_t w,h,advance; int8_t x,y; };','struct MicroFont { const uint8_t* pixels; const MicroGlyph* glyphs; };']
for name,size,bold in [('title',22,True),('name',18,True),('status',14,False),('detail',12,False),('tab',14,True)]:
 f=ImageFont.truetype(str(root/'assets/fonts'/('AtkinsonHyperlegible-Bold.ttf' if bold else 'AtkinsonHyperlegible-Regular.ttf')),size)
 glyphs=[];data=[]
 for code in range(32,127):
  char=chr(code);x,y,r,b=f.getbbox(char);w=r-x;h=b-y
  im=Image.new('L',(max(1,w),max(1,h)));ImageDraw.Draw(im).text((-x,-y),char,font=f,fill=255)
  glyphs.append((len(data),w,h,round(f.getlength(char)),x,y));data.extend(im.tobytes() if w*h else [])
 out.append('static const uint8_t micro_pixels_'+name+'[] PROGMEM={'+','.join(map(str,data))+'};')
 out.append('static const MicroGlyph micro_glyphs_'+name+'[] PROGMEM={'+','.join('{'+','.join(map(str,g))+'}' for g in glyphs)+'};')
 out.append('static const MicroFont micro_font_'+name+'={micro_pixels_'+name+',micro_glyphs_'+name+'};')
(root/'include/micro-fonts.h').write_text('\n'.join(out),encoding='utf-8')
print('Generated open-license Atkinson Hyperlegible fonts.')