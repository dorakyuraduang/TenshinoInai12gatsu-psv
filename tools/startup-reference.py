"""Export startup expectations from the actual Godot GameMain methods, without images."""
from pathlib import Path
import argparse, html, subprocess

parser = argparse.ArgumentParser()
parser.add_argument('resources')
parser.add_argument('reference')
parser.add_argument('--godot-root', type=Path,
                    help='Godot project directory containing Scripts (default: PSV parent directory).')
args = parser.parse_args()
psv = Path(__file__).resolve().parents[1]
root = (args.godot_root if args.godot_root is not None else psv.parent).resolve()
for name in ['GameMain.cs', 'Archive.cs', 'ScenarioScript.cs', 'PxDecoder.cs']:
    required = root / 'Scripts' / name
    if not required.is_file():
        parser.error(f'Godot reference source not found: {required}. '
                     'Pass --godot-root with the original Godot project directory.')
output = Path(args.reference).resolve()
output.mkdir(parents=True, exist_ok=True)
build = psv / 'build-host/startup-oracle'
build.mkdir(parents=True, exist_ok=True)
source = (root / 'Scripts/GameMain.cs').read_text(encoding='utf-8-sig')
start = source.index('            var sequence = new List<SceneEvent>();')
end = source.index('            _baseEvents =', start)
sequence = source[start:end]
start = source.index('    private void FinishSplash()')
end = source.index('    private void Start()', start)
finish = source[start:end]
start = source.index('        _eventIndex = _firstLaunch ?')
end = source.index(';', start) + 1
begin = source[start:end]
program = '''using System.Text.Json;
using Tenshi;
class Program {
    sealed class Surface { public bool Visible = true; }
    Surface _splash = new();
    bool _firstLaunch;
    int _prologueEventCount, _eventIndex;
    IReadOnlyList<SceneEvent> _events = [];
    string stage = "";
    void BeginStory() { BEGIN stage = "story"; }
    void ShowOpening() { stage = "opening"; }
    FINISH
    static void Main(string[] args) { new Program().Run(args); }
    void Run(string[] args) {
        var scripts = new Archive(Path.Combine(args[0], "tenshi_dvd.a"));
        SEQUENCE
        _firstLaunch = true; FinishSplash();
        string firstLaunchStage = stage;
        int firstStoryIndex = _eventIndex + 1;
        _firstLaunch = false; _splash.Visible = true; FinishSplash();
        string returnLaunchStage = stage;
        BeginStory();
        var firstDialogue = _events.Skip(firstStoryIndex).First(e => e.Kind == SceneEventKind.Dialogue);
        var postIntroDialogue = _events.Skip(_eventIndex + 1).First(e => e.Kind == SceneEventKind.Dialogue);
        var result = new {introEvents = _prologueEventCount, storyStart = _eventIndex + 1,
            firstLaunchStage, returnLaunchStage, firstDialogue, postIntroDialogue, events = _events};
        File.WriteAllText(Path.Combine(args[1], "startup.json"), JsonSerializer.Serialize(result,
            new JsonSerializerOptions {PropertyNamingPolicy = JsonNamingPolicy.CamelCase}));
        Console.WriteLine($"Godot startup oracle: {_prologueEventCount} intro events; new game begins at " +
            $"{postIntroDialogue.Source}:{postIntroDialogue.Value}; return launch = {returnLaunchStage}");
    }
}
'''.replace('BEGIN', begin).replace('FINISH', finish).replace('SEQUENCE', sequence)
(build / 'Program.cs').write_text(program, encoding='utf-8')
files = [root / 'Scripts' / name for name in ['Archive.cs', 'ScenarioScript.cs', 'PxDecoder.cs']]
files.append(psv / 'prepare/GodotData.cs.in')
items = ''.join('<Compile Include="' + html.escape(str(p)) + '" />' for p in files)
(build / 'Oracle.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
    '<OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework>'
    '<ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup>'
    + items + '</ItemGroup></Project>', encoding='utf-8')
subprocess.run(['dotnet', 'run', '--project', str(build / 'Oracle.csproj'), '-c', 'Release', '--',
    str(Path(args.resources).resolve()), str(output)], check=True)
