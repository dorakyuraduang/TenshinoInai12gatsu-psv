"""Rebuild the explicitly bundled H.264 opening from the matching original video."""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("source",type=Path)
parser.add_argument("--ffmpeg",default="ffmpeg")
parser.add_argument("--ffprobe",default="ffprobe")
args=parser.parse_args()
assets=Path(__file__).resolve().parents[1]/"assets"
metaPath=assets/"opening.json"
meta=json.loads(metaPath.read_text(encoding="utf-8"))
sourceHash=hashlib.sha256(args.source.read_bytes()).hexdigest()
if sourceHash!=meta["sourceSha256"]:
    raise SystemExit("Source differs from the version recorded in assets/opening.json")
output=assets/"opening.mp4"
subprocess.run([args.ffmpeg,"-hide_banner","-y","-i",str(args.source),*meta["conversion"],str(output)],check=True)
subprocess.run([args.ffmpeg,"-v","error","-i",str(output),"-f","null","-"],check=True)
probe=json.loads(subprocess.check_output([args.ffprobe,"-v","error","-count_frames","-show_streams","-show_format","-of","json",str(output)],text=True))
v,a=probe["streams"]
assert (v["codec_name"],v["profile"],v["level"],v["width"],v["height"],v["pix_fmt"],v["avg_frame_rate"],v["nb_read_frames"])==("h264","Main",31,800,600,"yuv420p","30/1","2695")
assert (a["codec_name"],a["sample_rate"],a["channels"])==("aac","44100",2)
meta.update(outputSha256=hashlib.sha256(output.read_bytes()).hexdigest(),outputBytes=output.stat().st_size,duration=probe["format"]["duration"])
meta["encoderVersion"]=subprocess.check_output([args.ffmpeg,"-version"],text=True).splitlines()[0]
metaPath.write_text(json.dumps(meta,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
print(f"Validated {v['nb_read_frames']} frames: {output}")

subprocess.run([sys.executable, str(Path(__file__).with_name("index-opening.py")), "--ffprobe", args.ffprobe], check=True)
