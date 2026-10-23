using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using Microsoft.Win32;

namespace ChainedGuard {
    // Runs synchronously on the caller's background worker. Never starts a process or hashes game files.
    internal static class GameDiscovery {
        internal sealed class Candidate {
            public string ExePath, Folder, Source, Store, SteamAppId, LaunchUri;
        }
        internal sealed class Options {
            public string SavedFolder, LauncherFolder;
            public IList<string> KnownFolders=null, SteamRoots=null;
            // Non-null replaces DriveInfo with explicit roots, including an empty list for fixture-only work.
            public IList<string> SearchRoots=null;
            public bool IncludeRegistry=true, ScanDrives=true;
            public TimeSpan TimeBudget=TimeSpan.FromSeconds(30);
            public int EntryBudget=250000, MaximumDepth=48;
            public Func<bool> MayContinue;
        }
        internal sealed class Progress {
            public string CurrentFolder, Message;
            public int EntriesVisited;
            public Candidate Candidate;
        }
        internal sealed class Result {
            public readonly List<Candidate> Candidates=new List<Candidate>();
            public readonly List<string> Diagnostics=new List<string>();
            public bool Complete, Cancelled, BudgetExceeded;
            public int EntriesVisited;
        }

        public static bool ValidateFolder(string folderOrExe, out Candidate candidate, out string reason) {
            candidate=null;reason=null;
            try {
                if(string.IsNullOrWhiteSpace(folderOrExe)){reason=I18n.Text("Папка игры не выбрана.","No game folder selected.");return false;}
                string input=folderOrExe.Trim().Trim('"');
                if(IsArchive(input) && !Directory.Exists(input)){reason=I18n.Text("Сначала распакуй весь архив игры в отдельную папку, затем выбери ChainedBeasts.exe.","Extract the entire game archive to a separate folder, then select ChainedBeasts.exe.");return false;}
                string full=LocalPath(input);
                if(full==null){reason=I18n.Text("Выбери установленную игру на локальном диске.","Select a game installation on a local drive.");return false;}
                string folder=string.Equals(Path.GetFileName(full),"ChainedBeasts.exe",StringComparison.OrdinalIgnoreCase)?Path.GetDirectoryName(full):full;
                string exe=Path.Combine(folder,"ChainedBeasts.exe");
                if(!File.Exists(exe)){reason=I18n.Text("В выбранной папке нет ChainedBeasts.exe. Если игра в архиве, распакуй его полностью.","ChainedBeasts.exe was not found in this folder. If the game is archived, extract it completely.");return false;}
                if(!IsX64Pe(exe,false)){reason=I18n.Text("Нужен Windows x64 ChainedBeasts.exe.","The Windows x64 ChainedBeasts.exe is required.");return false;}
                if(!IsX64Pe(Path.Combine(folder,"GameAssembly.dll"),true) || !IsX64Pe(Path.Combine(folder,"UnityPlayer.dll"),true)) {
                    reason=I18n.Text("Папка игры неполная: нужны GameAssembly.dll и UnityPlayer.dll для Windows x64. Распакуй или восстанови всю игру.","The game folder is incomplete: Windows x64 GameAssembly.dll and UnityPlayer.dll are required. Extract or restore the complete game.");return false;
                }
                string metadata=Path.Combine(folder,"ChainedBeasts_Data","il2cpp_data","Metadata","global-metadata.dat");
                using(var stream=new FileStream(metadata,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete))
                using(var reader=new BinaryReader(stream)) {
                    if(stream.Length<8 || reader.ReadUInt32()!=0xFAB11BAFu){reason=I18n.Text("Метаданные ChainedBeasts_Data отсутствуют или повреждены. Нужна полностью распакованная игра.","ChainedBeasts_Data metadata is missing or damaged. The complete extracted game is required.");return false;}
                    int version=reader.ReadInt32();
                    if(version<16 || version>128){reason=I18n.Text("Метаданные игры не распознаны.","Game metadata was not recognized.");return false;}
                }
                candidate=new Candidate{Folder=folder,ExePath=exe,Source=I18n.Text("Выбрана папка","Selected folder"),Store="Direct"};return true;
            } catch(Exception ex) { if(!IsFileError(ex))throw;
                reason=I18n.Text("Не удалось прочитать полную папку игры: ","Could not read the complete game folder: ")+ex.Message;return false;
            }
        }

        public static Result Discover(Options options, CancellationToken cancellation, Action<Progress> progress=null) {
            var scan=new Scan(options??new Options(),cancellation,progress);return scan.Run();
        }

        private sealed class Scan {
            private readonly Options options;
            private readonly CancellationToken cancellation;
            private readonly Action<Progress> progress;
            private readonly Stopwatch timer=Stopwatch.StartNew();
            private readonly Result result=new Result();
            private readonly HashSet<string> examined=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            private readonly HashSet<string> libraries=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            private readonly HashSet<string> steamRoots=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            private readonly HashSet<string> excluded=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            private bool depthLimited;
            private long nextProgress;
            public Scan(Options options,CancellationToken cancellation,Action<Progress> progress) {
                this.options=options;this.cancellation=cancellation;this.progress=progress;
                AddExcluded(Path.GetTempPath());AddExcluded(Environment.GetEnvironmentVariable("TEMP"));AddExcluded(Environment.GetEnvironmentVariable("TMP"));
                AddExcluded(Environment.GetFolderPath(Environment.SpecialFolder.Windows));
                AddExcluded(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData));
                AddExcluded(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData));
                AddExcluded(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
            }
            private void AddExcluded(string path){string full=LocalPath(path);if(full!=null)excluded.Add(full.TrimEnd(Path.DirectorySeparatorChar));}
            public Result Run() {
                try {
                    Probe(options.SavedFolder,I18n.Text("Сохранённая папка","Saved folder"),null);
                    if(options.KnownFolders!=null)foreach(string path in options.KnownFolders){if(!Check())break;Probe(path,I18n.Text("Известная папка","Known folder"),null);}
                    Nearby(options.LauncherFolder);
                    if(options.SteamRoots!=null)foreach(string path in options.SteamRoots){if(!Check())break;AddSteam(path);}
                    if(options.IncludeRegistry && Check()) ReadRegistry();
                    foreach(string root in steamRoots){if(!Check())break;ReadSteam(root);}
                    // Known locations may contain several installs. A fallback scan stops at the first complete install.
                    if(result.Candidates.Count==0 && options.ScanDrives && Check()) {
                        IList<string> roots=options.SearchRoots??LocalDriveRoots();
                        Walk(roots);
                    }
                    if(depthLimited && result.Candidates.Count==0){result.BudgetExceeded=true;Note(I18n.Text("Достигнут предел глубины папок; выбери папку игры вручную.","The folder depth limit was reached; select the game folder manually."));}
                    if(!result.Cancelled && !result.BudgetExceeded)result.Complete=true;
                } catch(Exception ex) { if(!IsFileError(ex))throw; Note(I18n.Text("Поиск остановлен: ","Search stopped: ")+ex.Message); }
                Publish(null,I18n.Text("Поиск завершён","Search complete"),null,true);return result;
            }
            private bool Check() {
                if(result.Cancelled || result.BudgetExceeded)return false;
                if(cancellation.IsCancellationRequested || (options.MayContinue!=null && !options.MayContinue())) {
                    result.Cancelled=true;Note(I18n.Text("Поиск отменён или приостановлен запуском игры.","Search was canceled or paused because the game is starting."));return false;
                }
                if(timer.Elapsed>=options.TimeBudget || result.EntriesVisited>=Math.Max(1,options.EntryBudget)) {
                    result.BudgetExceeded=true;Note(I18n.Text("Достигнут лимит поиска; выбери папку игры или повтори поиск.","The search limit was reached; select the game folder or search again."));return false;
                }
                return true;
            }
            private void Note(string message){if(result.Diagnostics.Count<40)result.Diagnostics.Add(message);}
            private void Publish(string folder,string message,Candidate candidate,bool force) {
                if(progress==null || (!force && timer.ElapsedMilliseconds<nextProgress))return;
                nextProgress=timer.ElapsedMilliseconds+250;
                progress(new Progress{CurrentFolder=folder,Message=message,EntriesVisited=result.EntriesVisited,Candidate=candidate});
            }
            private void Probe(string path,string source,Action<Candidate> annotate) {
                if(!Check())return;
                string normalized=LocalPath(path);if(normalized==null)return;
                if(string.Equals(Path.GetFileName(normalized),"ChainedBeasts.exe",StringComparison.OrdinalIgnoreCase))normalized=Path.GetDirectoryName(normalized);
                Candidate existing=result.Candidates.Find(delegate(Candidate item){return string.Equals(item.Folder,normalized,StringComparison.OrdinalIgnoreCase);});
                if(existing!=null){if(annotate!=null)annotate(existing);return;}
                // A later Steam manifest can add store metadata to a previously discovered path.
                if(!examined.Add(normalized))return;
                Candidate candidate;string reason;
                if(!ValidateFolder(normalized,out candidate,out reason))return;
                candidate.Source=source;if(annotate!=null)annotate(candidate);
                result.Candidates.Add(candidate);Publish(candidate.Folder,I18n.Text("Найдена папка игры","Game folder found"),candidate,true);
            }
            private void Nearby(string launcher) {
                string folder=LocalPath(launcher);if(folder==null)return;
                if(File.Exists(folder))folder=Path.GetDirectoryName(folder);
                for(int level=0;level<3 && folder!=null && Check();level++) {
                    Probe(folder,I18n.Text("Рядом с Dodger","Near Dodger"),null);
                    try {
                        foreach(string child in Directory.EnumerateDirectories(folder)) {
                            if(!Visit() || !Check())return;
                            if(IsGameName(Path.GetFileName(child)) && !IsReparse(child))Probe(child,I18n.Text("Рядом с Dodger","Near Dodger"),null);
                        }
                    } catch(Exception ex) { if(!IsFileError(ex))throw; }
                    folder=Path.GetDirectoryName(folder);
                }
            }
            private bool Visit(){result.EntriesVisited++;return Check();}
            private void AddSteam(string path){string full=LocalPath(path);if(full!=null)steamRoots.Add(full);}
            private void ReadRegistry() {
                foreach(RegistryHive hive in new[]{RegistryHive.CurrentUser,RegistryHive.LocalMachine})
                foreach(RegistryView view in new[]{RegistryView.Registry64,RegistryView.Registry32}) {
                    if(!Check())return;
                    try {
                        using(RegistryKey root=RegistryKey.OpenBaseKey(hive,view)) {
                            using(RegistryKey steam=root.OpenSubKey(@"Software\Valve\Steam")) {
                                if(steam!=null){AddSteam(steam.GetValue("SteamPath") as string);AddSteam(steam.GetValue("InstallPath") as string);}
                            }
                            using(RegistryKey uninstall=root.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall")) {
                                if(uninstall!=null)foreach(string name in uninstall.GetSubKeyNames()) {
                                    if(!Visit())return;
                                    using(RegistryKey item=uninstall.OpenSubKey(name))if(item!=null && IsGameName(item.GetValue("DisplayName") as string)) {
                                        string source=I18n.Text("Установленная игра","Installed game");
                                        Probe(item.GetValue("InstallLocation") as string,source,null);
                                        string icon=IconPath(item.GetValue("DisplayIcon") as string);
                                        if(icon!=null)Probe(Path.GetDirectoryName(icon),source,null);
                                    }
                                }
                            }
                            using(RegistryKey gog=root.OpenSubKey(@"Software\GOG.com\Games")) {
                                if(gog!=null)foreach(string name in gog.GetSubKeyNames()) {
                                    if(!Visit())return;
                                    using(RegistryKey item=gog.OpenSubKey(name))if(item!=null && (IsGameName(item.GetValue("gameName") as string)||IsGameName(item.GetValue("GAMENAME") as string)))
                                        Probe(item.GetValue("path") as string,"GOG",delegate(Candidate candidate){candidate.Store="GOG";});
                                }
                            }
                        }
                    } catch(Exception ex) { if(!IsRegistryError(ex))throw; Note(I18n.Text("Один из источников реестра недоступен.","One registry source is unavailable.")); }
                }
            }
            private void ReadSteam(string root) {
                if(!Check())return;
                var paths=new List<string>();paths.Add(root);
                foreach(string file in new[]{Path.Combine(root,"steamapps","libraryfolders.vdf"),Path.Combine(root,"config","libraryfolders.vdf")}) {
                    List<VdfEntry> entries=ReadVdfFile(file);
                    if(entries!=null)CollectLibraries(entries,paths);
                }
                foreach(string path in paths) {
                    if(!Check())return;
                    string library=LocalPath(path);if(library==null || !libraries.Add(library))continue;
                    string apps=Path.Combine(library,"steamapps");
                    try {
                        foreach(string manifest in Directory.EnumerateFiles(apps,"appmanifest_*.acf")) {
                            if(!Visit())return;
                            List<VdfEntry> entries=ReadVdfFile(manifest);if(entries==null)continue;
                            VdfEntry state=Find(entries,"AppState");if(state==null || state.Children==null)continue;
                            string title=Value(state.Children,"name"),install=Value(state.Children,"installdir"),id=Value(state.Children,"appid");
                            if(!IsGameName(title) && !IsGameName(install))continue;
                            string common=Path.Combine(apps,"common"),folder=ChildPath(common,install);if(folder==null)continue;
                            string appId=NumericId(id) && string.Equals(Path.GetFileNameWithoutExtension(manifest),"appmanifest_"+id,StringComparison.OrdinalIgnoreCase)?id:null;
                            Probe(folder,I18n.Text("Библиотека Steam","Steam library"),delegate(Candidate candidate){candidate.Store="Steam";candidate.SteamAppId=appId;candidate.LaunchUri=appId==null?null:"steam://rungameid/"+appId;});
                        }
                    } catch(Exception ex) { if(!IsFileError(ex))throw; }
                }
            }
            private List<VdfEntry> ReadVdfFile(string path) {
                if(!Check())return null;
                try {
                    var info=new FileInfo(path);if(!info.Exists || info.Length>1024*1024)return null;
                    return ParseVdf(File.ReadAllText(path));
                } catch(Exception ex) { if(!IsFileError(ex) && !(ex is FormatException))throw; return null; }
            }
            private IList<string> LocalDriveRoots() {
                var roots=new List<string>();
                foreach(DriveInfo drive in DriveInfo.GetDrives()) {
                    if(!Check())break;
                    try {
                        if((drive.DriveType==DriveType.Fixed || drive.DriveType==DriveType.Removable) && drive.IsReady)roots.Add(drive.RootDirectory.FullName);
                    } catch(Exception ex) { if(!IsFileError(ex))throw; }
                }
                return roots;
            }
            private sealed class Work {public string Folder;public int Depth;}
            private void Walk(IList<string> roots) {
                var queue=new Queue<Work>();var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                // Seed every ready local drive before descending into any of them. A large first drive
                // must not consume the entire budget before another drive's shallow folders are visited.
                foreach(string root in roots) {
                    if(!Check())return;
                    string full=LocalPath(root);
                    if(full!=null && !IsReparse(full) && !Skip(full) && seen.Add(full))queue.Enqueue(new Work{Folder=full});
                }
                while(queue.Count>0 && Check()) {
                    Work work=queue.Dequeue();Publish(work.Folder,I18n.Text("Поиск на локальных дисках","Searching local drives"),null,false);
                    try {
                        foreach(string path in Directory.EnumerateFileSystemEntries(work.Folder)) {
                            if(!Visit())return;
                            FileAttributes attributes;
                            try{attributes=File.GetAttributes(path);}catch(Exception ex) { if(!IsFileError(ex))throw;continue;}
                            if((attributes&FileAttributes.ReparsePoint)!=0)continue;
                            if((attributes&FileAttributes.Directory)!=0) {
                                if(work.Depth<Math.Max(0,options.MaximumDepth) && !Skip(path) && seen.Add(path))queue.Enqueue(new Work{Folder=path,Depth=work.Depth+1});
                                else if(work.Depth>=Math.Max(0,options.MaximumDepth) && !Skip(path))depthLimited=true;
                            } else if(string.Equals(Path.GetFileName(path),"ChainedBeasts.exe",StringComparison.OrdinalIgnoreCase)) {
                                Probe(work.Folder,I18n.Text("Поиск на диске","Drive search"),null);if(result.Candidates.Count>0)return;
                            }
                        }
                    } catch(Exception ex) { if(!IsFileError(ex))throw; }
                }
            }
            private bool Skip(string folder) {
                if(excluded.Contains(folder.TrimEnd(Path.DirectorySeparatorChar)))return true;
                string name=Path.GetFileName(folder.TrimEnd(Path.DirectorySeparatorChar));
                foreach(string blocked in new[]{"Windows","$Recycle.Bin","System Volume Information","Recovery","$Windows.~BT","$Windows.~WS","Config.Msi","MSOCache",".git",".svn","node_modules","ChainedBeasts_Data"})
                    if(string.Equals(name,blocked,StringComparison.OrdinalIgnoreCase))return true;
                return false;
            }
        }

        private static bool IsFileError(Exception ex){return ex is IOException || ex is UnauthorizedAccessException || ex is System.Security.SecurityException || ex is ArgumentException || ex is NotSupportedException;}
        private static bool IsRegistryError(Exception ex){return IsFileError(ex) || ex is System.Security.SecurityException;}
        private static bool IsReparse(string path){try{return (File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0;}catch(Exception ex) { if(!IsFileError(ex))throw;return true;}}
        private static string LocalPath(string path) {
            if(string.IsNullOrWhiteSpace(path))return null;
            try {
                string full=Path.GetFullPath(Environment.ExpandEnvironmentVariables(path.Trim().Trim('"')));
                return full.StartsWith(@"\\",StringComparison.Ordinal)?null:full.TrimEnd(Path.DirectorySeparatorChar)+ (Path.GetPathRoot(full)==full?Path.DirectorySeparatorChar.ToString():"");
            } catch(Exception ex) { if(!IsFileError(ex))throw;return null;}
        }
        private static string ChildPath(string parent,string child) {
            if(string.IsNullOrWhiteSpace(child) || Path.IsPathRooted(child))return null;
            string full=LocalPath(Path.Combine(parent,child)),basePath=LocalPath(parent);
            return full!=null && basePath!=null && full.StartsWith(basePath+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)?full:null;
        }
        private static bool IsArchive(string path) {
            string ext=Path.GetExtension(path);
            foreach(string archive in new[]{".zip",".rar",".7z",".tar",".gz",".bz2",".xz",".iso"})if(string.Equals(ext,archive,StringComparison.OrdinalIgnoreCase))return true;
            return false;
        }
        private static bool IsGameName(string text) {
            if(text==null)return false;var normalized=new StringBuilder();foreach(char ch in text)if(char.IsLetterOrDigit(ch))normalized.Append(char.ToLowerInvariant(ch));
            return normalized.ToString().Contains("chainedbeasts");
        }
        private static bool NumericId(string text){if(string.IsNullOrEmpty(text) || text.Length>12)return false;foreach(char ch in text)if(ch<'0'||ch>'9')return false;return text.TrimStart('0').Length>0;}
        private static string IconPath(string text) {
            if(string.IsNullOrWhiteSpace(text))return null;text=text.Trim();
            if(text.StartsWith("\"",StringComparison.Ordinal)){int end=text.IndexOf('"',1);return end>1?LocalPath(text.Substring(1,end-1)):null;}
            int comma=text.LastIndexOf(',');int index;if(comma>=0 && int.TryParse(text.Substring(comma+1),out index))text=text.Substring(0,comma);
            return LocalPath(text);
        }
        private static bool IsX64Pe(string path,bool dll) {
            try {
                using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete))
                using(var reader=new BinaryReader(stream)) {
                    if(stream.Length<88 || reader.ReadUInt16()!=0x5A4D)return false;
                    stream.Position=0x3c;int pe=reader.ReadInt32();if(pe<64 || pe>stream.Length-26)return false;
                    stream.Position=pe;if(reader.ReadUInt32()!=0x4550 || reader.ReadUInt16()!=0x8664)return false;
                    ushort sections=reader.ReadUInt16();if(sections==0 || sections>96)return false;
                    stream.Position=pe+20;ushort optionalSize=reader.ReadUInt16();
                    if(optionalSize<112 || (long)pe+24+optionalSize+(long)sections*40>stream.Length)return false;
                    ushort characteristics=reader.ReadUInt16();
                    return (characteristics&0x0002)!=0 && ((characteristics&0x2000)!=0)==dll && reader.ReadUInt16()==0x20b;
                }
            } catch(Exception ex) { if(!IsFileError(ex))throw;return false;}
        }

        // Small Valve KeyValues reader: quoted/unquoted tokens, comments, escaped quotes/backslashes, old/new libraries.
        internal sealed class VdfEntry { public string Key, Text;public List<VdfEntry> Children; }
        internal static List<VdfEntry> ParseVdf(string text) {
            if(text==null || text.Length>1024*1024)throw new FormatException("VDF is too large.");
            var parser=new VdfParser(text);return parser.Read(false,0);
        }
        private sealed class VdfParser {
            private readonly string text;private int index,tokens;
            public VdfParser(string text){this.text=text;}
            public List<VdfEntry> Read(bool nested,int depth) {
                if(depth>32)throw new FormatException("VDF nesting.");var result=new List<VdfEntry>();string key;
                while((key=Token())!=null) {
                    if(key=="}"){if(!nested)throw new FormatException("Unexpected brace.");return result;}
                    if(key=="{")throw new FormatException("Missing key.");string value=Token();if(value==null || value=="}")throw new FormatException("Missing value.");
                    result.Add(value=="{"?new VdfEntry{Key=key,Children=Read(true,depth+1)}:new VdfEntry{Key=key,Text=value});
                }
                if(nested)throw new FormatException("Unclosed block.");return result;
            }
            private string Token() {
                for(;;) {
                    while(index<text.Length && (char.IsWhiteSpace(text[index]) || text[index]=='\uFEFF'))index++;
                    if(index+1<text.Length && text[index]=='/' && text[index+1]=='/'){while(index<text.Length && text[index]!='\n')index++;continue;}break;
                }
                if(index>=text.Length)return null;if(++tokens>100000)throw new FormatException("VDF token budget.");
                char first=text[index++];if(first=='{' || first=='}')return first.ToString();var value=new StringBuilder();
                if(first=='"') {
                    while(index<text.Length) {
                        char ch=text[index++];if(ch=='"')return value.ToString();
                        if(ch=='\\' && index<text.Length) {
                            char next=text[index];if(next=='\\'||next=='"'){value.Append(next);index++;continue;}
                            // Unknown escapes retain their slash so ordinary Windows paths are not damaged.
                        }
                        value.Append(ch);
                    }
                    throw new FormatException("Unclosed string.");
                }
                value.Append(first);while(index<text.Length && !char.IsWhiteSpace(text[index]) && text[index]!='{' && text[index]!='}')value.Append(text[index++]);return value.ToString();
            }
        }
        private static VdfEntry Find(List<VdfEntry> entries,string key){return entries.Find(delegate(VdfEntry item){return string.Equals(item.Key,key,StringComparison.OrdinalIgnoreCase);});}
        private static string Value(List<VdfEntry> entries,string key){VdfEntry item=Find(entries,key);return item==null?null:item.Text;}
        private static void CollectLibraries(List<VdfEntry> entries,List<string> paths) {
            VdfEntry container=Find(entries,"libraryfolders");
            if(container==null || container.Children==null)return;
            foreach(VdfEntry item in container.Children) {
                int number;
                if(!int.TryParse(item.Key,NumberStyles.None,CultureInfo.InvariantCulture,out number))continue;
                string path=item.Children==null?item.Text:Value(item.Children,"path");
                if(path!=null)paths.Add(path);
            }
        }
    }
}
