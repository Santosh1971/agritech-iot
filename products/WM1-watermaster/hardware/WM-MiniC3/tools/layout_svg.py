# To-scale layout SVG for WM-Mini C3 draft 0.3 (99 x 48, 8 px per mm, origin offset 40)
S=8; O=40
X=lambda x: round(O+S*x,1)
Y=lambda y: round(O+S*y,1)
W,H=99,48
o=[]; a=o.append
a('<svg viewBox="0 0 900 540" role="img" aria-label="WM-Mini C3 layout, 99 by 48 millimetres">')
a(f'<line class="dim" x1="{X(0)}" y1="14" x2="{X(W)}" y2="14"/><line class="dim" x1="{X(0)}" y1="8" x2="{X(0)}" y2="20"/><line class="dim" x1="{X(W)}" y1="8" x2="{X(W)}" y2="20"/>')
a(f'<text class="dim-t" x="{X(W/2)}" y="10" text-anchor="middle">99 mm</text>')
a(f'<line class="dim" x1="{X(W)+22}" y1="{Y(0)}" x2="{X(W)+22}" y2="{Y(H)}"/><line class="dim" x1="{X(W)+16}" y1="{Y(0)}" x2="{X(W)+28}" y2="{Y(0)}"/><line class="dim" x1="{X(W)+16}" y1="{Y(H)}" x2="{X(W)+28}" y2="{Y(H)}"/>')
a(f'<text class="dim-t" x="{X(W)+38}" y="{Y(H/2)}" transform="rotate(90 {X(W)+38} {Y(H/2)})" text-anchor="middle">48 mm</text>')
a(f'<rect class="pcb-mask" x="{X(0)}" y="{Y(0)}" width="{S*W}" height="{S*H}" rx="6"/>')
# zones
a(f'<rect class="selv-z" x="{X(19.3)}" y="{Y(0.3)}" width="{S*35.6}" height="{S*47.4}" rx="4"/>')
a(f'<text class="selv-t" x="{X(37.1)}" y="{Y(47.2)}" text-anchor="middle">VALVES 12/24 VAC</text>')
pts=[(57.6,21.2),(93.4,21.2),(93.4,42.3),(90.6,42.3),(90.6,48),(63.6,48),(63.6,42.3),(57.6,42.3)]
a('<polygon class="mains-z" points="'+" ".join(f"{X(x)},{Y(y)}" for x,y in pts)+'"/>')
a(f'<text class="mains-t" x="{X(75.5)}" y="{Y(20.4)}" text-anchor="middle">230 V</text>')
# holes (M3, 92 apart on centre line)
for hx in (3.5,95.5):
    a(f'<circle class="keep" cx="{X(hx)}" cy="{Y(24)}" r="{S*3.2}"/><circle class="hole" cx="{X(hx)}" cy="{Y(24)}" r="{S*1.6}"/>')
    a(f'<text class="lbl-s" x="{X(hx)}" y="{Y(29.0)}" text-anchor="middle">M3</text>')
# relays
def relay(c,t,inv,k,n,mains=False):
    b=t+19.6
    a(f'<rect class="relay" x="{X(c-7.7)}" y="{Y(t)}" width="{S*15.4}" height="{S*19.6}" rx="2"/>')
    ce = b-1.3 if inv else t+1.3
    co = b-3.25 if inv else t+3.25
    ct = t+4.1 if inv else b-4.1
    pc = "mains-pad" if mains else "selv-pad"
    a(f'<circle class="{pc}" cx="{X(c)}" cy="{Y(ce)}" r="{S*1.5}"/>')
    for d in (-6,6): a(f'<circle class="cu" cx="{X(c+d)}" cy="{Y(co)}" r="{S*1.25}"/>')
    a(f'<circle class="{pc}" cx="{X(c+6)}" cy="{Y(ct)}" r="{S*1.5}"/>')
    a(f'<circle class="nc-pad {"m" if mains else "s"}" cx="{X(c-6)}" cy="{Y(ct)}" r="{S*1.5}"/>')
    ly = (t+7.6) if inv else (t+10.2)
    a(f'<text class="lbl-b" x="{X(c)}" y="{Y(ly)}" text-anchor="middle">{k}</text>')
    a(f'<text class="lbl" x="{X(c)}" y="{Y(ly+2)}" text-anchor="middle">{n}</text>')
    lc = ce-1.9 if inv else ce+2.9
    a(f'<text class="lbl-s" x="{X(c)}" y="{Y(lc)}" text-anchor="middle">COM</text>')
    lt = ct+3.0 if inv else ct-2.1
    a(f'<text class="lbl-s" x="{X(c+6)}" y="{Y(lt)}" text-anchor="middle">NO</text><text class="lbl-s" x="{X(c-6)}" y="{Y(lt)}" text-anchor="middle">NC</text>')
    if inv: a(f'<text class="lbl-s" x="{X(c)}" y="{Y(t+12.6)}" text-anchor="middle">▲ inverted</text>')
A,B=0.5,22.4
relay(27.5,A,True,"K3","V1"); relay(45.5,A,True,"K4","V2")
relay(27.5,B,False,"K5","V3"); relay(45.5,B,False,"K6","V4")
relay(66.5,B,False,"K2","DOSER",True); relay(84.5,B,False,"K1","PUMP",True)
# centre band: coil + valve COM line
a(f'<path class="lv-trk" d="M{X(19.6)} {Y(21.25)} L{X(55)} {Y(21.25)}"/>')
a(f'<path class="lv-trk" d="M{X(52)} {Y(14.5)} L{X(90.5)} {Y(14.5)} M{X(60.5)} {Y(14.5)} L{X(60.5)} {Y(24.4)} M{X(72.5)} {Y(14.5)} L{X(72.5)} {Y(24.4)} M{X(78.5)} {Y(14.5)} L{X(78.5)} {Y(24.4)} M{X(90.5)} {Y(14.5)} L{X(90.5)} {Y(24.4)}"/>')
a(f'<text class="lbl-s" x="{X(37.1)}" y="{Y(20.85)}" text-anchor="middle" style="font-size:8.5px">coils · +5 V common · valve COM</text>')
a(f'<text class="lbl-s" x="{X(76)}" y="{Y(13.7)}" text-anchor="middle">pump / doser coil feed (stays ≥ 5 mm from 230 V)</text>')
# connectors (MSTBA horizontal, pins 2.5 mm from edge, plug overhangs)
def conn(x0,n,ref,name,labels,cls):
    w=n*5.08
    a(f'<rect class="plug" x="{X(x0)}" y="{Y(48)}" width="{S*w}" height="{S*6}" rx="2"/>')
    a(f'<rect class="conn {cls}" x="{X(x0)}" y="{Y(42.6)}" width="{S*w}" height="{S*5.4}" rx="2"/>')
    pc={"lv":"cu","selv":"selv-pad","mains":"mains-pad"}[cls]
    for i,l in enumerate(labels):
        px=x0+2.54+5.08*i
        a(f'<circle class="{pc}" cx="{X(px)}" cy="{Y(45.5)}" r="{S*1.3}"/>')
        a(f'<text class="lbl-c" x="{X(px)}" y="{Y(52.6)}" text-anchor="middle">{l}</text>')
    a(f'<text class="lbl-c b" x="{X(x0+w/2)}" y="{Y(55.0)}" text-anchor="middle">{ref} {name}</text>')
conn(20,3,"J1","FLOW",["5V","G","SIG"],"lv")
conn(35.8,5,"J4","VALVES",["COM","V1","V2","V3","V4"],"selv")
conn(64.0,2,"J3","DOSER",["COM","NO"],"mains")
conn(79.3,2,"J2","PUMP",["COM","NO"],"mains")
# U1 Super Mini, USB at bottom edge
a(f'<rect class="part" x="{X(1)}" y="{Y(25.5)}" width="{S*18}" height="{S*22.5}" rx="3"/>')
a(f'<rect class="usb" x="{X(5.5)}" y="{Y(46)}" width="{S*9}" height="{S*3.4}" rx="5"/>')
a(f'<text class="lbl-c b" x="{X(10)}" y="{Y(55.0)}" text-anchor="middle">USB-C</text>')
ra=["5V","G","3V3","4","3","2","1","0"]; rb=["5","6","7","8","9","10","20","21"]
for k in range(8):
    py=44.2-2.54*k
    a(f'<rect class="cu" x="{X(2.38)-4}" y="{Y(py)-4}" width="8" height="8"/><rect class="cu" x="{X(17.62)-4}" y="{Y(py)-4}" width="8" height="8"/>')
    a(f'<text class="lbl-s" x="{X(2.38)+6}" y="{Y(py)+3.5}">{ra[k]}</text><text class="lbl-s" x="{X(17.62)-6}" y="{Y(py)+3.5}" text-anchor="end">{rb[k]}</text>')
# parts under the module
a(f'<rect class="under" x="{X(7)}" y="{Y(26.6)}" width="{S*6}" height="{S*9.9}" rx="1"/><text class="lbl-s" x="{X(10)}" y="{Y(31.0)}" text-anchor="middle">U3</text><text class="lbl-s" x="{X(10)}" y="{Y(32.4)}" text-anchor="middle">ULN</text><text class="lbl-s" x="{X(10)}" y="{Y(33.8)}" text-anchor="middle">2003</text>')
a(f'<rect class="under" x="{X(7)}" y="{Y(37.2)}" width="{S*6}" height="{S*4.9}" rx="1"/><text class="lbl-s" x="{X(10)}" y="{Y(39.5)}" text-anchor="middle">U2</text><text class="lbl-s" x="{X(10)}" y="{Y(40.9)}" text-anchor="middle">DS1307</text>')
a(f'<rect class="under" x="{X(7)}" y="{Y(42.6)}" width="{S*6}" height="{S*1.6}" rx="1"/><text class="lbl-s" x="{X(16.2)}" y="{Y(46.9)}" text-anchor="middle"> </text>')
a(f'<text class="lbl-s" x="{X(10)}" y="{Y(26.3)+0}" text-anchor="middle" style="font-size:8px">under module ↓</text>')
a(f'<circle class="led-b" cx="{X(10)}" cy="{Y(24.4)}" r="4"/>')
a(f'<text class="lbl-s" x="{X(13.4)}" y="{Y(24.8)}">IO8 WiFi</text>')
# LV column top: LEDs (vertical 1206), resistors, C1, flow parts
leds=["P","D","V1","V2","V3","V4","F"]
for i,l in enumerate(leds):
    cx=3.2+2.55*i
    a(f'<rect class="{"led-g" if l=="F" else "led"}" x="{X(cx-0.8)}" y="{Y(2.6)}" width="{S*1.6}" height="{S*3.2}" rx="1.5"/>')
    a(f'<text class="lbl-s" x="{X(cx)}" y="{Y(2.1)}" text-anchor="middle" style="font-size:8px">{l}</text>')
    a(f'<rect class="silk" x="{X(cx-0.6)}" y="{Y(6.5)}" width="{S*1.2}" height="{S*2.0}"/>')
a(f'<circle class="part" cx="{X(6.2)}" cy="{Y(14)}" r="{S*3.15}"/><text class="lbl" x="{X(6.2)}" y="{Y(13.9)}" text-anchor="middle">C1</text><text class="lbl-s" x="{X(6.2)}" y="{Y(15.4)}" text-anchor="middle">470µ</text>')
a(f'<rect class="silk" x="{X(11)}" y="{Y(10.2)}" width="{S*7.6}" height="{S*7.6}" rx="2"/>')
for i,t in enumerate(["flow in","D·R·ESD","4K7 ×2","IO2 pad"]):
    a(f'<text class="lbl-s" x="{X(14.8)}" y="{Y(12.2+1.7*i)}" text-anchor="middle" style="font-size:8.5px">{t}</text>')
# top-right: battery
a(f'<rect class="cell" x="{X(66)}" y="{Y(3.5)}" width="{S*22}" height="{S*7}" rx="5"/><text class="lbl-dark" x="{X(77)}" y="{Y(7.6)}" text-anchor="middle">BT1 CR2032 vertical</text>')
# notes under drawing
a(f'<text class="dim-t" x="{X(49.5)}" y="{Y(H)+86}" text-anchor="middle">▼ all cables (USB, flow, valves, doser, pump) leave from this edge · 2 boards per 98 × 98 panel ▼</text>')
a('</svg>')
print("\n".join(o))
