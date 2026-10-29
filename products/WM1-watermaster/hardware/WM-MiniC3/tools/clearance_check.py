# clearance check for draft 0.3 placement (mm). pads: (x,y,r,net-class)
import math
P=[]
def relay(c,t,inv,cls_com,cls_ct,name):
    # upright SRD, body c±7.7, t..t+19.6. normal: COM/coil at top. inverted: at bottom.
    b=t+19.6
    if not inv:
        P.append((c,t+1.3,1.5,cls_com,name+".COM")); [P.append((c+d,t+3.25,1.25,"LV",name+".coil")) for d in(-6,6)]
        P.append((c+6,b-4.1,1.5,cls_ct,name+".NO")); P.append((c-6,b-4.1,1.5,cls_com,name+".NC"))
    else:
        P.append((c,b-1.3,1.5,cls_com,name+".COM")); [P.append((c+d,b-3.25,1.25,"LV",name+".coil")) for d in(-6,6)]
        P.append((c+6,t+4.1,1.5,cls_ct,name+".NO")); P.append((c-6,t+4.1,1.5,cls_com,name+".NC"))
A,B=0.5,22.4
relay(27.5,A,True,"SELV","SELV","V1"); relay(45.5,A,True,"SELV","SELV","V2")
relay(27.5,B,False,"SELV","SELV","V3"); relay(45.5,B,False,"SELV","SELV","V4")
relay(66.5,B,False,"MAINS_D","MAINS_D","DOSER"); relay(84.5,B,False,"MAINS_P","MAINS_P","PUMP")
py=45.5
for i,x in enumerate([22.54,27.62,32.7]): P.append((x,py,1.3,"LV","J1.%d"%i))
for i,x in enumerate([38.34,43.42,48.5,53.58,58.66]): P.append((x,py,1.3,"SELV","J4.%d"%i))
for i,x in enumerate([66.54,71.62]): P.append((x,py,1.3,"MAINS_D","J3.%d"%i))
for i,x in enumerate([81.84,86.92]): P.append((x,py,1.3,"MAINS_P","J2.%d"%i))
worst={}
for i in range(len(P)):
  for j in range(i+1,len(P)):
    a,b=P[i],P[j]
    if a[3]==b[3]: continue
    if a[4].split('.')[0]==b[4].split('.')[0] and "J" not in a[4]: continue  # inside one relay = relay's own spacing
    d=math.dist(a[:2],b[:2])-a[2]-b[2]
    k=tuple(sorted((a[3],b[3])))
    if k not in worst or d<worst[k][0]: worst[k]=(round(d,2),a[4],b[4])
for k,v in sorted(worst.items()): print(k,v)
