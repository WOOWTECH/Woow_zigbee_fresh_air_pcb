"""產生 WOOW.pretty 自訂封裝。焊盤座標取自原設計 V1.3 Gerber（量測值，單位 mm），
方向與原板一致（擺件旋轉 0°）。KiCad Y 軸向下，所以原 Gerber 的 +y 這裡寫成 -y。"""
import os, re, uuid
OUT=os.path.join(os.path.dirname(__file__),'..','hardware','lib','WOOW.pretty')
def U(): return str(uuid.uuid4())
def rect(layer,x0,y0,x1,y1,w=0.12):
    return f'\t(fp_rect (start {x0} {y0}) (end {x1} {y1}) (stroke (width {w}) (type solid)) (fill no) (layer "{layer}") (uuid "{U()}"))\n'
def text(kind,val,y,layer):
    return f'\t(property "{kind}" "{val}" (at 0 {y} 0) (layer "{layer}") (uuid "{U()}") (effects (font (size 1 1) (thickness 0.15))))\n'
def pad_smd(n,x,y,w,h,shape='rect'):
    return f'\t(pad "{n}" smd {shape} (at {x} {y}) (size {w} {h}) (layers "F.Cu" "F.Paste" "F.Mask") (uuid "{U()}"))\n'
def pad_th(n,x,y,size,drill,shape='circle'):
    return f'\t(pad "{n}" thru_hole {shape} (at {x} {y}) (size {size} {size}) (drill {drill}) (layers "*.Cu" "*.Mask") (uuid "{U()}"))\n'
def npth(x,y,d):
    return f'\t(pad "" np_thru_hole circle (at {x} {y}) (size {d} {d}) (drill {d}) (layers "*.Cu" "*.Mask") (uuid "{U()}"))\n'
def fp(name,desc,body,crt,attr,pads,extra=''):
    x0,y0,x1,y1=body; c0,c1,c2,c3=crt
    s=f'(footprint "{name}"\n\t(version 20241229)\n\t(generator "woowtech_gen")\n\t(layer "F.Cu")\n\t(descr "{desc}")\n'
    s+=text('Reference','REF**',y0-1.2,'F.SilkS')+text('Value',name,y1+1.2,'F.Fab')
    s+=f'\t(attr {attr})\n'+rect('F.Fab',x0,y0,x1,y1,0.1)+rect('F.SilkS',x0-0.1,y0-0.1,x1+0.1,y1+0.1)+rect('F.CrtYd',c0,c1,c2,c3,0.05)+extra+''.join(pads)+')\n'
    open(os.path.join(OUT,name+'.kicad_mod'),'w').write(s)
os.makedirs(OUT,exist_ok=True)
# Tuya ZS3L：16 焊盤 2.0mm 間距，兩排 y=±7.425；天線端在 -x（要貼板邊、下方淨空）
pads=[pad_smd(i+1,-7+2*i,7.425,1.0,2.1) for i in range(8)]+[pad_smd(9+i,7-2*i,-7.425,1.0,2.1) for i in range(8)]
keep=f'\t(zone (net 0) (net_name "") (layers "F.Cu" "B.Cu") (uuid "{U()}") (hatch edge 0.5) (connect_pads (clearance 0)) (min_thickness 0.25) (filled_areas_thickness no) (keepout (tracks not_allowed) (vias not_allowed) (pads not_allowed) (copperpour not_allowed) (footprints allowed)) (fill (thermal_gap 0.5) (thermal_bridge_width 0.5)) (polygon (pts (xy -15.43 -8) (xy -8 -8) (xy -8 8) (xy -15.43 8))))\n'
fp('Tuya_ZS3L','Tuya ZS3L Zigbee module 24x16mm, pads measured from WO_30109 V1.3; antenna at -X, copper keep-out under antenna',
   (-15.43,-8,8.52,8),(-15.7,-8.8,8.8,8.8),'smd',pads,keep)
# Ebelong EPA09-4D：6 腳 2.0mm、孔 0.9、Pin1 方形在 +5；+13.5 有 Ø3.2 固定孔（外框未經規格書證實）
pads=[pad_th(1,5,0,1.5,0.9,'rect')]+[pad_th(n,5-2*(n-1),0,1.5,0.9) for n in range(2,7)]  # 原板 +13.5mm 的 Ø3.2 孔其實是板子固定孔，另以 H2 放置
fp('Ebelong_EPA09-4D','Ebelong EPA09-4D 433MHz receiver, 6 pins P2.0mm; module end shares the board M3 hole at +13.5mm (H3); body outline UNVERIFIED',(-7,-10,9.8,2),(-7.3,-10.3,10.0,2.3),'through_hole',pads)
# 插拔式端子座 2P 5.00mm（開口朝 -Y = 板邊）
pads=[pad_th(1,2.5,0,2.5,1.5,'rect'),pad_th(2,-2.5,0,2.5,1.5)]
fp('TerminalBlock_Pluggable_1x02_P5.00mm_Horizontal','Pluggable terminal header 2P 5.00mm right-angle (e.g. 2EDGRC-5.0-02P), holes 1.5mm',(-5.1,-8.1,5.1,2.2),(-5.4,-8.4,5.4,2.5),'through_hole',pads)
# 插拔式端子座 8P 5.08mm（開口朝 +Y = 板邊）；Pin1 在 +X（原板 AC_L）
pads=[pad_th(1,17.78,0,2.3,1.5,'rect')]+[pad_th(n,17.78-5.08*(n-1),0,2.3,1.5) for n in range(2,9)]
fp('TerminalBlock_Pluggable_1x08_P5.08mm_Horizontal','Pluggable terminal header 8P 5.08mm right-angle (e.g. 15EDGRC-5.08-08P), holes 1.5mm',(-20.4,-2.4,20.4,7.9),(-20.7,-2.7,20.7,8.2),'through_hole',pads)
# 6x6 輕觸開關 SMD（ZX-QC66-4.3TP）：1,1 / 2,2
pads=[pad_smd(1,-4.25,-2.25,2.1,1.4),pad_smd(1,4.25,-2.25,2.1,1.4),pad_smd(2,-4.25,2.25,2.1,1.4),pad_smd(2,4.25,2.25,2.1,1.4)]
fp('SW_Push_6x6mm_SMD_ZX-QC66','6x6mm SMD tactile switch (Megastar ZX-QC66-4.3TP, JLC C7470150)',(-3,-3,3,3),(-5.4,-3.0,5.4,3.0),'smd',pads)
# Omron G5Q-1：沿用 KiCad 官方封裝，只把 courtyard 收到本體+0.25mm（原板繼電器間距 10.67mm、本體 10.0mm）
src=open('/usr/share/kicad/footprints/Relay_THT.pretty/Relay_SPDT_Omron-G5Q-1.kicad_mod').read()
CRT={'-1.95':'-1.43','19.7':'19.21','-9.55':'-9.06','1.95':'1.44'}
def _tight_blk(blk):
    if '(layer "F.CrtYd")' not in blk: return blk
    def fix(m): return m.group(1)+' '.join(CRT.get(v,v) for v in m.group(2).split())+')'
    return re.sub(r'(\((?:start|end) )([-\d. ]+)\)', fix, blk)
parts=src.split('\n\t(fp_line')
src=parts[0]+''.join('\n\t(fp_line'+_tight_blk(x) for x in parts[1:])
src=src.replace('(footprint "Relay_SPDT_Omron-G5Q-1"','(footprint "Relay_SPDT_Omron-G5Q-1_Tight"')
open(os.path.join(OUT,'Relay_SPDT_Omron-G5Q-1_Tight.kicad_mod'),'w').write(src)
print('footprints:',sorted(os.listdir(OUT)))
