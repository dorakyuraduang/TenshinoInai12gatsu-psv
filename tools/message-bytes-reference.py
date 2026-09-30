"""Export unformatted dialogue byte counts using the same .NET encoding as Godot."""
from pathlib import Path
import argparse, subprocess
parser = argparse.ArgumentParser()
parser.add_argument('reference')
parser.add_argument('--godot-root', type=Path,
                    help='Godot project directory containing Scripts (default: PSV parent directory).')
args = parser.parse_args()
psv = Path(__file__).resolve().parents[1]
root = (args.godot_root if args.godot_root is not None else psv.parent).resolve()
required = root / 'Scripts/GameMain.Messages.cs'
if not required.is_file():
    parser.error(f'Godot reference source not found: {required}. '
                 'Pass --godot-root with the original Godot project directory.')
reference = Path(args.reference).resolve()
build = psv / 'build-host/message-bytes-oracle'
build.mkdir(parents=True, exist_ok=True)
source = (root / 'Scripts/GameMain.Messages.cs').read_text(encoding='utf-8-sig')
assert 'Encoding.GetEncoding("GB18030").GetByteCount(_activeMessage.Text)' in source
(build / 'Oracle.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings></PropertyGroup></Project>', encoding='utf-8')
(build / 'Program.cs').write_text(r'''using System.Text; using System.Text.Json;
Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
var encoding = Encoding.GetEncoding("GB18030");
var texts = new HashSet<string>{"", "ASCII", "中文", "\ue000\ue001", "\u007f$p48<文字>", "\u0080\u20ac", "𠀀😀"};
foreach (var file in Directory.EnumerateFiles(Path.Combine(args[0], "coverage"), "*.json")) {
    using var d = JsonDocument.Parse(File.ReadAllText(file));
    if (!d.RootElement.TryGetProperty("events", out var es) || es.ValueKind != JsonValueKind.Array) continue;
    foreach (var e in es.EnumerateArray()) if (e.GetProperty("kind").GetInt32() == 2) texts.Add(e.GetProperty("text").GetString()!);
}
using var writer = new StreamWriter(Path.Combine(args[0], "message-bytes.jsonl"));
foreach (var text in texts.OrderBy(s => s, StringComparer.Ordinal)) writer.WriteLine(JsonSerializer.Serialize(new{text, bytes=encoding.GetByteCount(text)}));
Console.WriteLine($"Godot .NET GB18030 oracle: {texts.Count} unformatted messages");
''', encoding='utf-8')
subprocess.run(['dotnet', 'run', '--project', str(build / 'Oracle.csproj'), '-c', 'Release', '--', str(reference)], check=True)
