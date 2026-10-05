"""SYN480R 433.92MHz 匹配選值：在 E 系列可買到的值裡，找「晶片阻抗範圍 × 元件公差」最差情況失配損耗最小的一組。

  python3 scripts/rf_match_opt.py

拓撲固定為 PCB 上的：天線 → L3∥C16 並聯 → C15 串聯 → L4 並聯∥晶片 ANT 腳。
晶片阻抗用同系列 Micrel 原廠實測值（見 sim/rf_match.cir 開頭）；電感 Q 取 40；公差：電感 ±5%、電容 ±0.1pF。
結果用 sim/rf_match.cir（ngspice）交叉驗證。
"""
import cmath, math, itertools
F=433.92e6
ZCHIP={'MICRF010@430':13.15-107j,'MICRF010@435':12.94-106j,'MICRF211':18.6-174.2j,'MICRF220':12-209j}
def zin_load(f,z433):
    # 以 433.92 的串聯 R + C 外插到其他頻率（電容性輸入）
    C=-1/(2*math.pi*F*z433.imag); return z433.real + 1/(1j*2*math.pi*f*C)
def ml_db(f,L3,C16,C15,L4,z433,Q=40):
    w=2*math.pi*f
    zL=lambda L: w*L/Q + 1j*w*L
    zC=lambda C: 1/(1j*w*C)
    par=lambda *zs: 1/sum(1/z for z in zs)
    Zl=zin_load(f,z433)
    z=par(zL(L4),Zl)               # 晶片端並聯 L4
    z=z+zC(C15)                    # 串聯 C15
    z=par(zL(L3),zC(C16),z)        # 天線端並聯 L3∥C16
    g=(z-50)/(z+50)
    # 傳到晶片電阻的功率 / 可用功率：用電壓分壓精算
    Vs=1; I=Vs/(50+z); Va=Vs-50*I                      # 天線節點電壓
    Zr=par(zL(L4),Zl); Vchip=Va*Zr/(Zr+zC(C15))
    Ichip=Vchip/Zl; P=0.5*abs(Ichip)**2*Zl.real; Pav=Vs**2/(8*50)
    return -10*math.log10(P/Pav)
def evaluate(v):
    L3,C16,C15,L4=v
    worst=0
    for z in ZCHIP.values():
        for dl3,dl4,dc in itertools.product((0.95,1,1.05),(0.95,1,1.05),(-0.1e-12,0,0.1e-12)):
            worst=max(worst, ml_db(F,L3*dl3,C16+dc,C15+dc,L4*dl4,z))
    return worst
def peak(v,z):
    fs=[200e6+i*0.5e6 for i in range(1001)]
    return min(fs,key=lambda f: ml_db(f,*v,z))
nH=1e-9; pF=1e-12
cands={'SYN480R 手冊值（現況）':(27*nH,1.8*pF,6.8*pF,47*nH),
       'MICRF211 原廠值':(24*nH,5.6*pF,1.5*pF,39*nH)}
for name,v in cands.items():
    print(f"{name:22s} worst ML={evaluate(v):5.1f}dB  "+'  '.join(f"{k}: ML433={ml_db(F,*v,z):4.1f}dB peak={peak(v,z)/1e6:5.0f}MHz" for k,z in ZCHIP.items()))
# 在可買到的 E 系列值裡搜尋
Ls=[x*nH for x in (22,24,27,33,36,39,43,47)]; Cs=[x*pF for x in (1.0,1.2,1.3,1.5,1.8,2.0,2.2,2.7,3.3,3.9,4.7,5.6,6.8)]
res=[]
for L3,C16 in itertools.product(Ls,Cs):
    if abs(1/(2*math.pi*math.sqrt(L3*C16))-F)/F>0.08: continue   # 天線端帶通要落在 433 附近
    for C15,L4 in itertools.product(Cs,Ls):
        v=(L3,C16,C15,L4); res.append((evaluate(v),v))
res.sort()
for w,v in res[:8]:
    print(f"L3={v[0]/nH:.0f}nH C16={v[1]/pF:.1f}pF C15={v[2]/pF:.1f}pF L4={v[3]/nH:.0f}nH  worst-case ML={w:.2f}dB  "+' '.join(f"{peak(v,z)/1e6:.0f}" for z in ZCHIP.values()))

