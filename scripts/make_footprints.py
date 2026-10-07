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
pads=[pad_th(1,2.5,0,2.5,1.6,'rect'),pad_th(2,-2.5,0,2.5,1.6)]   # V3.4：KF2EDGR-5.0 規格書建議孔 Ø1.6（原 1.5）
fp('TerminalBlock_Pluggable_1x02_P5.00mm_Horizontal','Pluggable terminal header 2P 5.00mm right-angle (KEFA KF2EDGR-5.0-2P, JLC C441193), holes 1.6mm',(-5.1,-8.1,5.1,2.2),(-5.4,-8.4,5.4,2.5),'through_hole',pads)
# 插拔式端子座 8P 5.08mm（開口朝 +Y = 板邊）；Pin1 在 +X（原板 AC_L）
pads=[pad_th(1,17.78,0,2.3,1.6,'rect')]+[pad_th(n,17.78-5.08*(n-1),0,2.3,1.6) for n in range(2,9)]   # V3.4：KF2EDGR-5.08 建議孔 Ø1.6
fp('TerminalBlock_Pluggable_1x08_P5.08mm_Horizontal','Pluggable terminal header 8P 5.08mm right-angle (KEFA KF2EDGR-5.08-8P, JLC C441210), holes 1.6mm',(-20.4,-2.4,20.4,7.9),(-20.7,-2.7,20.7,8.2),'through_hole',pads)
# 插拔式端子座 7P 3.50mm（V3.2 DI 輸入，開口朝 -Y = 板邊）；Pin1（+12V）在 -X。
# 外形取 3.5mm 直角插拔座常見尺寸（如 KF2EDGR-3.5-07P／15EDGRC-3.5-07P：孔 1.2、深約 7mm），下單前要對實際料號核對
pads=[pad_th(1,-10.5,0,2.0,1.2,'rect')]+[pad_th(n,-10.5+3.5*(n-1),0,2.0,1.2) for n in range(2,8)]
fp('TerminalBlock_Pluggable_1x07_P3.50mm_Horizontal','Pluggable terminal header 7P 3.50mm right-angle (e.g. KF2EDGR-3.5-07P), holes 1.2mm; body outline UNVERIFIED',(-12.5,-7.2,12.5,2.0),(-12.8,-7.5,12.8,2.3),'through_hole',pads)
# NFC 印刷線圈（V3.3，ST25DV04KC 用）：正面矩形螺旋，最內圈端點經 THT 焊盤（當過孔）走背面拉到線圈外。
# net tie（"1, 2"）：線圈銅箔合法連接兩個焊盤（電感在直流是短路）。電感用 scripts/nfc_coil.py 計算。
# 背面跨線不畫在封裝裡：封裝圖形不算連接（內外兩個「2」會被 DRC 判成未連線），由 PCB 腳本畫成真正的走線；
# 所以禁布區允許走線（禁鋪銅、過孔），別的網路由佈線器擋在外面。
def nfc_coil(W, H, n, w, s, name):
    pth = w + s; segs = []
    def ln(a, b, layer): return f'\t(fp_line (start {a[0]:.3f} {a[1]:.3f}) (end {b[0]:.3f} {b[1]:.3f}) (stroke (width {w}) (type solid)) (layer "{layer}") (uuid "{U()}"))\n'
    pts = []
    for k in range(n):
        a = k * pth
        xl, xr, yt, yb = -W/2 + w/2 + a, W/2 - w/2 - a, -H/2 + w/2 + a, H/2 - w/2 - a
        if k == 0: pts.append((xl, yb))
        pts += [(xl, yt), (xr, yt), (xr, yb), (xl + pth, yb)]
    pad_y = H/2 + 1.0
    body = ln((pts[0][0], pad_y), pts[0], "F.Cu")
    body += ''.join(ln(pts[i], pts[i + 1], "F.Cu") for i in range(len(pts) - 1))
    # 最內圈終點往中央空白斜拉 0.7mm 再放焊盤：直接放在 (xl+p, yb)，0.6mm 焊盤會碰到左邊與下面兩圈（中心距只有 0.4）
    end = (pts[-1][0] + 0.7, pts[-1][1] - 0.7)
    body += ln(pts[-1], end, "F.Cu")
    body += f'\t(fp_line (start {end[0]:.3f} {end[1]:.3f}) (end {end[0]:.3f} {pad_y:.3f}) (stroke (width {w}) (type dash)) (layer "B.Fab") (uuid "{U()}"))\n'   # 跨線位置（B.Fab 示意）
    pads = [pad_smd(1, round(pts[0][0], 3), pad_y, 0.6, 0.6),
            f'\t(pad "2" thru_hole circle (at {end[0]:.3f} {end[1]:.3f}) (size 0.6 0.6) (drill 0.3) (layers "*.Cu") (remove_unused_layers no) (uuid "{U()}"))\n',
            f'\t(pad "2" thru_hole circle (at {end[0]:.3f} {pad_y:.3f}) (size 0.6 0.6) (drill 0.3) (layers "*.Cu") (remove_unused_layers no) (uuid "{U()}"))\n']
    m = 0.5                                                        # 禁布區外擴：別的銅、鋪銅、過孔都不准進線圈
    keep = (f'\t(zone (net 0) (net_name "") (layers "F.Cu" "B.Cu") (uuid "{U()}") (name "NFC_KEEPOUT") (hatch edge 0.5) '
            f'(connect_pads (clearance 0)) (min_thickness 0.25) (filled_areas_thickness no) (keepout (tracks allowed) '
            f'(vias not_allowed) (pads allowed) (copperpour not_allowed) (footprints allowed)) (fill (thermal_gap 0.5) '
            f'(thermal_bridge_width 0.5)) (polygon (pts (xy {-W/2-m:.2f} {-H/2-m:.2f}) (xy {W/2+m:.2f} {-H/2-m:.2f}) '
            f'(xy {W/2+m:.2f} {H/2+m:.2f}) (xy {-W/2-m:.2f} {H/2+m:.2f}))))\n')
    silk = f'\t(fp_text user "NFC" (at 0 0) (layer "F.SilkS") (uuid "{U()}") (effects (font (size 1.5 1.5) (thickness 0.2))))\n'
    tie = '\t(net_tie_pad_groups "1, 2")\n'
    fp(name, f'NFC printed coil {W}x{H}mm {n} turns {w}/{s}mm (13.56MHz, ST25DV04KC Ctun 28.5pF + external tuning cap); net tie 1-2',
       (-W/2, -H/2, W/2, H/2), (-W/2 - m - 0.1, -H/2 - m - 0.1, W/2 + m + 0.1, pad_y + 0.6), 'smd', pads, tie + body + keep + silk)
    return pts, end, pad_y


nfc_coil(14.9, 14.6, 8, 0.2, 0.2, 'NFC_Coil_14.9x14.6mm_8T')
# V3.4（照外殼 4-02-3 改回原板框）：
# 插拔式端子座 7P 2.54mm（KEFA KF2EDGR-2.54-7P，C577599）。規格書：本體寬 P×2.54+1.8＝19.58、深 8.1、孔 Φ1.4、
# 孔排離本體後緣 1.2（開口在另一側，離孔排 6.9）。原點＝Pin4，開口朝 -Y（板邊），Pin1 在 -X
pads=[pad_th(1,-7.62,0,2.0,1.4,'rect')]+[pad_th(n,-7.62+2.54*(n-1),0,2.0,1.4) for n in range(2,8)]
fp('TerminalBlock_Pluggable_1x07_P2.54mm_Horizontal','Pluggable terminal header 7P 2.54mm right-angle (KEFA KF2EDGR-2.54-7P, JLC C577599), holes 1.4mm',(-9.79,-6.9,9.79,1.2),(-10.04,-7.15,10.04,1.45),'through_hole',pads)
# 燒錄測試點 2×3、2.54mm（取代 P2 排針，放背面用探針治具燒錄；腳號同 Conn_02x03_Odd_Even）
def pad_tp(n,x,y): return f'\t(pad "{n}" smd circle (at {x} {y}) (size 1.5 1.5) (layers "F.Cu" "F.Mask") (uuid "{U()}"))\n'
pads=[pad_tp(n, -1.27 if n%2 else 1.27, -2.54+2.54*((n-1)//2)) for n in range(1,7)]
fp('ProgPads_2x03_P2.54mm','Programming test pads 2x3 P2.54mm (pogo-pin jig; pin numbering as Conn_02x03_Odd_Even)',(-2.1,-3.35,2.1,3.35),(-2.3,-3.55,2.3,3.55),'smd',pads)
# 輕觸開關 3.9×3.0（HYP TS-1088-AR02016，JLC 基礎料 C720477）：焊盤取自 JLC／EasyEDA 封裝 SW-SMD_L3.9-W3.0-P4.45
pads=[pad_smd(1,-2.18,0,1.23,1.86),pad_smd(2,2.18,0,1.23,1.86)]
fp('SW_SPST_TS-1088','SMD tactile switch 3.9x3.0mm (HYP TS-1088-AR02016, JLC C720477)',(-1.95,-1.5,1.95,1.5),(-3.05,-1.8,3.05,1.8),'smd',pads)
# 6x6 輕觸開關 SMD（ZX-QC66-4.3TP）：1,1 / 2,2
pads=[pad_smd(1,-4.25,-2.25,2.1,1.4),pad_smd(1,4.25,-2.25,2.1,1.4),pad_smd(2,-4.25,2.25,2.1,1.4),pad_smd(2,4.25,2.25,2.1,1.4)]
fp('SW_Push_6x6mm_SMD_ZX-QC66','6x6mm SMD tactile switch (Megastar ZX-QC66-4.3TP, JLC C7470150)',(-3,-3,3,3),(-5.4,-3.0,5.4,3.0),'smd',pads)
# Espressif ESP32-C6-WROOM-1（V3.0）：18×25.5mm，天線在 -Y 端 6mm；原點=本體中心。
# 規格書 v1.4 Figure 10-1：兩側各 14 腳、1.27mm 間距、最後一腳離底邊 1.5mm；EPAD 3.3mm 方形，中心離 pin1 側邊 7.495、離底邊 12.29。
# 焊盤 1.5×0.9，中心 x=±8.75（伸出本體 0.25mm，方便目檢與手修）。天線下方兩層禁銅（含板邊外延 1mm）。
pads=[pad_smd(n,-8.75,round(-5.26+(n-1)*1.27,3),1.5,0.9) for n in range(1,15)]
pads+=[pad_smd(n,8.75,round(11.25-(n-15)*1.27,3),1.5,0.9) for n in range(15,29)]
pads+=[f'\t(pad "29" smd rect (at -1.505 0.46) (size 3.3 3.3) (layers "F.Cu" "F.Paste" "F.Mask") (solder_paste_margin -0.4) (uuid "{U()}"))\n']
keep=f'\t(zone (net 0) (net_name "") (layers "F.Cu" "B.Cu") (uuid "{U()}") (name "ANT_KEEPOUT") (hatch edge 0.5) (connect_pads (clearance 0)) (min_thickness 0.25) (filled_areas_thickness no) (keepout (tracks not_allowed) (vias not_allowed) (pads not_allowed) (copperpour not_allowed) (footprints allowed)) (fill (thermal_gap 0.5) (thermal_bridge_width 0.5)) (polygon (pts (xy -10 -13.75) (xy 10 -13.75) (xy 10 -6.75) (xy -10 -6.75))))\n'
ant='\t(fp_line (start -9 -6.75) (end 9 -6.75) (stroke (width 0.1) (type dash)) (layer "F.Fab") (uuid "%s"))\n' % U()
fp('Espressif_ESP32-C6-WROOM-1','Espressif ESP32-C6-WROOM-1 module 18x25.5mm (datasheet v1.4 Fig.10-1); PCB antenna at -Y, place at board edge; copper keep-out under antenna',
   (-9,-12.75,9,12.75),(-9.75,-13.0,9.75,13.0),'smd',pads,keep+ant)
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
