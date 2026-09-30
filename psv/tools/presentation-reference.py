"""Create a text-only oracle using the actual Godot layout/effect methods."""
from pathlib import Path
import argparse, subprocess, html
parser=argparse.ArgumentParser();parser.add_argument('reference');args=parser.parse_args()
root=Path(__file__).resolve().parents[2];out=Path(args.reference).resolve();build=root/'psv'/'build-host'/'presentation-oracle';build.mkdir(parents=True,exist_ok=True)
source=(root/'Scripts'/'MessageView.cs').read_text(encoding='utf-8-sig');start=source.index('    public void SetMessage(string text)');end=source.index('    public void Tick(',start)
stub='''using System.Globalization; using Godot;
namespace Godot { public readonly record struct Vector2(float X,float Y) {public static Vector2 Zero=>new(0,0);} }
namespace Tenshi {
public class MessageView {
public readonly record struct Glyph(string Text, Vector2 Position, float Advance, float Offset, int FontSize);
private readonly List<List<Glyph>> _pages=new();private string _text="";private float _revealed;public int Page;
public bool FullPage;public int FontSize=>FullPage?26:24;public int LineHeight=>FullPage?42:28;public int WrapWidth=>FullPage?640:480;
public Vector2 Size=>new(FullPage?654:494,FullPage?520:90);public IReadOnlyList<List<Glyph>> Pages=>_pages;private void QueueRedraw(){}
'''+source[start:end]+'\n}}\n'
(build/'Layout.cs').write_text(stub,encoding='utf-8')
(build/'Oracle.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings></PropertyGroup><ItemGroup><Compile Include="'+html.escape(str(root/'Scripts'/'OriginalEffectMath.cs'))+'" /></ItemGroup></Project>',encoding='utf-8')
program='''using System.Text.Json;using Tenshi;
class Program {static void Main(string[] args){
var texts=new HashSet<string>{"", "A\\u007fB", "$p48A<$p12B>C", new string('中',19)+"「中文。", "$p-4A", new string('文',1000)};
foreach(var file in Directory.EnumerateFiles(Path.Combine(args[0],"coverage"),"*.json")){using var d=JsonDocument.Parse(File.ReadAllText(file));if(!d.RootElement.TryGetProperty("events",out var es)||es.ValueKind!=JsonValueKind.Array)continue;foreach(var e in es.EnumerateArray())if(e.GetProperty("kind").GetInt32()==2)texts.Add(e.GetProperty("text").GetString()!.Replace("\\ue000","时纪").Replace("\\ue001","木田"));}
using var writer=new StreamWriter(Path.Combine(args[0],"presentation.jsonl"));int count=0;
foreach(string text in texts.OrderBy(s=>s,StringComparer.Ordinal))foreach(bool full in new[]{false,true}){var layout=new MessageView{FullPage=full};layout.SetMessage(text);writer.WriteLine(JsonSerializer.Serialize(new {text,full,pages=layout.Pages.Select(p=>p.Select(g=>new object[]{g.Text,g.Position.X,g.Position.Y,g.Advance,g.Offset,g.FontSize}))}));count++;}
var effects=new List<object>();foreach(int effect in new[]{4959,5251,6036,6200})for(int frame=0;frame<100;frame++){var s=OriginalEffectMath.Sample(effect,frame);effects.Add(new{effect,frame,x=s.Shift.X,y=s.Shift.Y,zoom=s.Zoom,flash=s.Flash});}File.WriteAllText(Path.Combine(args[0],"effects.json"),JsonSerializer.Serialize(effects));Console.WriteLine($"Godot oracle: {count} message layouts and {effects.Count} effect samples");}}
'''
(build/'Program.cs').write_text(program,encoding='utf-8')
subprocess.run(['dotnet','run','--project',str(build/'Oracle.csproj'),'-c','Release','--',str(out)],check=True)
