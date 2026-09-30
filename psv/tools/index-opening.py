"""Build a checked packet index for the bundled MP4; no video/audio re-encoding."""
from pathlib import Path
import hashlib, json, struct, subprocess, fractions, re, argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--ffprobe",default="ffprobe")
args=parser.parse_args()

assets=Path(__file__).resolve().parents[1]/'assets'
movie=assets/'opening.mp4'
meta_path=assets/'opening.json'
meta=json.loads(meta_path.read_text(encoding='utf-8'))
assert hashlib.sha256(movie.read_bytes()).hexdigest()==meta['outputSha256']
def probe(*args):
    return json.loads(subprocess.check_output([globals()['args'].ffprobe,'-v','error',*args,'-of','json',str(movie)]))
streams=probe('-show_streams','-show_data')['streams']
v,a=streams
assert (v['codec_name'],v['width'],v['height'],v['has_b_frames'])==('h264',800,600,0)
assert (a['codec_name'],a['profile'],a['sample_rate'],a['channels'])==('aac','LC','44100',2)
def extra(s):
    return bytes.fromhex(''.join(line.split(':',1)[1].split('  ',1)[0].replace(' ','') for line in s['extradata'].splitlines() if ':' in line))
avcc=extra(v)
assert avcc[0]==1 and avcc[4]&3==3
pos=6
parameters=[]
for n in (avcc[5]&31,):
    for _ in range(n):
        size=int.from_bytes(avcc[pos:pos+2],'big');pos+=2
        parameters.append(avcc[pos:pos+size]);pos+=size
n=avcc[pos];pos+=1
for _ in range(n):
    size=int.from_bytes(avcc[pos:pos+2],'big');pos+=2
    parameters.append(avcc[pos:pos+size]);pos+=size
assert pos==len(avcc) and [x[0]&31 for x in parameters]==[7,8]
# Main-profile SPS: derive the coded geometry/reference count from the actual bitstream.
sps=parameters[0]
rbsp=re.sub(b'\x00\x00\x03',b'\x00\x00',sps[1:])
bits=''.join(f'{b:08b}' for b in rbsp); bit=0
def get(n):
    global bit
    value=int(bits[bit:bit+n],2);bit+=n;return value
def ue():
    n=0
    while get(1)==0:n+=1
    return (1<<n)-1+(get(n) if n else 0)
assert get(8)==77
get(8);get(8);ue();ue()
poc=ue()
assert poc==2 or poc==0
if poc==0:ue()
refs=ue();get(1)
coded_w=(ue()+1)*16;coded_h=(ue()+1)*16
assert get(1)==1
assert (coded_w,coded_h,refs)==(800,608,2)
packets=probe('-show_packets','-show_entries','packet=stream_index,pos,size,pts,dts,duration')['packets']
video=[];audio=[]
for p in packets:
    offset=int(p['pos']);size=int(p['size'])
    assert 0<offset<movie.stat().st_size and 0<size<=512*1024 and offset+size<=movie.stat().st_size
    if p['stream_index']==v['index']:
        assert p['pts']==p['dts']
        stamp=fractions.Fraction(p['pts'])*fractions.Fraction(v['time_base'])*90000
        assert stamp.denominator==1
        video.append((offset,size,int(stamp)))
    else:
        assert p['stream_index']==a['index'] and size<=1536
        audio.append((offset,size,int(p['pts'])))
assert len(video)==2695 and [p[2] for p in video]==list(range(0,2695*3000,3000))
assert audio[0][2]==-1024 and all(abs(p[2]-(i-1)*1024)<=1 for i,p in enumerate(audio))
# MOV timestamp rescaling has occasional one-sample rounding; AAC grains are exactly 1024 samples.
audio=[(p[0],p[1],(i-1)*1024) for i,p in enumerate(audio)]
config=b''.join(b'\x00\x00\x00\x01'+x for x in parameters)
index=struct.pack('<8s7I3QI',b'TNSMP401',800,600,refs,44100,2,len(video),len(audio),movie.stat().st_size,int(a['duration_ts']),2695*3000,len(config))+config
index+=b''.join(struct.pack('<QIq',*p) for p in video+audio)
out=assets/'opening.idx'
out.write_bytes(index)
meta['packetIndex']={'file':'opening.idx','sha256':hashlib.sha256(index).hexdigest(),'bytes':len(index),'videoPackets':len(video),'audioPackets':len(audio),'codedWidth':coded_w,'codedHeight':coded_h,'referenceFrames':refs,'audioSamples':int(a['duration_ts']),'format':'TNSMP401; AVCC length=4; video PTS 90 kHz; audio PTS 44.1 kHz'}
meta_path.write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(meta['packetIndex'],indent=2))
