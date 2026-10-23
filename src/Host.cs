using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using System.Xml.Linq;

namespace ChainedGuard {
    internal static class Program {
        [STAThread] public static int Main(string[] args) {
            try {
                if(Environment.OSVersion.Platform!=PlatformID.Win32NT || !Environment.Is64BitProcess) return 2;
                Native.SetProcessDPIAware();
                if (Array.IndexOf(args, "--self-test") >= 0) return Tests.Run();
                StartupOptions options=StartupOptions.Parse(args);
                bool acquired; using (var mutex = new Mutex(true, "Local\\ChainedGuard.Host", out acquired)) {
                    if (!acquired) { Native.GrantExistingHostForeground();PanelRequest.Notify(PanelRequest.EventName);return 0; }
                    using(var panelRequest=new EventWaitHandle(false,EventResetMode.AutoReset,PanelRequest.EventName)) {
                        var app = new Application(); app.ShutdownMode = ShutdownMode.OnMainWindowClose;
                        return app.Run(new GuardWindow(options,panelRequest));
                    }
                }
            } catch (Exception ex) {
                try { File.WriteAllText(HostStorage.ErrorFile, DateTime.Now + "\r\n" + ex); } catch { }
                return 1;
            }
        }
    }

    internal static class PanelRequest {
        public const string EventName="Local\\ChainedGuard.ShowPanel";
        public static bool Notify(string name,int attempts=10,int retryMilliseconds=30) {
            for(int i=0;i<attempts;i++) {
                try{using(var signal=EventWaitHandle.OpenExisting(name))return signal.Set();}
                catch(WaitHandleCannotBeOpenedException){if(i+1<attempts && retryMilliseconds>0)Thread.Sleep(retryMilliseconds);}
                catch(UnauthorizedAccessException){return false;}
            }
            return false;
        }
        public static RegisteredWaitHandle Listen(EventWaitHandle signal,Action callback) { return ThreadPool.RegisterWaitForSingleObject(signal,delegate(object state,bool timeout){if(!timeout)callback();},null,Timeout.Infinite,false); }
    }

    internal static class Branding {
        public const string Name="Dodger";
        public const string Version="19";
        public static ImageSource WindowIcon() {
            using(var stream=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("Dodger.Icon")) {
                if(stream==null)throw new InvalidOperationException("Embedded Dodger icon is missing.");
                var decoder=BitmapDecoder.Create(stream,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.OnLoad);
                var frame=decoder.Frames[decoder.Frames.Count-1];frame.Freeze();return frame;
            }
        }
        public static System.Drawing.Icon TrayIcon() {
            using(var stream=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("Dodger.Icon")) {
                if(stream==null)throw new InvalidOperationException("Embedded Dodger icon is missing.");
                using(var icon=new System.Drawing.Icon(stream,32,32))return (System.Drawing.Icon)icon.Clone();
            }
        }
    }

    internal sealed class StartupOptions {
        public bool Preview,Block,Dodge,Minimized,BlockInvulnerable,PriorityDefense;
        public static StartupOptions Parse(string[] args) {
            bool preview=Array.IndexOf(args,"--preview")>=0;
            return new StartupOptions { Preview=preview,Block=!preview && Array.IndexOf(args,"--block")>=0,Dodge=!preview && Array.IndexOf(args,"--dodge")>=0,Minimized=!preview && Array.IndexOf(args,"--minimized")>=0,BlockInvulnerable=!preview && Array.IndexOf(args,"--block-invulnerable")>=0,PriorityDefense=!preview && Array.IndexOf(args,"--priority-defense")>=0 };
        }
    }

    internal sealed class HostLog : IDisposable {
        private readonly object gate=new object();
        private StreamWriter writer;
        private readonly string session=DateTime.UtcNow.ToString("yyyyMMddTHHmmssfff",CultureInfo.InvariantCulture)+"-"+Guid.NewGuid().ToString("N").Substring(0,8);
        private readonly int processId=Process.GetCurrentProcess().Id;
        public string Folder { get; private set; }
        public string FilePath { get; private set; }
        public string Error { get; private set; }
        public HostLog(string folder,string filename=null) {
            Folder=Path.GetFullPath(folder);FilePath=Path.Combine(Folder,filename??("host-"+DateTime.Now.ToString("yyyyMMdd",CultureInfo.InvariantCulture)+".jsonl"));
            try {
                Directory.CreateDirectory(Folder);
                writer=new StreamWriter(new FileStream(FilePath,FileMode.Append,FileAccess.Write,FileShare.ReadWrite),new UTF8Encoding(false),4096){AutoFlush=true};
            }catch(Exception ex){Error=ex.Message;}
        }
        public void Write(string name,params object[] fields) {
            lock(gate) {
                if(writer==null)return;
                try {
                    var line=new StringBuilder();line.Append("{\"utc\":").Append(Json(DateTime.UtcNow.ToString("o",CultureInfo.InvariantCulture))).Append(",\"tick_ms\":").Append(unchecked((uint)Environment.TickCount).ToString(CultureInfo.InvariantCulture)).Append(",\"session\":").Append(Json(session)).Append(",\"host_pid\":").Append(processId.ToString(CultureInfo.InvariantCulture)).Append(",\"event\":").Append(Json(name));
                    for(int i=0;i+1<fields.Length;i+=2)line.Append(',').Append(Json(Convert.ToString(fields[i],CultureInfo.InvariantCulture))).Append(':').Append(Json(fields[i+1]));
                    line.Append('}');writer.WriteLine(line.ToString());
                }catch(Exception ex){Error=ex.Message;}
            }
        }
        internal static string Json(object value) {
            if(value==null)return "null";
            if(value is bool)return (bool)value?"true":"false";
            if(value is float || value is double) { double n=Convert.ToDouble(value,CultureInfo.InvariantCulture);return double.IsNaN(n)||double.IsInfinity(n)?"null":n.ToString("R",CultureInfo.InvariantCulture); }
            if(value is byte || value is sbyte || value is short || value is ushort || value is int || value is uint || value is long || value is ulong || value is decimal)return Convert.ToString(value,CultureInfo.InvariantCulture);
            string text=Convert.ToString(value,CultureInfo.InvariantCulture);var json=new StringBuilder("\"");
            foreach(char ch in text) {
                switch(ch) {
                    case '"':json.Append("\\\"");break;case '\\':json.Append("\\\\");break;
                    case '\b':json.Append("\\b");break;case '\f':json.Append("\\f");break;case '\n':json.Append("\\n");break;case '\r':json.Append("\\r");break;case '\t':json.Append("\\t");break;
                    default:if(ch<0x20)json.Append("\\u").Append(((int)ch).ToString("x4",CultureInfo.InvariantCulture));else json.Append(ch);break;
                }
            }
            return json.Append('"').ToString();
        }
        public void Dispose() { lock(gate){if(writer!=null){try{writer.Dispose();}catch(Exception ex){Error=ex.Message;}writer=null;}} }
    }

    internal sealed class Settings {
        public double Lead = 90, Range = 5;
        public bool Overlay = true;
        public bool BlockInvulnerable;
        public bool InterruptChargedAttack=true;
        public int Mode = 0;
        public string GameFolder;
        public string Language=I18n.DefaultLanguage;
        public static string FilePath { get { return HostStorage.SettingsPath; } }
        public static Settings Load() {
            try{return FromXml(XElement.Load(FilePath));}catch{
                try{return FromXml(XElement.Load(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"settings.xml")));}catch{return new Settings();}
            }
        }
        internal static Settings FromXml(XElement x) { var s=new Settings();bool b;if(bool.TryParse((string)x.Element("Overlay"),out b))s.Overlay=b;if(bool.TryParse((string)x.Element("BlockInvulnerable"),out b))s.BlockInvulnerable=b;if(bool.TryParse((string)x.Element("InterruptChargedAttack"),out b))s.InterruptChargedAttack=b;s.GameFolder=(string)x.Element("GameFolder");s.Language=x.Element("Language")==null?"ru":I18n.Normalize((string)x.Element("Language"));return s; }
        internal XElement ToXml() { return new XElement("ChainedGuard",new XElement("Overlay",Overlay),new XElement("BlockInvulnerable",BlockInvulnerable),new XElement("InterruptChargedAttack",InterruptChargedAttack),new XElement("GameFolder",GameFolder??""),new XElement("Language",I18n.Normalize(Language))); }
        public void Save() {
            ToXml().Save(FilePath);
        }
        public static double Clamp(double n, double lo, double hi) { return double.IsNaN(n) || double.IsInfinity(n) ? lo : Math.Max(lo, Math.Min(hi, n)); }
    }

    internal static class GameSelection {
        internal sealed class Candidate {
            public int Pid;public long Started;public bool HasWindow,EngineLoaded,Foreground;
        }
        public static int Pick(IList<Candidate> candidates) {
            Candidate best=null;
            foreach(var candidate in candidates) {
                if(!candidate.HasWindow || !candidate.EngineLoaded)continue;
                if(best==null || (candidate.Foreground&&!best.Foreground) || (candidate.Foreground==best.Foreground && candidate.Started>best.Started))best=candidate;
            }
            return best==null?0:best.Pid;
        }
    }

    internal sealed class Channel : IDisposable {
        public const uint Magic = 0x43424731;
        public MemoryMappedFile Mapping;
        public MemoryMappedViewAccessor View;
        public int Pid;
        public uint MarkSeq;
        public Channel(int pid,uint initialMarkSeq=0) {
            Pid = pid;MarkSeq=initialMarkSeq; Mapping = MemoryMappedFile.CreateOrOpen("Local\\ChainedGuard_" + pid, 4096, MemoryMappedFileAccess.ReadWrite);
            View = Mapping.CreateViewAccessor(0,4096,MemoryMappedFileAccess.ReadWrite);
            View.Write(0, Magic); View.Write(4, (uint)1);
        }
        public void Write(bool block, bool dodge, bool overlay, bool paused, Settings s,bool priority=false,bool blockInvulnerable=false) {
            uint seq = View.ReadUInt32(8); seq = (seq + 2) & ~1u;
            View.Write(8,seq | 1u);
            View.Write(12, Flags(block,dodge,overlay,paused,priority,blockInvulnerable,s.InterruptChargedAttack));
            View.Write(16,(float)s.Lead); View.Write(20,(float)s.Range); View.Write(24,s.Mode); View.Write(28,unchecked((uint)Environment.TickCount));View.Write(100,MarkSeq);
            Thread.MemoryBarrier(); View.Write(8,seq);
        }
        public static uint Flags(bool block,bool dodge,bool overlay,bool paused,bool priority=false,bool blockInvulnerable=false,bool interruptCharged=false) { return (block?1u:0) | (dodge?2u:0) | (overlay?4u:0) | (paused?8u:0) | (priority?16u:0) | (block&&blockInvulnerable?32u:0) | (interruptCharged?64u:0); }
        public Snapshot Read() { return ReadSnapshot(true); }
        internal Snapshot ReadHud() { return ReadSnapshot(false); }
        private Snapshot ReadSnapshot(bool includeText) {
            for (int i=0;i<3;i++) {
                uint seq = View.ReadUInt32(32); if ((seq & 1) != 0) continue;
                var s = new Snapshot(); s.State=View.ReadUInt32(36); s.Frame=View.ReadUInt32(40); s.Threats=View.ReadUInt32(44); s.Blocks=View.ReadUInt32(48); s.Dodges=View.ReadUInt32(52);
                s.X=View.ReadSingle(56); s.Y=View.ReadSingle(60); s.Visible=View.ReadInt32(64)!=0; s.AttackMs=View.ReadSingle(68); s.LastAction=View.ReadUInt32(72); s.Attached=View.ReadUInt32(80);s.NativeTick=View.ReadUInt32(84);s.ConfirmedBlockTick=View.ReadUInt32(88);s.ActualDamage=View.ReadUInt32(92);s.ActualDamageTick=View.ReadUInt32(96);
                s.ConfirmedProjectileDodges=View.ReadUInt32(104);s.DroppedLogRecords=View.ReadUInt32(108);s.TraceReady=View.ReadUInt32(112);s.NativeBuild=View.ReadUInt32(116);
                if(s.NativeBuild>=5){s.DashSeq=View.ReadUInt32(120);s.ConfirmedProjectileDodgeSeq=View.ReadUInt32(124);s.HudVersion=View.ReadUInt32(1536);s.ShieldFlags=View.ReadUInt32(1540);s.ShieldType=View.ReadInt32(1544);s.ShieldCurrent=View.ReadInt32(1548);s.ShieldMax=View.ReadInt32(1552);s.DashTick=View.ReadUInt32(1556);s.ConfirmedProjectileDodgeTick=View.ReadUInt32(1560);s.ShieldMaxSource=View.ReadUInt32(1564);}
                if(s.NativeBuild>=6)s.NativeRevision=View.ReadUInt32(1568);
                if(s.NativeBuild==6&&s.NativeRevision>=8)s.HudDangerFlags=View.ReadUInt32(1572);
                if(s.NativeBuild==6&&s.NativeRevision>=16&&s.HudVersion>=2){s.ConfirmedBlockSeq=View.ReadUInt32(1580);s.ShieldBreakSeq=View.ReadUInt32(1584);s.ShieldIdentity=View.ReadUInt64(1592);s.ShieldBreakIdentity=View.ReadUInt64(1600);}
                if(includeText){s.Status=ReadText(128,256);s.Info=ReadText(512,512);}Thread.MemoryBarrier();
                if (seq == View.ReadUInt32(32)) return s;
            }
            return null;
        }
        private string ReadText(int offset,int count) { byte[] data=new byte[count]; View.ReadArray(offset,data,0,count); int n=Array.IndexOf(data,(byte)0); return Encoding.UTF8.GetString(data,0,n<0?count:n); }
        public void Dispose() { if (View!=null) View.Dispose(); if (Mapping!=null) Mapping.Dispose(); }
    }
    internal sealed class Snapshot {
        public uint State,Frame,Threats,Blocks,Dodges,LastAction,Attached,NativeTick,ConfirmedBlockTick,ActualDamage,ActualDamageTick;
        public uint ConfirmedProjectileDodges,DroppedLogRecords,TraceReady,NativeBuild,NativeRevision,HudDangerFlags;
        public uint DashSeq,ConfirmedProjectileDodgeSeq,HudVersion,ShieldFlags,DashTick,ConfirmedProjectileDodgeTick,ShieldMaxSource;
        public uint ConfirmedBlockSeq,ShieldBreakSeq;
        public ulong ShieldIdentity,ShieldBreakIdentity;
        public int ShieldType,ShieldCurrent,ShieldMax;
        public float X,Y,AttackMs;
        public bool Visible;
        public string Status,Info;
    }

    internal static class Native {
        public const uint VMOperation=0x8,VMRead=0x10,VMWrite=0x20,CreateThread=0x2,Query=0x400;
        [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
        [DllImport("user32.dll",SetLastError=true)] public static extern bool AllowSetForegroundWindow(uint processId);
        [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr hwnd,int index);
        [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr hwnd,int index,int value);
        [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd,IntPtr after,int x,int y,int width,int height,uint flags);
        [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr OpenProcess(uint rights,bool inherit,int pid);
        [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr VirtualAllocEx(IntPtr process,IntPtr address,UIntPtr size,uint type,uint protect);
        [DllImport("kernel32.dll",SetLastError=true)] public static extern bool VirtualFreeEx(IntPtr process,IntPtr address,UIntPtr size,uint type);
        [DllImport("kernel32.dll",SetLastError=true)] public static extern bool WriteProcessMemory(IntPtr process,IntPtr address,byte[] data,UIntPtr size,out UIntPtr written);
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetModuleHandle(string name);
        [DllImport("kernel32.dll",CharSet=CharSet.Ansi,ExactSpelling=true)] public static extern IntPtr GetProcAddress(IntPtr module,string name);
        [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr CreateRemoteThread(IntPtr process,IntPtr attrs,UIntPtr stack,IntPtr start,IntPtr parameter,uint flags,out uint id);
        [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr handle,uint timeout);
        [DllImport("kernel32.dll")] public static extern bool GetExitCodeThread(IntPtr thread,out uint code);
        [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
        public delegate IntPtr HookProc(int code,IntPtr wp,IntPtr lp);
        [DllImport("user32.dll",SetLastError=true)] public static extern IntPtr SetWindowsHookEx(int id,HookProc proc,IntPtr module,uint thread);
        [DllImport("user32.dll")] public static extern bool UnhookWindowsHookEx(IntPtr hook);
        [DllImport("user32.dll")] public static extern IntPtr CallNextHookEx(IntPtr hook,int code,IntPtr wp,IntPtr lp);
        public static bool ForegroundIs(int pid) { uint p; GetWindowThreadProcessId(GetForegroundWindow(),out p); return p==(uint)pid; }
        public static void GrantExistingHostForeground() {
            try {
                using(var current=Process.GetCurrentProcess()) {
                    string path=current.MainModule.FileName;int ownPid=current.Id;
                    var candidates=new List<Process>();var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase){current.ProcessName,"Dodger","ChainedGuard"};
                    foreach(string name in names)candidates.AddRange(Process.GetProcessesByName(name));
                    try {
                        uint compatiblePid=0;
                        for(int i=0;i<candidates.Count && i<16;i++) {
                            try {
                                if(candidates[i].Id==ownPid)continue;
                                string candidatePath=candidates[i].MainModule.FileName;
                                if(string.Equals(candidatePath,path,StringComparison.OrdinalIgnoreCase)){AllowSetForegroundWindow((uint)candidates[i].Id);return;}
                                if(CompatibleHostPath(candidatePath,path))compatiblePid=(uint)candidates[i].Id;
                            }catch{}
                        }
                        if(compatiblePid!=0)AllowSetForegroundWindow(compatiblePid);
                    }finally{foreach(var candidate in candidates)candidate.Dispose();}
                }
            }catch{}
        }
        internal static bool CompatibleHostPath(string candidate,string current) {
            if(string.Equals(candidate,current,StringComparison.OrdinalIgnoreCase))return true;
            string c=Path.GetFileNameWithoutExtension(candidate),r=Path.GetFileNameWithoutExtension(current);
            bool knownCandidate=string.Equals(c,"Dodger",StringComparison.OrdinalIgnoreCase)||string.Equals(c,"ChainedGuard",StringComparison.OrdinalIgnoreCase);
            bool knownCurrent=string.Equals(r,"Dodger",StringComparison.OrdinalIgnoreCase)||string.Equals(r,"ChainedGuard",StringComparison.OrdinalIgnoreCase);
            return knownCandidate&&knownCurrent&&string.Equals(Path.GetDirectoryName(candidate),Path.GetDirectoryName(current),StringComparison.OrdinalIgnoreCase);
        }
        public static string Hash(string path) { using(var sha=SHA256.Create()) using(var f=File.OpenRead(path)) { return BitConverter.ToString(sha.ComputeHash(f)).Replace("-",""); } }
        public static bool Inject(int pid,string path,out string reason) {
            reason=""; IntPtr proc=IntPtr.Zero,buffer=IntPtr.Zero,thread=IntPtr.Zero;
            try {
                proc=OpenProcess(VMOperation|VMRead|VMWrite|CreateThread|Query,false,pid); if(proc==IntPtr.Zero) throw new Exception(I18n.Text("Нет доступа к процессу. Запустите панель с теми же правами, что и игру.","Access denied. Run Dodger with the same permissions as the game."));
                byte[] bytes=Encoding.Unicode.GetBytes(Path.GetFullPath(path)+"\0");
                buffer=VirtualAllocEx(proc,IntPtr.Zero,(UIntPtr)bytes.Length,0x3000,0x04); if(buffer==IntPtr.Zero) throw new Exception(I18n.Text("Не удалось выделить память для подключения.","Could not allocate memory for the connection."));
                UIntPtr written; if(!WriteProcessMemory(proc,buffer,bytes,(UIntPtr)bytes.Length,out written) || written.ToUInt64()!=(ulong)bytes.Length) throw new Exception(I18n.Text("Не удалось передать путь модуля.","Could not transfer the module path."));
                IntPtr localKernel=GetModuleHandle("kernel32.dll");
                IntPtr load=GetProcAddress(localKernel,"LoadLibraryW"); if(load==IntPtr.Zero) throw new Exception(I18n.Text("LoadLibraryW недоступен.","LoadLibraryW is unavailable."));
                string owner="kernel32.dll";IntPtr ownerBase=localKernel;
                using(var current=Process.GetCurrentProcess()) foreach(ProcessModule module in current.Modules) { long first=module.BaseAddress.ToInt64();if(load.ToInt64()>=first && load.ToInt64()<first+module.ModuleMemorySize){owner=module.ModuleName;ownerBase=module.BaseAddress;break;} }
                IntPtr remoteOwner=IntPtr.Zero;
                using(var target=Process.GetProcessById(pid)) foreach(ProcessModule module in target.Modules) if(string.Equals(module.ModuleName,owner,StringComparison.OrdinalIgnoreCase)){remoteOwner=module.BaseAddress;break;}
                if(remoteOwner==IntPtr.Zero)throw new Exception(I18n.Text("Не найдена системная функция загрузки в игровом процессе.","The system loader was not found in the game process."));
                load=new IntPtr(remoteOwner.ToInt64()+(load.ToInt64()-ownerBase.ToInt64()));
                uint tid; thread=CreateRemoteThread(proc,IntPtr.Zero,UIntPtr.Zero,load,buffer,0,out tid); if(thread==IntPtr.Zero) throw new Exception(I18n.Text("Не удалось загрузить модуль в игру.","Could not load the module into the game."));
                uint wait=WaitForSingleObject(thread,5000); if(wait!=0) { buffer=IntPtr.Zero; throw new Exception(I18n.Text("Игра ещё загружает модуль. Ожидайте сигнала подключения.","The game is still loading the module. Waiting for a connection signal.")); }
                uint code; if(!GetExitCodeThread(thread,out code) || code==0) throw new Exception(I18n.Text("Игра не смогла загрузить ChainedGuard.Native.dll.","The game could not load ChainedGuard.Native.dll."));
                return true;
            } catch(Exception ex) { reason=ex.Message; return false; }
            finally { if(thread!=IntPtr.Zero) CloseHandle(thread); if(buffer!=IntPtr.Zero && proc!=IntPtr.Zero) VirtualFreeEx(proc,buffer,UIntPtr.Zero,0x8000); if(proc!=IntPtr.Zero) CloseHandle(proc); }
        }
    }

    internal sealed class ThreatWindow : IDisposable {
        public sealed class HudFrame {
            public int GamePid;
            public uint Frame,Threats,DashSeq,ConfirmedProjectileDodgeSeq,BlockSeq,ActualDamageTick,HudVersion,ConfirmedBlockSeq,ShieldBreakSeq;
            public ulong ShieldIdentity,ShieldBreakIdentity;
            public float X,Y,EtaMs=-1;
            public bool DodgeUnavailable,Allowed,Visible,Suppressed,HasShield,ShieldCurrentValid,ShieldMaxKnown,ShieldNoWear;
            public int ShieldType,ShieldCurrent,ShieldMax,HitsRemaining=-1;
        }
        internal const double DashDuration=600,ContactDuration=680,WaveDuration=780,BlockDuration=520,BlockRise=100,BreakDuration=500,DurabilityDuration=320;
        internal const float CanvasCenter=60,CanvasSize=120;
        internal enum WaveKind {Dash,Block}
        internal struct Wave {internal bool Active,Confirmed;internal double Started;internal WaveKind Kind;}
        internal struct Pulse { internal uint Generation;internal double Started;internal bool Active; }
        internal sealed class Playback {
            internal HudFrame Frame;
            internal Pulse Green,Dash,Contact,Warning,Break;
            internal readonly Wave[] Waves=new Wave[12];
            private int nextWave;private bool wasThreat,wasCritical;
            internal uint PendingBlock;
            internal bool BrokenCore,BlockOnCurrentShield;
            internal double BreakFraction;
            internal ulong BreakIdentity;
            internal System.Drawing.Color BlockFromColor;
            private System.Drawing.Color lastThreatColor;
            private bool initialized;
            private int pid;
            private uint dash,contact,confirmedBlock,shieldBreak,hudVersion,generation;
            private bool shieldPresent;
            private ulong shieldIdentity;
            private int shieldType,shieldMax;
            private double durabilityFrom=double.NaN,durabilityTo=double.NaN,durabilityStarted;
            private HudFrame lastValidFrame;
            private double lastValidAt=double.NegativeInfinity;
            internal void Clear(){Green.Active=Dash.Active=Contact.Active=Warning.Active=Break.Active=false;PendingBlock=0;for(int i=0;i<Waves.Length;i++)Waves[i].Active=false;nextWave=0;wasThreat=wasCritical=BrokenCore=BlockOnCurrentShield=shieldPresent=false;BreakFraction=0;BreakIdentity=shieldIdentity=0;shieldType=shieldMax=0;durabilityFrom=durabilityTo=double.NaN;BlockFromColor=lastThreatColor=System.Drawing.Color.Empty;}
            internal void AddWave(double now,bool confirmed){AddWave(now,confirmed,WaveKind.Dash);}
            private void AddWave(double now,bool confirmed,WaveKind kind){Waves[nextWave]=new Wave{Active=true,Started=now,Confirmed=confirmed,Kind=kind};nextWave=(nextWave+1)%Waves.Length;}
            internal void AddBlockWave(double now){AddWave(now,false,WaveKind.Block);}
            internal int ActiveWaves {get{int count=0;foreach(var wave in Waves)if(wave.Active)count++;return count;}}
            internal int ActiveBlockWaves {get{int count=0;foreach(var wave in Waves)if(wave.Active&&wave.Kind==WaveKind.Block)count++;return count;}}
            internal bool ShowBlockGlyph {get{return Green.Active&&(!Dash.Active||Green.Started>=Dash.Started)&&(!Contact.Active||Green.Started>=Contact.Started);}}
            internal bool ShowDashGlyph {get{return Dash.Active&&(!Contact.Active||Dash.Started>Contact.Started)&&(!Green.Active||Dash.Started>Green.Started);}}
            internal double DisplayFraction(double now){if(double.IsNaN(durabilityFrom)||double.IsNaN(durabilityTo))return double.NaN;if(durabilityFrom==durabilityTo)return durabilityTo;double t=EaseOut((now-durabilityStarted)/DurabilityDuration);return durabilityFrom+(durabilityTo-durabilityFrom)*t;}
            internal bool DurabilityAnimating(double now){return !double.IsNaN(durabilityFrom)&&!double.IsNaN(durabilityTo)&&durabilityFrom!=durabilityTo&&now-durabilityStarted<DurabilityDuration;}
            internal System.Drawing.Color ShieldColor(double now,double fraction){
                var durability=double.IsNaN(fraction)?System.Drawing.Color.FromArgb(240,193,201,184):DurabilityColor(fraction);if(!Green.Active||(shieldPresent&&!BlockOnCurrentShield))return durability;
                double age=Math.Max(0,now-Green.Started);var green=System.Drawing.Color.FromArgb(240,156,247,112);
                return age<BlockRise?Mix(BlockFromColor,green,EaseOut(age/BlockRise)):Mix(green,durability,EaseOut((age-BlockRise)/(BlockDuration-BlockRise)));
            }
            private void Durability(HudFrame frame,double now,bool baseline){
                bool present=frame.HasShield&&frame.ShieldCurrentValid;double target=Fraction(frame);
                bool replace=baseline||!present||!shieldPresent||frame.ShieldIdentity!=shieldIdentity||frame.ShieldType!=shieldType||frame.ShieldMax!=shieldMax||double.IsNaN(target)||double.IsNaN(durabilityTo);
                if(replace){durabilityFrom=durabilityTo=target;durabilityStarted=now;BlockOnCurrentShield=false;}else if(target!=durabilityTo){durabilityFrom=DisplayFraction(now);durabilityTo=target;durabilityStarted=now;}
                shieldPresent=present;shieldIdentity=frame.ShieldIdentity;shieldType=frame.ShieldType;shieldMax=frame.ShieldMax;if(present)BrokenCore=false;
            }
            // Modular sequence arithmetic accepts uint wrap, but a reset is a new baseline.
            internal static bool CounterReset(uint current,uint previous){return unchecked(current-previous)>0x7fffffffu;}
            private void ConfirmWave(double now){
                int latest=-1;double time=double.NegativeInfinity;for(int i=0;i<Waves.Length;i++)if(Waves[i].Active&&Waves[i].Kind==WaveKind.Dash&&now-Waves[i].Started<=450&&Waves[i].Started>time){latest=i;time=Waves[i].Started;}
                if(latest>=0)Waves[latest].Confirmed=true;else AddWave(now,true);
            }
            private void WarningEvent(HudFrame frame,double now){bool threat=frame.Threats>0,critical=threat&&(Urgent(frame)||frame.DodgeUnavailable);if((threat&&!wasThreat)||(critical&&!wasCritical))Start(ref Warning,now);if(threat)lastThreatColor=frame.DodgeUnavailable?System.Drawing.Color.FromArgb(255,255,56,52):ThreatColor(frame.EtaMs);wasThreat=threat;wasCritical=critical;}
            internal void Start(ref Pulse pulse,double now){pulse=new Pulse{Active=true,Started=now,Generation=++generation};}
            internal static void Expire(ref Pulse pulse,uint expectedGeneration,double now,double duration){if(pulse.Active&&pulse.Generation==expectedGeneration&&now-pulse.Started>=duration)pulse.Active=false;}
            private void ExpirePulses(double now){Expire(ref Green,Green.Generation,now,BlockDuration);Expire(ref Break,Break.Generation,now,BreakDuration);Expire(ref Dash,Dash.Generation,now,DashDuration);Expire(ref Contact,Contact.Generation,now,ContactDuration);Expire(ref Warning,Warning.Generation,now,340);for(int i=0;i<Waves.Length;i++)if(Waves[i].Active&&now-Waves[i].Started>=WaveDuration)Waves[i].Active=false;}
            internal void Advance(HudFrame frame,double now) {
                if(frame==null){ExpirePulses(now);Frame=lastValidFrame!=null&&now-lastValidAt<=50?lastValidFrame:null;if(Frame==null){Clear();initialized=false;lastValidFrame=null;lastValidAt=double.NegativeInfinity;}return;}
                Frame=frame;
                if(frame.Suppressed||!frame.Allowed||!frame.Visible||!ValidAnchor(frame.X,frame.Y)){Clear();initialized=false;lastValidFrame=null;lastValidAt=double.NegativeInfinity;return;}
                lastValidFrame=frame;lastValidAt=now;
                if(!initialized||pid!=frame.GamePid||hudVersion!=frame.HudVersion||CounterReset(frame.DashSeq,dash)||CounterReset(frame.ConfirmedProjectileDodgeSeq,contact)||(frame.HudVersion>=2&&(CounterReset(frame.ConfirmedBlockSeq,confirmedBlock)||CounterReset(frame.ShieldBreakSeq,shieldBreak)))){initialized=true;pid=frame.GamePid;hudVersion=frame.HudVersion;dash=frame.DashSeq;contact=frame.ConfirmedProjectileDodgeSeq;confirmedBlock=frame.ConfirmedBlockSeq;shieldBreak=frame.ShieldBreakSeq;Clear();Durability(frame,now,true);WarningEvent(frame,now);return;}
                bool newBlock=frame.HudVersion>=2&&frame.ConfirmedBlockSeq!=confirmedBlock,newBreak=frame.HudVersion>=2&&frame.ShieldBreakSeq!=shieldBreak;
                bool newDash=frame.DashSeq!=dash,newContact=frame.ConfirmedProjectileDodgeSeq!=contact;
                double oldFraction=DisplayFraction(now);int oldMax=shieldMax;bool matchingBreak=frame.ShieldBreakIdentity!=0&&((shieldPresent&&shieldIdentity==frame.ShieldBreakIdentity)||(frame.HasShield&&frame.ShieldIdentity==frame.ShieldBreakIdentity));
                var blockFrom=newBlock?(Green.Active?ShieldColor(now,oldFraction):(wasThreat?lastThreatColor:ShieldColor(now,oldFraction))):System.Drawing.Color.Empty;uint blockDelta=unchecked(frame.ConfirmedBlockSeq-confirmedBlock);bool sameShield=shieldPresent&&frame.HasShield&&frame.ShieldCurrentValid&&shieldIdentity==frame.ShieldIdentity&&shieldType==frame.ShieldType;
                dash=frame.DashSeq;contact=frame.ConfirmedProjectileDodgeSeq;confirmedBlock=frame.ConfirmedBlockSeq;shieldBreak=frame.ShieldBreakSeq;
                Durability(frame,now,false);
                if(newDash){Start(ref Dash,now);AddWave(now,false);}
                if(newContact){Start(ref Contact,now);ConfirmWave(now);}
                // Legacy block ticks include partial damage. Only HUD2's full-block
                // event is success proof, and it is independent of remaining threats.
                if(newBlock){BlockFromColor=System.Drawing.Color.FromArgb(240,blockFrom);BlockOnCurrentShield=sameShield;Start(ref Green,now);for(int i=0;i<(int)Math.Min(4u,blockDelta);i++)AddBlockWave(now+i*60);}
                if(newBreak&&matchingBreak){Start(ref Break,now);BreakIdentity=frame.ShieldBreakIdentity;BreakFraction=!double.IsNaN(oldFraction)&&oldFraction>0?oldFraction:(oldMax>0?Math.Min(1,1.0/oldMax):.25);BrokenCore=!shieldPresent;}
                WarningEvent(frame,now);
                ExpirePulses(now);
            }
            internal bool Drawable { get { return Frame!=null&&!Frame.Suppressed&&Frame.Allowed&&Frame.Visible&&ValidAnchor(Frame.X,Frame.Y)&&((Frame.HasShield&&Frame.ShieldCurrentValid)||Frame.Threats>0||Green.Active||Break.Active||Dash.Active||Contact.Active||ActiveWaves>0); } }
        }
        internal enum RenderDecision {Hidden,Rasterize,Move,Reuse}
        internal sealed class RenderCache {
            // Store values, never the mutable HudFrame reference or frame counter.
            private struct VisualKey {
                internal int Pid,ShieldType,Current,Maximum,Hits,ThreatColor,Flags;internal uint HudVersion;internal ulong ShieldIdentity;internal double Fraction,Urgency;
                internal bool Same(VisualKey other){return Pid==other.Pid&&ShieldType==other.ShieldType&&Current==other.Current&&Maximum==other.Maximum&&Hits==other.Hits&&ThreatColor==other.ThreatColor&&Flags==other.Flags&&HudVersion==other.HudVersion&&ShieldIdentity==other.ShieldIdentity&&Fraction.Equals(other.Fraction)&&Urgency.Equals(other.Urgency);}
            }
            private bool valid,wasAnimated;private VisualKey visual;private int width,x,y;private double scale,subX,subY;
            internal void Invalidate(){valid=wasAnimated=false;}
            internal static bool Animated(Playback state,double now){
                var f=state.Frame;if(f==null)return false;bool shield=f.HasShield&&f.ShieldCurrentValid,threat=f.Threats>0;
                if(state.Green.Active||state.Dash.Active||state.Contact.Active||state.Break.Active||state.ActiveWaves>0||state.DurabilityAnimating(now)||(threat&&state.Warning.Active))return true;
                if(threat&&(f.DodgeUnavailable||CriticalProgress(f.EtaMs)>0))return true;
                double fraction=state.DisplayFraction(now);return shield&&!f.ShieldNoWear&&!double.IsNaN(fraction)&&fraction>0&&(f.HitsRemaining==1||f.HitsRemaining==2);
            }
            internal RenderDecision Decide(Playback state,double now,int nextWidth,double nextScale,int nextX,int nextY,double nextSubX,double nextSubY){
                if(!state.Drawable){Invalidate();return RenderDecision.Hidden;}var f=state.Frame;bool threat=f.Threats>0,animated=Animated(state,now);
                var key=new VisualKey{Pid=f.GamePid,HudVersion=f.HudVersion,ShieldIdentity=f.ShieldIdentity,ShieldType=f.ShieldType,Current=f.ShieldCurrent,Maximum=f.ShieldMax,Hits=f.HitsRemaining,Fraction=state.DisplayFraction(now),Urgency=threat?(f.DodgeUnavailable?1:CriticalProgress(f.EtaMs)):0,ThreatColor=threat?(f.DodgeUnavailable?System.Drawing.Color.FromArgb(255,255,56,52):ThreatColor(f.EtaMs)).ToArgb():0,Flags=(f.HasShield?1:0)|(f.ShieldCurrentValid?2:0)|(f.ShieldMaxKnown?4:0)|(f.ShieldNoWear?8:0)|(threat?16:0)|(f.DodgeUnavailable?32:0)|(state.BrokenCore?64:0)};
                RenderDecision decision=!valid||animated||wasAnimated||width!=nextWidth||scale!=nextScale||subX!=nextSubX||subY!=nextSubY||!visual.Same(key)?RenderDecision.Rasterize:(x!=nextX||y!=nextY?RenderDecision.Move:RenderDecision.Reuse);
                valid=true;wasAnimated=animated;visual=key;width=nextWidth;scale=nextScale;x=nextX;y=nextY;subX=nextSubX;subY=nextSubY;return decision;
            }
        }
        private sealed class Command { internal int Kind;internal float X,Y; }
        private volatile Func<HudFrame> provider;
        private volatile Command command;
        private volatile bool stopping,visible,success;
        private volatile string lastError;
        private readonly Thread renderThread;
        private IntPtr window,dc,bitmap,oldBitmap;
        private System.Drawing.Bitmap surface;
        private System.Drawing.Graphics graphics;
        private int width;
        private readonly RenderCache renderCache=new RenderCache();
        private static readonly object registrationGate=new object();
        private static bool registered;
        private static readonly WindowProcedure procedure=WindowProc;
        public Func<HudFrame> FrameProvider {get{return provider;}set{provider=value;}}
        public bool SuccessActive {get{return success;}}
        public bool IsVisible {get{return visible;}}
        public string LastError {get{return lastError;}}
        public ThreatWindow(){renderThread=new Thread(RenderLoop){IsBackground=true,Name="Dodger HUD renderer"};renderThread.Start();}
        public static bool ValidAnchor(float x,float y){return !float.IsNaN(x)&&!float.IsInfinity(x)&&!float.IsNaN(y)&&!float.IsInfinity(y);}
        // Native 6 supplies the physical marker center beside the actual overhead bars.
        internal static Point HudOrigin(float x,float y,double sx,double sy){return new Point(x-CanvasCenter*sx,y-CanvasCenter*sy);}
        internal static double Fraction(HudFrame f){return f!=null&&f.HasShield&&f.ShieldCurrentValid&&f.ShieldMaxKnown&&f.ShieldMax>0?Math.Max(0,Math.Min(1,(double)f.ShieldCurrent/f.ShieldMax)):double.NaN;}
        internal static double WarningPeriod(HudFrame f){return f!=null&&f.HitsRemaining==1?360:700;}
        internal static double WarningStrength(HudFrame f,double now){if(f==null||!f.HasShield||!f.ShieldCurrentValid||f.ShieldNoWear)return 0;double cap=f.HitsRemaining==1?0.64:(f.HitsRemaining==2?0.24:0);return cap*(0.5+0.5*Math.Sin(now/WarningPeriod(f)*Math.PI*2));}
        internal static bool Urgent(HudFrame f){return f!=null&&f.Threats>0&&f.EtaMs>=0&&f.EtaMs<=300;}
        internal static double CriticalProgress(float eta){if(float.IsNaN(eta)||float.IsInfinity(eta)||eta<0)return 0;double t=Math.Max(0,Math.Min(1,(650.0-eta)/350.0));return t*t*(3-2*t);}
        internal static double EaseOut(double progress){double t=Math.Max(0,Math.Min(1,progress));return 1-Math.Pow(1-t,3);}
        internal static double BlockTint(Pulse pulse,double now){if(!pulse.Active)return 0;double age=Math.Max(0,now-pulse.Started);return age<BlockRise?EaseOut(age/BlockRise):1-EaseOut((age-BlockRise)/(BlockDuration-BlockRise));}
        public void ShowThreat(float x,float y){command=new Command{Kind=1,X=x,Y=y};}
        public void ShowSuccess(float x,float y){command=new Command{Kind=2,X=x,Y=y};}
        public void Follow(float x,float y){command=new Command{Kind=3,X=x,Y=y};}
        public void Dismiss(){command=new Command{Kind=4};}
        public void Close(){Dispose();}
        public void Dispose(){if(stopping)return;stopping=true;if(Thread.CurrentThread!=renderThread)renderThread.Join(500);}
        private static double Clock(){return Stopwatch.GetTimestamp()*1000.0/Stopwatch.Frequency;}
        private void RenderLoop() {
            var state=new Playback();Command previous=null;HudFrame legacy=null;
            while(!stopping) {
            try {
                CreateNativeWindow();
                lastError=null;
                while(!stopping) {
                    double started=Clock();PumpMessages();
                    HudFrame frame=null;var sample=provider;bool legacySuccess=false;
                    if(sample!=null){try{frame=sample();}catch(Exception ex){lastError="HUD sampling: "+ex.Message;}}
                    else {
                        var next=command;
                        if(next!=null&&!ReferenceEquals(previous,next)) {
                            previous=next;
                            if(next.Kind==4){legacy=null;state.Clear();}
                            else {
                                if(legacy==null)legacy=new HudFrame{GamePid=-1,Allowed=true,Visible=true};
                                legacy.X=next.X;legacy.Y=next.Y;
                                if(next.Kind==1)legacy.Threats=1;
                                if(next.Kind==2){legacy.Threats=0;legacy.BlockSeq++;legacySuccess=true;}
                            }
                        }
                        frame=legacy;
                    }
                    state.Advance(frame,started);if(legacySuccess){state.BlockFromColor=state.ShieldColor(started,state.DisplayFraction(started));state.BlockOnCurrentShield=true;state.Start(ref state.Green,started);state.AddBlockWave(started);}success=state.Green.Active;
                    bool drawable=state.Drawable;if(drawable)Present(state,started);else HideNative();
                    // DWM provides frame pacing while the WPF panel is hidden; no anchor filtering is used.
                    if(!drawable){Thread.Sleep(16);continue;}
                    // Do not add Sleep(1) after a successful DWM flush: the default timer quantum
                    // could turn a high-refresh monitor into 40-60Hz. Follow its native cadence.
                    int dwm=DwmFlush();double elapsed=Clock()-started;
                    if(dwm!=0&&elapsed<8)Thread.Sleep((int)Math.Max(1,8-elapsed));
                }
            }catch(Exception ex){lastError="HUD renderer: "+ex.Message;}
            finally {HideNative();ReleaseSurface();if(dc!=IntPtr.Zero)DeleteDC(dc);dc=IntPtr.Zero;if(window!=IntPtr.Zero)DestroyWindow(window);window=IntPtr.Zero;}
            if(!stopping)Thread.Sleep(200);
            }
        }
        private void CreateNativeWindow() {
            IntPtr instance=GetModuleHandle(null);
            lock(registrationGate) {
                if(!registered){var wc=new WindowClass{Size=(uint)Marshal.SizeOf(typeof(WindowClass)),Procedure=Marshal.GetFunctionPointerForDelegate(procedure),Instance=instance,Name="DodgerNativeHudV1"};if(RegisterClassEx(ref wc)==0&&Marshal.GetLastWin32Error()!=1410)throw new IOException("RegisterClassEx "+Marshal.GetLastWin32Error());registered=true;}
            }
            window=CreateWindowEx(0x00080000|0x00000020|0x08000000|0x00000080|0x00000008,"DodgerNativeHudV1","Dodger HUD",0x80000000,0,0,1,1,IntPtr.Zero,IntPtr.Zero,instance,IntPtr.Zero);
            if(window==IntPtr.Zero)throw new IOException("CreateWindowEx "+Marshal.GetLastWin32Error());
            dc=CreateCompatibleDC(IntPtr.Zero);if(dc==IntPtr.Zero)throw new IOException("CreateCompatibleDC");
        }
        private static IntPtr WindowProc(IntPtr h,uint message,IntPtr w,IntPtr l){if(message==0x84)return new IntPtr(-1);if(message==0x21)return new IntPtr(3);if(message==0x14||message==0x10)return IntPtr.Zero;return DefWindowProc(h,message,w,l);}
        private void PumpMessages(){Message message;while(PeekMessage(out message,IntPtr.Zero,0,0,1)){TranslateMessage(ref message);DispatchMessage(ref message);}}
        private void HideNative(){if(visible&&window!=IntPtr.Zero)ShowWindow(window,0);visible=false;success=false;renderCache.Invalidate();}
        private void ReleaseSurface(){renderCache.Invalidate();if(graphics!=null){graphics.Dispose();graphics=null;}if(surface!=null){surface.Dispose();surface=null;}if(bitmap!=IntPtr.Zero){SelectObject(dc,oldBitmap);DeleteObject(bitmap);bitmap=IntPtr.Zero;}width=0;}
        private void EnsureSurface(int size) {
            if(size==width&&surface!=null)return;ReleaseSurface();
            var info=new BitmapInfo{Size=40,Width=size,Height=-size,Planes=1,Bits=32};IntPtr bits;
            bitmap=CreateDIBSection(dc,ref info,0,out bits,IntPtr.Zero,0);if(bitmap==IntPtr.Zero)throw new IOException("CreateDIBSection");
            oldBitmap=SelectObject(dc,bitmap);if(oldBitmap==IntPtr.Zero||oldBitmap==new IntPtr(-1)){DeleteObject(bitmap);bitmap=IntPtr.Zero;throw new IOException("SelectObject");}
            surface=new System.Drawing.Bitmap(size,size,size*4,System.Drawing.Imaging.PixelFormat.Format32bppPArgb,bits);graphics=System.Drawing.Graphics.FromImage(surface);width=size;
        }
        private void Present(Playback state,double now) {
            double scale=GetDpiForWindow(window)/96.0;if(scale<0.5||scale>4)scale=1;
            EnsureSurface((int)Math.Ceiling(CanvasSize*scale));Point raw=HudOrigin(state.Frame.X,state.Frame.Y,scale,scale);
            int x=(int)Math.Floor(raw.X),y=(int)Math.Floor(raw.Y);
            RenderDecision decision=renderCache.Decide(state,now,width,scale,x,y,raw.X-x,raw.Y-y);
            if(decision==RenderDecision.Hidden){HideNative();return;}
            if(decision==RenderDecision.Reuse)return;
            if(decision==RenderDecision.Move&&visible&&SetWindowPos(window,IntPtr.Zero,x,y,0,0,0x0215))return;
            if(decision==RenderDecision.Rasterize){Draw(graphics,width,scale,state,now,raw.X-x,raw.Y-y);graphics.Flush(System.Drawing.Drawing2D.FlushIntention.Sync);}
            var target=new NativePoint{X=x,Y=y};var origin=new NativePoint();var size=new NativeSize{X=width,Y=width};var blend=new Blend{Alpha=255,Format=1};
            if(!UpdateLayeredWindow(window,IntPtr.Zero,ref target,ref size,dc,ref origin,0,ref blend,2))throw new IOException("UpdateLayeredWindow "+Marshal.GetLastWin32Error());
            if(!visible){SetWindowPos(window,new IntPtr(-1),0,0,0,0,0x0013);ShowWindow(window,4);visible=true;}
        }
        private static System.Drawing.Color Mix(System.Drawing.Color a,System.Drawing.Color b,double t){return System.Drawing.Color.FromArgb(a.A,(int)Math.Round(a.R+(b.R-a.R)*t),(int)Math.Round(a.G+(b.G-a.G)*t),(int)Math.Round(a.B+(b.B-a.B)*t));}
        internal static System.Drawing.Color DurabilityColor(double fraction) {
            var red=System.Drawing.Color.FromArgb(240,244,75,65);var yellow=System.Drawing.Color.FromArgb(240,246,211,77);var lime=System.Drawing.Color.FromArgb(240,174,225,79);var green=System.Drawing.Color.FromArgb(240,89,210,116);
            if(fraction<=0.25)return red;if(fraction<=0.5)return Mix(red,yellow,(fraction-0.25)*4);if(fraction<=0.75)return Mix(yellow,lime,(fraction-0.5)*4);return Mix(lime,green,(fraction-0.75)*4);
        }
        internal static System.Drawing.Color ThreatColor(float eta) {
            if(float.IsNaN(eta)||float.IsInfinity(eta)||eta<0)return System.Drawing.Color.FromArgb(246,255,155,57);
            if(eta<=800){double t=CriticalProgress(eta);var color=Mix(System.Drawing.Color.FromArgb(246,255,155,57),System.Drawing.Color.FromArgb(255,255,56,52),t);return System.Drawing.Color.FromArgb((int)Math.Round(246+9*t),color);}
            return System.Drawing.Color.FromArgb(246,255,219,80);
        }
        // Fixed angular wear is sampled identically on every frame: movement and pulses never jitter.
        private static double Wear(double angle,double phase) {return 0.31*Math.Sin(angle*7+phase)+0.22*Math.Sin(angle*17+phase*2)+0.15*Math.Sin(angle*31-phase);}
        private struct ArcSample {internal double Cos,Sin,Wear,InnerWear,Chip;}
        private static ArcSample RoughSample(double angle){return new ArcSample{Cos=Math.Cos(angle),Sin=Math.Sin(angle),Wear=Wear(angle,0.1),InnerWear=Wear(angle,1.4),Chip=Math.Pow(Math.Max(0,Math.Sin(angle*13+0.7)),18)*0.85};}
        private static ArcSample WaveSample(double angle){return new ArcSample{Cos=Math.Cos(angle),Sin=Math.Sin(angle),Wear=.38*Math.Sin(angle*7+.3)+.24*Math.Sin(angle*11-.8),Chip=.65*Math.Pow(Math.Max(0,Math.Sin(angle*5+.9)),12)};}
        private static ArcSample[] ArcSamples(double start,double sweep,double step,bool wave){int segments=Math.Max(2,(int)Math.Ceiling(sweep/step));var samples=new ArcSample[segments+1];for(int i=0;i<=segments;i++){double angle=(start+sweep*i/segments)*Math.PI/180;samples[i]=wave?WaveSample(angle):RoughSample(angle);}return samples;}
        private static ArcSample[] BackplateSamples(){var samples=new ArcSample[144];for(int i=0;i<samples.Length;i++){double angle=i*Math.PI*2/samples.Length;samples[i]=new ArcSample{Cos=Math.Cos(angle),Sin=Math.Sin(angle),Wear=Wear(angle,0.1)};}return samples;}
        // Only immutable, fixed contours are retained (about 13 KB); changing durability arcs are sampled directly.
        private static readonly ArcSample[] fullArc=ArcSamples(-90,360,2.5,false),waveArc=ArcSamples(-90,360,15,true),waveGlint=ArcSamples(-120,45,15,true),waveAccent=ArcSamples(45,15,15,true),backplateArc=BackplateSamples();
        private static void RoughArc(System.Drawing.Graphics g,System.Drawing.Color color,double start,double sweep,float radius,float thickness) {
            if(sweep<=0||color.A==0)return;int segments=Math.Max(2,(int)Math.Ceiling(sweep/2.5));var points=new System.Drawing.PointF[(segments+1)*2];
            var fixedSamples=start==-90&&sweep==360?fullArc:null;
            for(int side=0;side<2;side++)for(int i=0;i<=segments;i++) {
                int sample=side==0?i:segments-i;
                if(fixedSamples==null){double angle=(start+sweep*sample/segments)*Math.PI/180;double bite=Math.Pow(Math.Max(0,Math.Sin(angle*13+0.7)),18)*0.85;double fallbackEdge=radius+(side==0?thickness/2:-thickness/2)+Wear(angle,side==0?0.1:1.4)+(side==0?-bite:bite*0.4);points[side*(segments+1)+i]=new System.Drawing.PointF((float)(Math.Cos(angle)*fallbackEdge),(float)(Math.Sin(angle)*fallbackEdge));continue;}
                var contour=fixedSamples[sample];
                double edge=radius+(side==0?thickness/2:-thickness/2)+(side==0?contour.Wear:contour.InnerWear)+(side==0?-contour.Chip:contour.Chip*0.4);
                points[side*(segments+1)+i]=new System.Drawing.PointF((float)(contour.Cos*edge),(float)(contour.Sin*edge));
            }
            using(var path=new System.Drawing.Drawing2D.GraphicsPath())using(var brush=new System.Drawing.SolidBrush(color)){path.AddPolygon(points);g.FillPath(brush,path);}
        }
        private static void Backplate(System.Drawing.Graphics g,float radius,int alpha=205) {
            var points=new System.Drawing.PointF[144];for(int i=0;i<points.Length;i++){var contour=backplateArc[i];double edge=radius+contour.Wear*0.6;points[i]=new System.Drawing.PointF((float)(contour.Cos*edge),(float)(contour.Sin*edge));}
            using(var brush=new System.Drawing.SolidBrush(System.Drawing.Color.FromArgb(alpha,5,7,8)))g.FillPolygon(brush,points);
        }
        private static void Check(System.Drawing.Graphics g,System.Drawing.Color color){using(var pen=new System.Drawing.Pen(color,2.5f){StartCap=System.Drawing.Drawing2D.LineCap.Flat,EndCap=System.Drawing.Drawing2D.LineCap.Flat,LineJoin=System.Drawing.Drawing2D.LineJoin.Bevel})g.DrawLines(pen,new[]{new System.Drawing.PointF(-7,0),new System.Drawing.PointF(-2,5),new System.Drawing.PointF(8,-6)});}
        private static void Chevron(System.Drawing.Graphics g,System.Drawing.Color color){using(var pen=new System.Drawing.Pen(color,2.4f){StartCap=System.Drawing.Drawing2D.LineCap.Flat,EndCap=System.Drawing.Drawing2D.LineCap.Flat,LineJoin=System.Drawing.Drawing2D.LineJoin.Bevel}){g.DrawLines(pen,new[]{new System.Drawing.PointF(-8,-5),new System.Drawing.PointF(-2,0),new System.Drawing.PointF(-8,5)});g.DrawLines(pen,new[]{new System.Drawing.PointF(0,-5),new System.Drawing.PointF(6,0),new System.Drawing.PointF(0,5)});}}
        private static void Exclamation(System.Drawing.Graphics g,System.Drawing.Color color){using(var brush=new System.Drawing.SolidBrush(color)){g.FillPolygon(brush,new[]{new System.Drawing.PointF(-1.8f,-10),new System.Drawing.PointF(1.7f,-9.5f),new System.Drawing.PointF(1.2f,2.5f),new System.Drawing.PointF(-1.2f,2.5f)});g.FillPolygon(brush,new[]{new System.Drawing.PointF(-1.7f,7),new System.Drawing.PointF(1.7f,6.7f),new System.Drawing.PointF(1.5f,10),new System.Drawing.PointF(-1.5f,10.2f)});}}
        private static void PulseRing(System.Drawing.Graphics g,Pulse pulse,double now,double duration,System.Drawing.Color color) {
            if(!pulse.Active)return;double age=Math.Max(0,Math.Min(1,(now-pulse.Started)/duration));float radius=(float)(26+7*(1-Math.Pow(1-age,3)));int alpha=(int)Math.Round((1-age)*230);
            RoughArc(g,System.Drawing.Color.FromArgb(alpha,color),-90,360,radius,2.2f);
        }
        // Independent wavefronts: rapid dashes never replace a previous sonar.
        // A real dash is cyan; a confirmed projectile evade adds mint. A separate
        // green wave marks each full block without consuming or recoloring a dash.
        // The fixed facets and cut edges expand together, with no random per-frame shimmer.
        private static void WornWaveArc(System.Drawing.Graphics g,System.Drawing.Color color,double start,double sweep,float radius,float thickness) {
            if(color.A==0||sweep<=0)return;int segments=Math.Max(2,(int)Math.Ceiling(sweep/15));var points=new System.Drawing.PointF[(segments+1)*2];
            var fixedSamples=start==-90&&sweep==360?waveArc:(start==-120&&sweep==45?waveGlint:(start==45&&sweep==15?waveAccent:null));
            for(int side=0;side<2;side++)for(int i=0;i<=segments;i++){
                int sample=side==0?i:segments-i;var contour=fixedSamples==null?WaveSample((start+sweep*sample/segments)*Math.PI/180):fixedSamples[sample];
                double edge=radius+contour.Wear+(side==0?thickness/2-contour.Chip:-thickness/2+contour.Chip*.22);
                points[side*(segments+1)+i]=new System.Drawing.PointF((float)(contour.Cos*edge),(float)(contour.Sin*edge));
            }
            using(var path=new System.Drawing.Drawing2D.GraphicsPath())using(var brush=new System.Drawing.SolidBrush(color)){path.AddPolygon(points);g.FillPath(brush,path);}
        }
        private static void Sonar(System.Drawing.Graphics g,Wave wave,double now) {
            if(!wave.Active)return;
            for(int i=0;i<2;i++){
                double elapsed=now-wave.Started-i*80,age=elapsed/(WaveDuration-i*80);if(age<0||age>=1)continue;
                double ease=1-Math.Pow(1-age,3),fade=Math.Pow(1-age,1.65)*Math.Min(1,elapsed/36);
                float radius=(float)(25+(i==0?24:20)*ease),width=(float)(2.4-1.3*age);int alpha=(int)Math.Round((i==0?205:105)*fade);
                var rgb=wave.Kind==WaveKind.Block?System.Drawing.Color.FromArgb(156,247,112):(wave.Confirmed?System.Drawing.Color.FromArgb(166,255,208):System.Drawing.Color.FromArgb(117,231,246));
                WornWaveArc(g,System.Drawing.Color.FromArgb(alpha/9,rgb),-90,360,radius,width+4);
                WornWaveArc(g,System.Drawing.Color.FromArgb(alpha,rgb),-90,360,radius,width);
                WornWaveArc(g,System.Drawing.Color.FromArgb(alpha/2,225,255,248),-120,45,radius,.75f);
                WornWaveArc(g,System.Drawing.Color.FromArgb(alpha/4,rgb),45,15,radius+2.6f,.7f);
            }
        }
        private static void ShieldFragments(System.Drawing.Graphics g,Playback state,double now){
            if(!state.Break.Active)return;double age=Math.Max(0,Math.Min(1,(now-state.Break.Started)/BreakDuration)),ease=EaseOut(age),sweep=360*Math.Max(.05,Math.Min(1,state.BreakFraction));
            int count=Math.Max(3,Math.Min(7,(int)Math.Ceiling(sweep/24))),alpha=(int)Math.Round(240*Math.Pow(1-age,1.35));double start=-90+360-sweep,part=sweep/count;
            for(int i=0;i<count;i++){
                double middle=(start+part*(i+.5))*Math.PI/180,travel=(8+(i%3)*1.8)*ease;var saved=g.Save();
                g.TranslateTransform((float)(Math.Cos(middle)*travel),(float)(Math.Sin(middle)*travel));g.RotateTransform((float)((i%2==0?1:-1)*9*ease));
                RoughArc(g,System.Drawing.Color.FromArgb(alpha,247,77,64),start+part*i+part*.07,part*.83,(float)(24+2.5*ease),(float)(5.4-2*age));g.Restore(saved);
            }
        }
        internal static double WarningPop(Pulse pulse,double now){if(!pulse.Active)return 0;double age=(now-pulse.Started)/340;return age>=0&&age<1?Math.Sin(age*Math.PI)*Math.Pow(1-age,1.2):0;}
        internal static double DangerBreath(double now,double urgency){return urgency*(.5+.5*Math.Sin(now*Math.PI*2/420));}
        private static int PulseAlpha(Pulse pulse,double now,double duration){return (int)Math.Round(Math.Max(0,Math.Min(1,1-(now-pulse.Started)/duration))*241);}
        internal static void Draw(System.Drawing.Graphics g,int size,double scale,Playback state,double now,double subX,double subY) {
            g.ResetTransform();g.Clear(System.Drawing.Color.Transparent);g.SmoothingMode=System.Drawing.Drawing2D.SmoothingMode.AntiAlias;g.PixelOffsetMode=System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
            g.TranslateTransform((float)(CanvasCenter*scale+subX),(float)(CanvasCenter*scale+subY));g.ScaleTransform((float)scale,(float)scale);
            var f=state.Frame;bool shield=f!=null&&f.HasShield&&f.ShieldCurrentValid;bool threat=f!=null&&f.Threats>0;double fraction=state.DisplayFraction(now),blockTint=state.BlockOnCurrentShield?BlockTint(state.Green,now):0;
            var threatColor=threat&&f.DodgeUnavailable?System.Drawing.Color.FromArgb(255,255,56,52):ThreatColor(f==null?-1:f.EtaMs);double urgency=threat?(f.DodgeUnavailable?1:CriticalProgress(f.EtaMs)):0;bool urgent=urgency>0;
            foreach(var wave in state.Waves)Sonar(g,wave,now);
            ShieldFragments(g,state,now);
            double pop=threat?WarningPop(state.Warning,now):0,breath=threat?DangerBreath(now,urgency):0;
            var markerTransform=g.Save();float markerScale=(float)(1+.055*urgency+.10*pop);g.ScaleTransform(markerScale,markerScale);
            double coreFade=!shield&&!threat&&state.BrokenCore&&!state.Dash.Active&&!state.Contact.Active?1-EaseOut((now-state.Break.Started)/BreakDuration):1;
            Backplate(g,shield&&threat?(float)(26.3+0.35*urgency):(float)(24+urgency),(int)Math.Round(205*coreFade));
            if(shield) {
                if(double.IsNaN(fraction))RoughArc(g,System.Drawing.Color.FromArgb(145,193,201,184),-90,360,24,1.8f);
                else if(fraction>0) {
                    double warning=WarningStrength(f,now)*(1-blockTint);var color=state.ShieldColor(now,fraction);color=System.Drawing.Color.FromArgb((int)Math.Round(color.A*(1-warning*0.58)),color);
                    RoughArc(g,color,-90+360*(1-fraction),360*fraction,24,5.4f);
                }
            }
            if(threat) {
                float radius=shield?(float)(26.3+0.35*urgency):(float)(24+urgency);
                if(urgent){RoughArc(g,System.Drawing.Color.FromArgb((int)Math.Round(20*urgency+45*breath),255,42,36),-90,360,radius,6.0f);}
                int edgeAlpha=(int)Math.Round(threatColor.A*(1-.22*urgency+.22*breath));threatColor=System.Drawing.Color.FromArgb(edgeAlpha,threatColor);
                RoughArc(g,threatColor,-90,360,radius,(float)(1.9+1.1*urgency+.45*breath));
            }
            if(urgent){var saved=g.Save();float symbolScale=(float)(1+.30*urgency+.18*pop+.07*breath);g.ScaleTransform(symbolScale,symbolScale);Exclamation(g,threatColor);g.Restore(saved);}
            else if(state.ShowBlockGlyph)Check(g,System.Drawing.Color.FromArgb(PulseAlpha(state.Green,now,BlockDuration),193,255,158));
            else if(state.ShowDashGlyph)Chevron(g,System.Drawing.Color.FromArgb(PulseAlpha(state.Dash,now,DashDuration),117,230,255));
            else if(state.Contact.Active)Check(g,System.Drawing.Color.FromArgb(PulseAlpha(state.Contact,now,ContactDuration),158,245,255));
            else if(threat){var symbol=g.Save();float symbolScale=(float)(1+.18*pop);g.ScaleTransform(symbolScale,symbolScale);Exclamation(g,threatColor);g.Restore(symbol);}
            g.Restore(markerTransform);g.ResetTransform();
        }
        internal static System.Drawing.Bitmap Preview(HudFrame frame,double now,string effect) {
            var playback=new Playback();playback.Advance(frame,now);playback.Frame=frame;
            if(effect=="block"){playback.BlockFromColor=playback.ShieldColor(now,Fraction(frame));playback.BlockOnCurrentShield=true;playback.Start(ref playback.Green,now-110);playback.AddBlockWave(now-110);}if(effect=="dash"){playback.Start(ref playback.Dash,now-70);playback.AddWave(now-70,false);}if(effect=="contact"){playback.Start(ref playback.Contact,now-80);playback.AddWave(now-80,true);}
            return PreviewState(playback,now,2);
        }
        internal static System.Drawing.Bitmap PreviewState(Playback playback,double now,double scale){
            int size=(int)Math.Ceiling(CanvasSize*scale);var bitmap=new System.Drawing.Bitmap(size,size,System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
            using(var g=System.Drawing.Graphics.FromImage(bitmap)){if(playback.Drawable)Draw(g,size,scale,playback,now,0,0);else g.Clear(System.Drawing.Color.Transparent);}return bitmap;
        }
        [StructLayout(LayoutKind.Sequential)] private struct NativePoint{public int X,Y;}
        [StructLayout(LayoutKind.Sequential)] private struct NativeSize{public int X,Y;}
        [StructLayout(LayoutKind.Sequential,Pack=1)] private struct Blend{public byte Operation,Flags,Alpha,Format;}
        [StructLayout(LayoutKind.Sequential)] private struct BitmapInfo{public uint Size;public int Width,Height;public ushort Planes,Bits;public uint Compression,ImageSize;public int XPels,YPels;public uint Colors,Important;}
        [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] private struct WindowClass{public uint Size,Style;public IntPtr Procedure;public int ClassExtra,WindowExtra;public IntPtr Instance,Icon,Cursor,Background;public string Menu,Name;public IntPtr SmallIcon;}
        [StructLayout(LayoutKind.Sequential)] private struct Message{public IntPtr Hwnd;public uint Value;public UIntPtr W;public IntPtr L;public uint Time;public NativePoint Point;public uint Private;}
        [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate IntPtr WindowProcedure(IntPtr h,uint message,IntPtr w,IntPtr l);
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string name);
        [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern ushort RegisterClassEx(ref WindowClass c);
        [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern IntPtr CreateWindowEx(uint extended,string klass,string title,uint style,int x,int y,int width,int height,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr parameter);
        [DllImport("user32.dll")] private static extern bool DestroyWindow(IntPtr h);
        [DllImport("user32.dll")] private static extern IntPtr DefWindowProc(IntPtr h,uint message,IntPtr w,IntPtr l);
        [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr h,int command);
        [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr h);
        [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int hgt,uint flags);
        [DllImport("user32.dll",SetLastError=true)] private static extern bool UpdateLayeredWindow(IntPtr h,IntPtr screen,ref NativePoint target,ref NativeSize size,IntPtr source,ref NativePoint origin,uint color,ref Blend blend,uint flags);
        [DllImport("user32.dll")] private static extern bool PeekMessage(out Message message,IntPtr h,uint first,uint last,uint remove);
        [DllImport("user32.dll")] private static extern bool TranslateMessage(ref Message message);
        [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref Message message);
        [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleDC(IntPtr source);
        [DllImport("gdi32.dll",SetLastError=true)] private static extern IntPtr CreateDIBSection(IntPtr source,ref BitmapInfo info,uint usage,out IntPtr bits,IntPtr section,uint offset);
        [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr dc,IntPtr value);
        [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr value);
        [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr dc);
        [DllImport("dwmapi.dll")] private static extern int DwmFlush();
    }


    internal sealed class GuardWindow : Window {
        private static readonly Dictionary<string,SolidColorBrush> brushes=new Dictionary<string,SolidColorBrush>();
        public static SolidColorBrush Brush(string hex) { lock(brushes){SolidColorBrush b;if(!brushes.TryGetValue(hex,out b)){b=(SolidColorBrush)new BrushConverter().ConvertFromString(hex);b.Freeze();brushes.Add(hex,b);}return b;} }
        private readonly Settings settings;
        private readonly bool preview;
        private readonly bool uiFixture;
        private readonly DispatcherTimer timer;
        private readonly ThreatWindow overlay;
        private sealed class HudBinding { public Channel Channel;public int GamePid;public bool Enabled; }
        private HudBinding hudBinding;
        private readonly HostLog hostLog;
        private readonly EventWaitHandle panelRequest;
        private RegisteredWaitHandle panelRequestWait;
        private Channel channel;
        private GameRuntime.Identity game;
        private volatile bool anyGameProcess;
        private int connectionEpoch,discoveryEpoch,selectionEpoch;
        private bool discoveryAttempted,discoveryWorking,launchWorking;
        private CancellationTokenSource discoveryCancellation;
        private GameDiscovery.Candidate installation;
        private DateTime lastPanelRead=DateTime.MinValue;
        private TextBlock folderText;
        private Button searchButton,launchButton,folderButton;
        private IntPtr self,hook;
        private Native.HookProc hookProc;
        private System.Windows.Forms.NotifyIcon tray;
        private System.Drawing.Icon trayIcon;
        private volatile bool closing;
        private bool f9Down,panel=true,working=false,userPanelRequested;
        private uint markerSeq=unchecked((uint)Environment.TickCount)|1u;
        private DateTime lastScan=DateTime.MinValue;
        private DateTime nextConnectAttempt=DateTime.MinValue;
        private int connectFailures;
        private string lastConfigSignature,lastNativeSignature,lastHudError;
        private bool? lastPause;
        private ToggleButton autoBlock,autoDodge,showOverlay,priorityDefense,blockInvulnerable,interruptCharged;
        private TextBlock stateText,connectionText,playerText,signalText,detailText,infoText,blocksValue,dodgesValue,threatsValue,logNoteText,projectileDodgesValue,traceStateText;
        private Border statePill;
        private Button attach;
        private readonly List<Action> localizers=new List<Action>();
        private Button languageRu,languageEn;
        private Expander statsExpander;
        private readonly string root=AppDomain.CurrentDomain.BaseDirectory;
        private const string ExpectedHash="C7437CED53B682904EAF18356AEF16613AFA436141E2C706A7560D3A1A4DACDD";

        public GuardWindow(StartupOptions options,EventWaitHandle request,bool uiFixture=false,string fixtureLanguage=null) {
            panelRequest=request;
            this.uiFixture=uiFixture;preview=options.Preview; settings=uiFixture?new Settings():Settings.Load();if(uiFixture&&fixtureLanguage!=null)settings.Language=I18n.Normalize(fixtureLanguage);I18n.Language=settings.Language; overlay=uiFixture?null:new ThreatWindow();if(overlay!=null)overlay.FrameProvider=ReadHudFrame;
            hostLog=new HostLog(uiFixture?Path.Combine(root,"panel-fixture-logs"):HostStorage.LogFolder);
            hostLog.Write("session_start","preview",preview,"block_requested",options.Block,"dodge_requested",options.Dodge,"minimized_requested",options.Minimized,"block_invulnerable_requested",options.BlockInvulnerable,"priority_defense_requested",options.PriorityDefense,"marker_seed",markerSeq);
            Title=Branding.Name;Icon=Branding.WindowIcon(); Width=500; SizeToContent=SizeToContent.Height; MinWidth=470; MaxHeight=Math.Max(300,SystemParameters.WorkArea.Height-24); WindowStartupLocation=WindowStartupLocation.CenterScreen;ShowActivated=!options.Minimized;
            WindowStyle=WindowStyle.None; ResizeMode=ResizeMode.NoResize; AllowsTransparency=true; Background=Brushes.Transparent; Foreground=Brush("#EAF0F1"); Topmost=false;
            FontFamily=new FontFamily("Segoe UI"); FontSize=13;
            Build();
            autoBlock.IsChecked=options.Block;autoDodge.IsChecked=options.Dodge;priorityDefense.IsChecked=!preview&&options.PriorityDefense;
            blockInvulnerable.IsChecked=!preview&&(settings.BlockInvulnerable||options.BlockInvulnerable);
            if(uiFixture)return;
            LogConfig("startup");
            bool firstLoaded=false;
            Loaded+=delegate {
                if(firstLoaded)return;firstLoaded=true;if(!preview)Scan(true);
                if(options.Minimized && !preview)Dispatcher.BeginInvoke(new Action(delegate{if(!closing && panel && !userPanelRequested)MinimizePanel("startup_minimized");}),DispatcherPriority.Background);
            };
            trayIcon=Branding.TrayIcon();tray=new System.Windows.Forms.NotifyIcon { Icon=trayIcon,Text="Dodger",Visible=true };
            var menu=new System.Windows.Forms.ContextMenuStrip();
            var openItem=menu.Items.Add(I18n.Text("Открыть панель","Open panel"),null,delegate{Dispatcher.BeginInvoke(new Action(ShowPanel));});localizers.Add(delegate{openItem.Text=I18n.Text("Открыть панель","Open panel");});
            var exitItem=menu.Items.Add(I18n.Text("Завершить","Exit"),null,delegate{Dispatcher.BeginInvoke(new Action(Close));});localizers.Add(delegate{exitItem.Text=I18n.Text("Завершить","Exit");});
            tray.ContextMenuStrip=menu;tray.DoubleClick+=delegate{Dispatcher.BeginInvoke(new Action(ShowPanel));};
            SourceInitialized+=delegate {
                self=new WindowInteropHelper(this).Handle; hookProc=KeyboardHook;
                hook=Native.SetWindowsHookEx(13,hookProc,Native.GetModuleHandle(null),0);
                if(hook==IntPtr.Zero) detailText.Text=I18n.Text("Метка F9 недоступна. Боевой журнал записывается автоматически.","F9 markers are unavailable. Combat logging is automatic.");
            };
            Closing+=delegate { closing=true;connectionEpoch++;discoveryEpoch++;CancelDiscovery();if(panelRequestWait!=null)panelRequestWait.Unregister(null);panelRequest.Dispose(); try{if(channel!=null)channel.Write(false,false,false,true,settings);}catch{}; timer.Stop(); overlay.Close(); if(hook!=IntPtr.Zero)Native.UnhookWindowsHookEx(hook); if(tray!=null){tray.Visible=false;tray.Dispose();}if(trayIcon!=null)trayIcon.Dispose(); try{settings.Save();}catch{}; if(channel!=null)channel.Dispose();hostLog.Write("session_end","reason","window_close");hostLog.Dispose(); };
            timer=new DispatcherTimer { Interval=TimeSpan.FromMilliseconds(40) }; timer.Tick+=Tick; timer.Start();
            panelRequestWait=PanelRequest.Listen(panelRequest,delegate {
                if(closing || Dispatcher.HasShutdownStarted)return;
                try{Dispatcher.BeginInvoke(new Action(delegate{if(!closing){hostLog.Write("panel_requested","source","second_launch");ShowPanel();}}));}catch(InvalidOperationException){}
            });
            StateChanged+=delegate {
                if(closing)return;
                if(WindowState==WindowState.Minimized){if(panel){panel=false;hostLog.Write("panel_visibility","visible",false,"reason","window_minimized");}}
                else if(IsActive && !panel){panel=true;hostLog.Write("panel_visibility","visible",true,"reason","window_restored");}
            };
            Activated+=delegate {
                if(!closing && WindowState!=WindowState.Minimized){userPanelRequested=true;if(!panel){panel=true;hostLog.Write("panel_visibility","visible",true,"reason","window_activated");}}
            };
            if(preview)detailText.Visibility=Visibility.Collapsed;
        }
        private void WatchLocalized(DependencyObject element,DependencyProperty property) {
            Action apply=delegate { string current=element.GetValue(property) as string;string translated=I18n.Translate(current);if(current!=translated)element.SetCurrentValue(property,translated); };
            localizers.Add(apply);DependencyPropertyDescriptor.FromProperty(property,element.GetType()).AddValueChanged(element,delegate{apply();});apply();
        }
        private void ApplyLanguage(string language) {
            settings.Language=I18n.Normalize(language);I18n.Language=settings.Language;
            foreach(var apply in localizers)apply();
            // The action is stateful: a language change must retain an active scan.
            searchButton.Content=discoveryWorking?I18n.Text("Стоп","Stop"):I18n.Text("Найти","Find");
            languageRu.Foreground=Brush(settings.Language=="ru"?"#D7E8AB":"#829798");languageEn.Foreground=Brush(settings.Language=="en"?"#D7E8AB":"#829798");
            SavePrefs();
        }
        private TextBlock Text(string text,double size,string color) { var block=new TextBlock{ Text=text,FontSize=size,Foreground=Brush(color),TextWrapping=TextWrapping.Wrap };WatchLocalized(block,TextBlock.TextProperty);WatchLocalized(block,FrameworkElement.ToolTipProperty);return block; }
        private Border Card(UIElement child) { return new Border{ Background=Brush("#181E21"),BorderBrush=Brush("#293236"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(14),Padding=new Thickness(16),Child=child }; }
        private Button Button(string text,bool primary) {
            var b=new Button { Content=text,Padding=new Thickness(18,10,18,10),Background=Brush(primary?"#D7E8AB":"#232D31"),Foreground=Brush(primary?"#182019":"#E5EBED"),BorderThickness=new Thickness(0),Cursor=Cursors.Hand,FontWeight=FontWeights.SemiBold,FontSize=13 };
            b.Template=(ControlTemplate)XamlReader.Parse("<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='Button'><Border Background='{TemplateBinding Background}' CornerRadius='8' Padding='{TemplateBinding Padding}'><ContentPresenter HorizontalAlignment='Center' VerticalAlignment='Center'/></Border><ControlTemplate.Triggers><Trigger Property='IsMouseOver' Value='True'><Setter Property='Opacity' Value='0.85'/></Trigger><Trigger Property='IsEnabled' Value='False'><Setter Property='Opacity' Value='0.45'/></Trigger></ControlTemplate.Triggers></ControlTemplate>"); WatchLocalized(b,ContentControl.ContentProperty);WatchLocalized(b,FrameworkElement.ToolTipProperty);return b;
        }
        private ToggleButton Toggle(bool value) {
            var t=new ToggleButton { Width=44,Height=26,IsChecked=value,Cursor=Cursors.Hand,VerticalAlignment=VerticalAlignment.Center,Focusable=false };
            t.Template=(ControlTemplate)XamlReader.Parse("<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='ToggleButton'><Border x:Name='Track' Background='#354044' CornerRadius='13'><Ellipse x:Name='Thumb' Width='18' Height='18' Fill='#A4ADB0' HorizontalAlignment='Left' Margin='4'><Ellipse.RenderTransform><TranslateTransform X='0'/></Ellipse.RenderTransform></Ellipse></Border></ControlTemplate>");
            t.Loaded+=delegate{AnimateToggle(t,false);};
            t.Checked+=delegate{AnimateToggle(t,true);SavePrefs();LogConfig("toggle");};t.Unchecked+=delegate{AnimateToggle(t,true);SavePrefs();LogConfig("toggle");};return t;
        }
        private static void AnimateToggle(ToggleButton toggle,bool animate) {
            toggle.ApplyTemplate();var track=toggle.Template.FindName("Track",toggle) as Border;var thumb=toggle.Template.FindName("Thumb",toggle) as System.Windows.Shapes.Ellipse;if(track==null||thumb==null)return;
            bool enabled=toggle.IsChecked==true;var transform=thumb.RenderTransform as TranslateTransform;if(transform==null)return;if(transform.IsFrozen){transform=transform.Clone();thumb.RenderTransform=transform;}
            double target=enabled?18:0,from=transform.X;Color trackColor=(Color)ColorConverter.ConvertFromString(enabled?"#C6DE99":"#354044"),thumbColor=(Color)ColorConverter.ConvertFromString(enabled?"#22311E":"#A4ADB0");
            double seconds=animate && SystemParameters.ClientAreaAnimation ? .16 : 0;
            transform.BeginAnimation(TranslateTransform.XProperty,null);transform.X=target;
            if(seconds>0)transform.BeginAnimation(TranslateTransform.XProperty,new DoubleAnimation(from,target,TimeSpan.FromSeconds(seconds)){EasingFunction=new CubicEase{EasingMode=EasingMode.EaseOut}});
            var fill=thumb.Fill as SolidColorBrush;Color oldFill=fill==null?thumbColor:fill.Color;var nextFill=new SolidColorBrush(thumbColor);thumb.Fill=nextFill;if(seconds>0)nextFill.BeginAnimation(SolidColorBrush.ColorProperty,new ColorAnimation(oldFill,thumbColor,TimeSpan.FromSeconds(seconds)));
            var prior=track.Background as SolidColorBrush;Color previous=prior==null?trackColor:prior.Color;var background=new SolidColorBrush(trackColor);track.Background=background;if(seconds>0)background.BeginAnimation(SolidColorBrush.ColorProperty,new ColorAnimation(previous,trackColor,TimeSpan.FromSeconds(seconds)));
        }
        private void SetBlockOptions(Border options,bool visible,bool animate) {
            int sequence=options.Tag is int?(int)options.Tag+1:1;options.Tag=sequence;
            double from=options.ActualHeight,opacity=options.Opacity;bool moving=animate&&options.IsLoaded&&SystemParameters.ClientAreaAnimation;
            options.BeginAnimation(FrameworkElement.HeightProperty,null);options.BeginAnimation(UIElement.OpacityProperty,null);
            if(!moving){options.Height=visible?double.NaN:0;options.Opacity=visible?1:0;options.Visibility=visible?Visibility.Visible:Visibility.Collapsed;return;}
            options.Visibility=Visibility.Visible;options.Child.Measure(new Size(Math.Max(1,Width-68),double.PositiveInfinity));double target=visible?options.Child.DesiredSize.Height:0;
            options.Height=target;options.Opacity=visible?1:0;
            var height=new DoubleAnimation(from,target,TimeSpan.FromMilliseconds(180)){EasingFunction=new CubicEase{EasingMode=EasingMode.EaseOut}};
            height.Completed+=delegate{if(!Equals(options.Tag,sequence))return;options.BeginAnimation(FrameworkElement.HeightProperty,null);options.Height=visible?double.NaN:0;if(!visible)options.Visibility=Visibility.Collapsed;};
            options.BeginAnimation(FrameworkElement.HeightProperty,height);options.BeginAnimation(UIElement.OpacityProperty,new DoubleAnimation(opacity,visible?1:0,TimeSpan.FromMilliseconds(140)));
        }
        private UIElement ToggleRow(string title,string sub,string key,out ToggleButton toggle,bool value) {
            var g=new Grid{ Margin=new Thickness(0,0,0,8),ToolTip=sub };WatchLocalized(g,FrameworkElement.ToolTipProperty); g.ColumnDefinitions.Add(new ColumnDefinition());g.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});g.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});
            var words=Text(title,15,"#ECF0F1");words.VerticalAlignment=VerticalAlignment.Center;words.ToolTip=sub;g.Children.Add(words);
            if(!string.IsNullOrEmpty(key)){var k=new Border{BorderBrush=Brush("#394449"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(5),Padding=new Thickness(7,3,7,3),Margin=new Thickness(12,0,16,0),VerticalAlignment=VerticalAlignment.Center,Child=Text(key,10,"#A8B3B6")};Grid.SetColumn(k,1);g.Children.Add(k);}
            toggle=Toggle(value);Grid.SetColumn(toggle,2);g.Children.Add(toggle);var control=toggle;g.Cursor=Cursors.Hand;
            g.MouseLeftButtonUp+=delegate(object sender,MouseButtonEventArgs e){DependencyObject node=e.OriginalSource as DependencyObject;while(node!=null&&node!=g){if(node==control)return;node=VisualTreeHelper.GetParent(node);}control.IsChecked=control.IsChecked!=true;e.Handled=true;};return g;
        }
        private UIElement Metric(string label,out TextBlock value) {
            var s=new StackPanel();value=Text("—",21,"#E5ECEE");value.FontWeight=FontWeights.SemiBold;s.Children.Add(value);var l=Text(label,10,"#89969A");l.Margin=new Thickness(0,3,0,0);s.Children.Add(l);return s;
        }
        private void Build() {
            var outer=new Border { Background=Brush("#101416"),BorderBrush=Brush("#354147"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(14),Padding=new Thickness(24,22,24,20) };
            var stack=new StackPanel();outer.Child=stack;Content=outer;
            var header=new Grid{Margin=new Thickness(0,0,0,16)};header.ColumnDefinitions.Add(new ColumnDefinition());header.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});
            var title=new StackPanel{Orientation=Orientation.Horizontal};title.Children.Add(new Image{Source=Icon,Width=27,Height=27,Margin=new Thickness(0,0,10,0)});title.Children.Add(new TextBlock { Text="DODGER",FontSize=18,FontWeight=FontWeights.Bold,Foreground=Brush("#EAF0E5"),VerticalAlignment=VerticalAlignment.Center });title.Children.Add(new TextBlock{Text="version "+Branding.Version,FontSize=10,Foreground=Brush("#829798"),Margin=new Thickness(9,0,0,0),VerticalAlignment=VerticalAlignment.Center});header.Children.Add(title);
            var controls=new StackPanel{Orientation=Orientation.Horizontal};
            languageRu=Button("RU",false);languageEn=Button("EN",false);
            foreach(var languageButton in new[]{languageRu,languageEn}){languageButton.Padding=new Thickness(5,6,5,6);languageButton.FontSize=10;languageButton.Background=Brushes.Transparent;languageButton.Focusable=false;controls.Children.Add(languageButton);}
            languageEn.Margin=new Thickness(0,0,6,0);languageRu.Click+=delegate{ApplyLanguage("ru");};languageEn.Click+=delegate{ApplyLanguage("en");};
            languageRu.Foreground=Brush(settings.Language=="ru"?"#D7E8AB":"#829798");languageEn.Foreground=Brush(settings.Language=="en"?"#D7E8AB":"#829798");var hide=Button("—",false);hide.Padding=new Thickness(10,6,10,6);hide.FontSize=11;hide.ToolTip=I18n.Text("Свернуть окно настроек","Minimize settings");hide.Click+=delegate{MinimizePanel("window_button");};controls.Children.Add(hide);var close=Button("×",false);close.Padding=new Thickness(10,3,10,7);close.FontSize=16;close.Margin=new Thickness(6,0,0,0);close.Click+=delegate{Close();};controls.Children.Add(close);Grid.SetColumn(controls,1);header.Children.Add(controls);header.MouseLeftButtonDown+=delegate(object sender,MouseButtonEventArgs e){if(e.LeftButton==MouseButtonState.Pressed)try{DragMove();}catch{}};stack.Children.Add(header);
            var statusRow=new Grid();statusRow.ColumnDefinitions.Add(new ColumnDefinition());statusRow.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});
            stateText=Text(I18n.Text("ОЖИДАНИЕ ИГРЫ","WAITING FOR GAME"),10,"#A7B8BC");stateText.FontWeight=FontWeights.SemiBold;
            statePill=new Border{Child=stateText,Background=Brush("#202B2E"),CornerRadius=new CornerRadius(5),Padding=new Thickness(8,5,8,5),HorizontalAlignment=HorizontalAlignment.Left};statusRow.Children.Add(statePill);
            var auto=Text(I18n.Text("●  Автоподключение","●  Auto-connect"),10,"#ADC58E");auto.VerticalAlignment=VerticalAlignment.Center;Grid.SetColumn(auto,1);statusRow.Children.Add(auto);stack.Children.Add(statusRow);
            detailText=Text(I18n.Text("Запустите Chained Beasts — подключение произойдёт автоматически.","Start Chained Beasts to connect automatically."),12,"#81969E");detailText.Margin=new Thickness(0,9,0,0);detailText.MinHeight=34;stack.Children.Add(detailText);
            var connectActions=new StackPanel{Orientation=Orientation.Horizontal,Margin=new Thickness(0,7,0,12)};
            launchButton=Button(I18n.Text("Запустить игру","Launch game"),false);launchButton.Padding=new Thickness(10,6,10,6);launchButton.FontSize=11;launchButton.Click+=delegate{Launch();};connectActions.Children.Add(launchButton);
            folderButton=Button(I18n.Text("Папка…","Folder…"),false);folderButton.Padding=new Thickness(10,6,10,6);folderButton.FontSize=11;folderButton.Margin=new Thickness(6,0,0,0);folderButton.ToolTip=I18n.Text("Выбрать папку с распакованной игрой","Select the extracted game folder");folderButton.Click+=delegate{ChooseFolder();};connectActions.Children.Add(folderButton);
            searchButton=Button(I18n.Text("Найти","Find"),false);searchButton.Padding=new Thickness(10,6,10,6);searchButton.FontSize=11;searchButton.Margin=new Thickness(6,0,0,0);searchButton.Click+=delegate{if(discoveryWorking)CancelDiscovery();else BeginDiscovery(true);};connectActions.Children.Add(searchButton);
            attach=Button("↻",false);attach.Padding=new Thickness(10,4,10,6);attach.ToolTip=I18n.Text("Повторить подключение","Reconnect");attach.FontSize=14;attach.Margin=new Thickness(6,0,0,0);attach.Click+=delegate{Scan(true);};connectActions.Children.Add(attach);stack.Children.Add(connectActions);
            folderText=Text(string.IsNullOrWhiteSpace(settings.GameFolder)?I18n.Text("Папка игры будет найдена автоматически.","The game folder will be found automatically."):settings.GameFolder,10,"#81969E");folderText.TextWrapping=TextWrapping.NoWrap;folderText.TextTrimming=TextTrimming.CharacterEllipsis;folderText.Margin=new Thickness(0,-5,0,12);folderText.ToolTip=folderText.Text;stack.Children.Add(folderText);
            if(preview){folderText.Visibility=Visibility.Collapsed;connectActions.Visibility=Visibility.Collapsed;}
            var divider=new Border{Height=1,Background=Brush("#293438"),Margin=new Thickness(0,0,0,14)};stack.Children.Add(divider);
            stack.Children.Add(ToggleRow(I18n.Text("Автоблок","Auto-block"),I18n.Text("Поворачивает героя к удару и ставит блок. Нужен щит или перк защиты рукой. С перком блок рукой поглощает 75% урона.","Faces the incoming attack and blocks. Requires a shield or the arm-guard perk. With the perk, arm blocks absorb 75% of damage."),I18n.Text("ПКМ","RMB"),out autoBlock,false));
            var blockOptions=new Border{Margin=new Thickness(18,0,0,0),Height=0,Opacity=0,Visibility=Visibility.Collapsed,ClipToBounds=true};
            var blockRowOption=ToggleRow(I18n.Text("Блок при неуязвимости","Block while invulnerable"),I18n.Text("Разрешает автоблок даже во время неуязвимости — ради оглушения противника. Щит при этом может терять прочность.","Allows auto-block while invulnerable to stun attackers. This can consume shield durability."),"",out blockInvulnerable,!preview&&settings.BlockInvulnerable);
            var nestedGrid=blockRowOption as Grid;if(nestedGrid!=null){var label=nestedGrid.Children[0] as TextBlock;if(label!=null)label.FontSize=13;}
            blockOptions.Child=blockRowOption;
            blockOptions.SetBinding(UIElement.IsEnabledProperty,new System.Windows.Data.Binding("IsChecked"){Source=autoBlock});
            autoBlock.Checked+=delegate{SetBlockOptions(blockOptions,true,true);};autoBlock.Unchecked+=delegate{SetBlockOptions(blockOptions,false,true);};
            blockOptions.Loaded+=delegate{SetBlockOptions(blockOptions,autoBlock.IsChecked==true,false);};
            stack.Children.Add(blockOptions);
            stack.Children.Add(ToggleRow(I18n.Text("Автоуклонение","Auto-dodge"),I18n.Text("Полноценный уворот, если блок невозможен. Учитывает настоящие кадры неуязвимости, выносливость и травмы.","Uses a full dodge when blocking is unavailable. Accounts for actual invulnerability frames, stamina and injuries."),I18n.Text("ПРОБЕЛ","SPACE"),out autoDodge,false));
            stack.Children.Add(ToggleRow(I18n.Text("Приоритет защиты","Defense priority"),I18n.Text("Задерживает новый удар, если замах помешает вовремя поставить блок или начать автоуклонение. Учитывает время до включения неуязвимости. Пропускает проверенное добивание обычным ближним ударом, если нет других угроз. Уже начатые атаки подчиняются правилам игры.","Delays a new attack if its windup would prevent a timely block or dodge. Accounts for invulnerability startup. Allows a verified normal melee finisher when no other threats remain. Attacks already started follow the game's cancellation rules."),"",out priorityDefense,false));
            stack.Children.Add(ToggleRow(I18n.Text("Прерывать усиленный удар","Interrupt charged attacks"),I18n.Text("Разрешает автоматике отменить усиленный удар в блок или рывок, когда это разрешает игра. Неотменяемый замах не обходится. Требуется включённый автоблок или автоуклонение.","Allows a charged attack to be canceled into a block or dodge when the game permits it. Cannot bypass an uncancelable windup. Requires auto-block or auto-dodge."),null,out interruptCharged,settings.InterruptChargedAttack));
            stack.Children.Add(ToggleRow(I18n.Text("Индикатор угрозы","Threat indicator"),I18n.Text("Чёрный круг с цветной дугой — прочность щита. При угрозе появляется контур и знак !. Переход из оранжевого в красный начинается за 0,65 с и завершается за 0,3 с до попадания. Последние деления щита мигают чаще.","The black circle's colored arc shows shield durability. Incoming attacks add an outline and !. Orange turns red from 0.65 to 0.3 seconds before impact. The final shield segments blink faster."),"◇",out showOverlay,settings.Overlay));
            var extra=new Expander{Header=I18n.Text("Статистика и журнал","Statistics and logs"),Foreground=Brush("#90A3AB"),FontSize=11,Margin=new Thickness(0,7,0,0)};
            var options=new StackPanel{Margin=new Thickness(1,14,1,0)};
            var blockRow=new Grid();blockRow.ColumnDefinitions.Add(new ColumnDefinition());blockRow.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});
            var blockLabel=Text(I18n.Text("Успешных блоков","Successful blocks"),13,"#A8BDC3");blockLabel.VerticalAlignment=VerticalAlignment.Center;blockRow.Children.Add(blockLabel);
            blocksValue=Text("—",24,"#CBE6B0");blocksValue.FontWeight=FontWeights.SemiBold;Grid.SetColumn(blocksValue,1);blockRow.Children.Add(blocksValue);options.Children.Add(blockRow);
            var statHint=Text(I18n.Text("Считаются только удары, которые действительно удалось заблокировать.","Counts only attacks actually blocked."),10,"#6F8992");statHint.Margin=new Thickness(0,5,0,0);options.Children.Add(statHint);
            var dashRow=new DockPanel{Margin=new Thickness(0,10,0,0)};dodgesValue=Text("—",13,"#CBE6B0");DockPanel.SetDock(dodgesValue,Dock.Right);dashRow.Children.Add(dodgesValue);dashRow.Children.Add(Text(I18n.Text("Рывков выполнено","Dashes performed"),12,"#94ABB3"));options.Children.Add(dashRow);
            var avoidedRow=new DockPanel{Margin=new Thickness(0,5,0,0)};projectileDodgesValue=Text("—",13,"#CBE6B0");DockPanel.SetDock(projectileDodgesValue,Dock.Right);avoidedRow.Children.Add(projectileDodgesValue);avoidedRow.Children.Add(Text(I18n.Text("Снарядов избегнуто","Projectiles evaded"),12,"#94ABB3"));options.Children.Add(avoidedRow);
            options.Children.Add(new Border{Height=1,Background=Brush("#293438"),Margin=new Thickness(0,12,0,12)});
            logNoteText=Text(hostLog.Error==null?I18n.Text("Журнал записывается автоматически.\nПосле прогона опиши проблему в чате.","Logs are recorded automatically.\nDescribe any issue after your session."):I18n.Text("Не удалось открыть журнал: ","Could not open the log: ")+hostLog.Error,11,"#94ABB3");options.Children.Add(logNoteText);
            var noScreens=Text(I18n.Text("Снимки экрана не сохраняются.","Screenshots are not saved."),10,"#6F8992");noScreens.Margin=new Thickness(0,5,0,0);options.Children.Add(noScreens);
            var markHint=Text(I18n.Text("F9 — отметить проблему; затем опиши её в чате.","F9 — mark an issue, then describe it afterwards."),10,"#94ABB3");markHint.Margin=new Thickness(0,7,0,0);options.Children.Add(markHint);
            traceStateText=Text(I18n.Text("Боевой журнал: ожидаем подключение.","Combat log: waiting for connection."),10,"#6F8992");traceStateText.Margin=new Thickness(0,7,0,0);options.Children.Add(traceStateText);
            var logPath=Text(hostLog.Folder,9,"#6F8992");logPath.Margin=new Thickness(0,7,0,0);options.Children.Add(logPath);
            var openLog=Button(I18n.Text("Открыть журнал","Open logs"),false);openLog.HorizontalAlignment=HorizontalAlignment.Left;openLog.Padding=new Thickness(11,7,11,7);openLog.FontSize=11;openLog.Margin=new Thickness(0,9,0,0);openLog.Click+=delegate{OpenLogFolder();};options.Children.Add(openLog);
            connectionText=Text("",11,"#81969E");playerText=Text("",11,"#81969E");signalText=Text("",11,"#81969E");threatsValue=Text("—",11,"#C4D1D6");infoText=Text("",10,"#738990");
            extra.Content=new ScrollViewer{Content=options,MaxHeight=300,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled};statsExpander=extra;WatchLocalized(extra,HeaderedContentControl.HeaderProperty);stack.Children.Add(extra);

        }
        private void SavePrefs() {
            if(showOverlay==null||uiFixture)return;
            settings.Overlay=showOverlay.IsChecked==true;if(interruptCharged!=null)settings.InterruptChargedAttack=interruptCharged.IsChecked==true;if(blockInvulnerable!=null)settings.BlockInvulnerable=blockInvulnerable.IsChecked==true;
            try{settings.Save();}catch{}
        }
        private void LogConfig(string source) {
            if(autoBlock==null || autoDodge==null || showOverlay==null || priorityDefense==null || blockInvulnerable==null)return;
            bool block=autoBlock.IsChecked==true,dodge=autoDodge.IsChecked==true,mark=showOverlay.IsChecked==true,priority=priorityDefense.IsChecked==true,invulnerable=blockInvulnerable.IsChecked==true;
            string signature=block+"|"+dodge+"|"+mark+"|"+priority+"|"+invulnerable+"|"+settings.InterruptChargedAttack;
            if(signature==lastConfigSignature)return;lastConfigSignature=signature;
            hostLog.Write("config_changed","source",source,"auto_block",block,"auto_dodge",dodge,"threat_indicator",mark,"priority_defense",priority,"block_invulnerable",invulnerable,"interrupt_charged_attack",settings.InterruptChargedAttack);
        }
        private void OpenLogFolder() {
            try{Directory.CreateDirectory(hostLog.Folder);hostLog.Write("log_folder_opened");Process.Start(new ProcessStartInfo(hostLog.Folder){UseShellExecute=true});}
            catch(Exception ex){hostLog.Write("log_folder_error","error",ex.Message);logNoteText.Text=I18n.Text("Не удалось открыть папку журнала: ","Could not open the log folder: ")+ex.Message;}
        }
        private bool GameFocused { get { return game!=null && Native.ForegroundIs(game.Pid); } }
        private IntPtr KeyboardHook(int code,IntPtr wp,IntPtr lp) {
            int key=code>=0?Marshal.ReadInt32(lp):0;
            if(code>=0 && key==0x78) {
                bool down=wp==(IntPtr)0x100 || wp==(IntPtr)0x104;
                bool up=wp==(IntPtr)0x101 || wp==(IntPtr)0x105;
                bool scope=(panel && Native.ForegroundIs(Process.GetCurrentProcess().Id)) || GameFocused;
                if(down && scope && !preview) { if(!f9Down){f9Down=true;Dispatcher.BeginInvoke(new Action(MarkProblem));}return new IntPtr(1); }
                if(up && f9Down){f9Down=false;return new IntPtr(1);}
            }
            return Native.CallNextHookEx(hook,code,wp,lp);
        }
        private void MarkProblem() {
            if(closing || preview)return;
            markerSeq=NextMarker(markerSeq);
            hostLog.Write("manual_marker","marker_id",markerSeq,"game_pid",game==null?0:game.Pid,"game_foreground",GameFocused,"panel_open",panel);
            try{if(channel!=null){channel.MarkSeq=markerSeq;channel.Write(autoBlock.IsChecked==true,autoDodge.IsChecked==true,showOverlay.IsChecked==true,!GameFocused,settings,priorityDefense.IsChecked==true,blockInvulnerable.IsChecked==true);}}
            catch(Exception ex){hostLog.Write("marker_channel_error","marker_id",markerSeq,"error",ex.Message);}
        }
        private void MinimizePanel(string reason) {
            panel=false;Topmost=false;WindowState=WindowState.Minimized;
            hostLog.Write("panel_visibility","visible",false,"reason",reason,"kept_on_taskbar",true);
        }
        private void ShowPanel() { if(closing)return;panel=true;userPanelRequested=true;hostLog.Write("panel_visibility","visible",true);try{if(channel!=null)channel.Write(autoBlock.IsChecked==true,autoDodge.IsChecked==true,showOverlay.IsChecked==true,true,settings,priorityDefense.IsChecked==true,blockInvulnerable.IsChecked==true);}catch{};Topmost=false;Show();WindowState=WindowState.Normal;Native.SetWindowPos(self,new IntPtr(-2),0,0,0,0,0x0053);Activate();Native.SetForegroundWindow(self); }
        // Filesystem/process enumeration stays on workers. No disk search runs from the 40 ms heartbeat.
        private bool Post(Action action,Action abandoned=null) {
            if(closing || Dispatcher.HasShutdownStarted){if(abandoned!=null)abandoned();return false;}
            int released=0;Action release=delegate{if(abandoned!=null&&Interlocked.Exchange(ref released,1)==0)abandoned();};
            try { var operation=Dispatcher.BeginInvoke(action);if(abandoned!=null){operation.Aborted+=delegate{release();};if(operation.Status==DispatcherOperationStatus.Aborted)release();}return true; }
            catch(InvalidOperationException){if(abandoned!=null)abandoned();return false;}
        }
        private void CancelDiscovery(){var cancel=discoveryCancellation;if(cancel!=null)try{cancel.Cancel();}catch(ObjectDisposedException){}}
        private void SetFolderMessage(string text){folderText.Text=text;folderText.ToolTip=text;}
        private void SelectInstallation(GameDiscovery.Candidate candidate) {
            if(candidate==null)return;
            if(installation!=null&&string.Equals(installation.Folder,candidate.Folder,StringComparison.OrdinalIgnoreCase)&&
                string.Equals(installation.Store,"Steam",StringComparison.OrdinalIgnoreCase)&&!string.Equals(candidate.Store,"Steam",StringComparison.OrdinalIgnoreCase)){
                candidate.Store=installation.Store;candidate.SteamAppId=installation.SteamAppId;candidate.LaunchUri=installation.LaunchUri;
            }
            bool changed=!string.Equals(settings.GameFolder,candidate.Folder,StringComparison.OrdinalIgnoreCase);
            installation=candidate;settings.GameFolder=candidate.Folder;
            SetFolderMessage(candidate.Folder);if(changed)SavePrefs();
            hostLog.Write("game_folder_selected","path",candidate.Folder,"source",candidate.Source,"store",candidate.Store);
        }
        private void BeginDiscovery(bool manual) {
            if(preview || closing || discoveryWorking)return;
            if(anyGameProcess){SetFolderMessage(I18n.Text("Игра уже запущена — поиск дисков отложен.","The game is already running. Disk search is postponed."));return;}
            discoveryAttempted=true;discoveryWorking=true;int epoch=++discoveryEpoch,selection=selectionEpoch;
            var cancel=new CancellationTokenSource();discoveryCancellation=cancel;
            searchButton.Content=I18n.Text("Стоп","Stop");SetFolderMessage(I18n.Text("Ищем игру на локальных дисках…","Searching local drives for the game…"));
            var options=new GameDiscovery.Options{SavedFolder=settings.GameFolder,LauncherFolder=root,MayContinue=delegate{return !closing&&!anyGameProcess;}};var retained=installation;
            hostLog.Write("discovery_start","manual",manual);
            var discoveryThread=new Thread(delegate() {
                GameDiscovery.Result result=null;string error=null;
                try {
                    GameDiscovery.Candidate retainedCandidate;string retainedReason;
                    bool valid=retained!=null&&GameDiscovery.ValidateFolder(retained.Folder,out retainedCandidate,out retainedReason);
                    if(!valid)Post(delegate{if(epoch==discoveryEpoch&&selection==selectionEpoch&&ReferenceEquals(installation,retained))installation=null;});
                    result=GameDiscovery.Discover(options,cancel.Token,delegate(GameDiscovery.Progress progress){
                    Post(delegate{if(closing||epoch!=discoveryEpoch||selection!=selectionEpoch)return;
                        if(progress.Candidate!=null && installation==null)SelectInstallation(progress.Candidate);
                        else if(installation==null)SetFolderMessage(I18n.Text("Поиск: ","Searching: ")+(progress.CurrentFolder??I18n.Translate(progress.Message)));
                    });
                }); }catch(Exception ex){error=ex.Message;}
                hostLog.Write("discovery_result","found",result==null?0:result.Candidates.Count,"entries",result==null?0:result.EntriesVisited,"cancelled",result!=null&&result.Cancelled,"budget_exceeded",result!=null&&result.BudgetExceeded,"error",error);
                Post(delegate {
                    if(closing||epoch!=discoveryEpoch)return;
                    discoveryWorking=false;searchButton.Content=I18n.Text("Найти","Find");
                    if(ReferenceEquals(discoveryCancellation,cancel))discoveryCancellation=null;
                    if(selection!=selectionEpoch)return;
                    if(result!=null&&result.Candidates.Count>0)SelectInstallation(result.Candidates[0]);
                    else if(installation!=null)SetFolderMessage(installation.Folder);
                    else if(anyGameProcess)SetFolderMessage(I18n.Text("Игра запущена. Ожидаем подключение…","The game is running. Waiting for connection…"));
                    else SetFolderMessage(error!=null?I18n.Text("Ошибка поиска. Выбери папку вручную.","Search failed. Select the folder manually."):(result!=null&&result.Cancelled?I18n.Text("Поиск остановлен. Можно выбрать папку вручную.","Search stopped. You can select the folder manually."):I18n.Text("Игра не найдена в пределах поиска. Нажми «Папка…».","The game was not found within the search limits. Click “Folder…”.")));
                });cancel.Dispose();
            }){IsBackground=true,Priority=ThreadPriority.BelowNormal,Name="Dodger game discovery"};discoveryThread.Start();
        }
        private static bool InspectFolder(string folder,out GameDiscovery.Candidate candidate,out string reason){
            if(!GameDiscovery.ValidateFolder(folder,out candidate,out reason))return false;
            string common=Path.GetDirectoryName(candidate.Folder);
            if(common!=null&&string.Equals(Path.GetFileName(common),"common",StringComparison.OrdinalIgnoreCase)){
                string apps=Path.GetDirectoryName(common);
                if(apps!=null&&string.Equals(Path.GetFileName(apps),"steamapps",StringComparison.OrdinalIgnoreCase)){
                    var options=new GameDiscovery.Options{SavedFolder=candidate.Folder,IncludeRegistry=false,ScanDrives=false,SteamRoots=new[]{Path.GetDirectoryName(apps)},TimeBudget=TimeSpan.FromSeconds(2),EntryBudget=4000};
                    var result=GameDiscovery.Discover(options,CancellationToken.None);
                    if(result.Candidates.Count>0)candidate=result.Candidates[0];
                }
            }
            return true;
        }
        private sealed class FolderOwner:System.Windows.Forms.IWin32Window {public IntPtr Handle{get;set;}}
        private void ChooseFolder() {
            if(preview||closing)return;
            using(var picker=new System.Windows.Forms.FolderBrowserDialog{Description=I18n.Text("Выбери папку с ChainedBeasts.exe и всей распакованной игрой","Select the folder containing ChainedBeasts.exe and the extracted game"),ShowNewFolderButton=false}) {
                if(installation!=null)picker.SelectedPath=installation.Folder;
                if(picker.ShowDialog(new FolderOwner{Handle=self})!=System.Windows.Forms.DialogResult.OK)return;
                selectionEpoch++;CancelDiscovery();string folder=picker.SelectedPath;int selection=selectionEpoch;
                folderButton.IsEnabled=false;SetFolderMessage(I18n.Text("Проверяем выбранную папку…","Checking the selected folder…"));
                ThreadPool.QueueUserWorkItem(delegate {
                    GameDiscovery.Candidate candidate;string reason;bool valid=InspectFolder(folder,out candidate,out reason);
                    Post(delegate{folderButton.IsEnabled=true;if(selection!=selectionEpoch)return;if(valid)SelectInstallation(candidate);else SetFolderMessage(reason);});
                });
            }
        }
        private void Launch() {
            if(preview||closing||launchWorking)return;
            launchWorking=true;launchButton.IsEnabled=false;var chosen=installation;string saved=settings.GameFolder;int selection=selectionEpoch;
            ThreadPool.QueueUserWorkItem(delegate {
                string error=null;GameRuntime.ProbeResult probe=null;GameDiscovery.Candidate candidate=chosen;bool started=false;
                try {
                    probe=GameRuntime.Probe();anyGameProcess=probe.AnyProcess;
                    if(!probe.AnyProcess&&!string.IsNullOrEmpty(probe.Diagnostic))throw new InvalidOperationException(probe.Diagnostic);
                    if(!probe.AnyProcess) {
                        GameDiscovery.Candidate validated;string reason;
                        if(!InspectFolder(candidate==null?saved:candidate.Folder,out validated,out reason))throw new InvalidOperationException(reason+I18n.Text(" Нажми «Папка…» или «Найти»."," Click “Folder…” or “Find”."));
                        if(candidate!=null&&!string.Equals(validated.Store,"Steam",StringComparison.OrdinalIgnoreCase)){validated.Store=candidate.Store;validated.SteamAppId=candidate.SteamAppId;validated.LaunchUri=candidate.LaunchUri;}
                        candidate=validated;
                        // Re-probe immediately before launch to avoid duplicating a game started independently.
                        probe=GameRuntime.Probe();anyGameProcess=probe.AnyProcess;
                        if(!probe.AnyProcess&&!string.IsNullOrEmpty(probe.Diagnostic))throw new InvalidOperationException(probe.Diagnostic);
                        if(closing||selection!=Volatile.Read(ref selectionEpoch))throw new OperationCanceledException(I18n.Text("Запуск отменён.","Launch canceled."));
                        if(!probe.AnyProcess){using(var launched=Process.Start(GameLaunch.Plan(candidate))){}started=true;}
                    }
                }catch(Exception ex){error=ex.Message;}
                bool didStart=started;var ready=probe;var selected=candidate;
                hostLog.Write("game_launch_result","started",started,"already_running",probe!=null&&probe.AnyProcess,"error",error);
                Post(delegate {
                    launchWorking=false;launchButton.IsEnabled=true;
                    if(selection!=selectionEpoch)return;
                    if(error!=null){detailText.Text=error;return;}
                    CancelDiscovery();if(selected!=null)SelectInstallation(selected);
                    if(ready!=null&&ready.Selected!=null){if(panel)MinimizePanel("game_selected");Native.SetForegroundWindow(ready.Selected.Window);}
                    else detailText.Text=didStart?I18n.Text("Игра запускается. Подключимся автоматически…","The game is starting. Connecting automatically…"):(ready==null?I18n.Text("Ожидаем игру…","Waiting for the game…"):ready.Diagnostic);
                    lastScan=DateTime.MinValue;Scan(true);
                });
            });
        }
        private void Tick(object sender,EventArgs e) {
            if(closing)return;
            string hudError=overlay.LastError;
            if(hudError!=lastHudError){hostLog.Write(hudError==null?"hud_renderer_recovered":"hud_renderer_error","error",hudError);lastHudError=hudError;}
            if(hostLog.Error!=null && logNoteText!=null && !Equals(logNoteText.Tag,hostLog.Error)){logNoteText.Tag=hostLog.Error;logNoteText.Text=I18n.Text("Не удалось записать журнал: ","Could not write the log: ")+hostLog.Error;}
            if(!preview && !working && (DateTime.UtcNow-lastScan).TotalSeconds>(channel==null?2:5))Scan(false);
            bool focus=GameFocused;
            bool paused=AutomationPaused(focus);
            var binding=Volatile.Read(ref hudBinding);int pid=game==null?0:game.Pid;bool enabled=!preview&&showOverlay.IsChecked==true;
            if(binding==null||binding.Channel!=channel||binding.GamePid!=pid||binding.Enabled!=enabled)Volatile.Write(ref hudBinding,new HudBinding{Channel=channel,GamePid=pid,Enabled=enabled});
            if(lastPause!=paused){lastPause=paused;hostLog.Write("automation_state","paused",paused,"panel_open",panel,"game_foreground",focus);}
            if(channel!=null) {
                try {
                    channel.MarkSeq=markerSeq;
                    channel.Write(autoBlock.IsChecked==true,autoDodge.IsChecked==true,showOverlay.IsChecked==true,!focus,settings,priorityDefense.IsChecked==true,blockInvulnerable.IsChecked==true);
                    if((DateTime.UtcNow-lastPanelRead).TotalMilliseconds>(panel?120:750)){
                        lastPanelRead=DateTime.UtcNow;var snapshot=channel.Read();if(snapshot!=null)UpdateSnapshot(snapshot,focus);
                    }
                }catch(Exception ex){hostLog.Write("channel_error","error",ex.Message);Disconnect();detailText.Text=I18n.Text("Канал недоступен: ","Channel unavailable: ")+ex.Message;}
            }
        }
        private void Scan(bool force) {
            if(preview||closing||working)return;
            lastScan=DateTime.UtcNow;working=true;attach.IsEnabled=false;
            int epoch=connectionEpoch;var previous=game;var existing=channel;
            bool allowAttempt=force||DateTime.UtcNow>=nextConnectAttempt;uint mark=markerSeq;
            var config=new Settings{Overlay=settings.Overlay,BlockInvulnerable=settings.BlockInvulnerable,InterruptChargedAttack=settings.InterruptChargedAttack,Lead=settings.Lead,Range=settings.Range,Mode=settings.Mode};
            ThreadPool.QueueUserWorkItem(delegate {
                GameRuntime.ProbeResult probe=null;Channel next=null;GameDiscovery.Candidate runningInstallation=null;string error=null;bool loaded=false,reused=false,attempted=false,healthy=false;
                try {
                    probe=GameRuntime.Probe();anyGameProcess=probe.AnyProcess;if(probe.AnyProcess)CancelDiscovery();
                    if(probe.Selected!=null) {
                        if(previous==null){string reason;GameDiscovery.ValidateFolder(probe.Selected.ExePath,out runningInstallation,out reason);}
                        bool same=GameRuntime.Same(previous,probe.Selected);
                        if(same&&existing!=null)try{var current=existing.ReadHud();healthy=current!=null&&current.Attached!=0&&HeartbeatAlive(unchecked((uint)Environment.TickCount),current.NativeTick);}catch(ObjectDisposedException){}
                        if(!healthy&&(!same||allowAttempt)) {
                            attempted=true;var selected=probe.Selected;string verifyError;
                            if(!GameRuntime.Verify(selected,out verifyError))throw new InvalidOperationException(verifyError);
                            if(!string.Equals(Native.Hash(selected.AssemblyPath),ExpectedHash,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException(I18n.Text("Эта сборка игры пока не поддерживается: GameAssembly.dll отличается от проверенной версии.","This game build is not supported: GameAssembly.dll differs from the verified build."));
                            string dll=Path.Combine(root,"ChainedGuard.Native.dll");if(!File.Exists(dll))throw new FileNotFoundException(I18n.Text("Не найден ChainedGuard.Native.dll рядом с Dodger.exe.","ChainedGuard.Native.dll was not found next to Dodger.exe."));
                            dll=NativeFiles.Prepare(dll,HostStorage.BaseFolder,HostStorage.UsesFallback);
                            using(var target=Process.GetProcessById(selected.Pid))foreach(ProcessModule module in target.Modules)if(IsGuardModuleName(module.ModuleName)){reused=true;break;}
                            if(!GameRuntime.Verify(selected,out verifyError))throw new InvalidOperationException(verifyError);
                            next=new Channel(selected.Pid,mark);next.Write(false,false,config.Overlay,true,config,false,config.BlockInvulnerable);
                            if(closing||epoch!=Volatile.Read(ref connectionEpoch))throw new InvalidOperationException(I18n.Text("Подключение отменено.","Connection canceled."));
                            loaded=reused||Native.Inject(selected.Pid,dll,out error);
                            if(!GameRuntime.Verify(selected,out verifyError)){loaded=false;throw new InvalidOperationException(verifyError);}
                            if(!loaded && next!=null){next.Dispose();next=null;}
                        }
                    }
                }catch(Exception ex){error=ex.Message;loaded=false;if(next!=null){next.Dispose();next=null;}}
                var result=probe;var owned=next;
                if(attempted)hostLog.Write("attach_result","game_pid",probe==null||probe.Selected==null?0:probe.Selected.Pid,"success",loaded,"existing_module",reused,"error",error);
                Post(delegate {
                    if(closing||epoch!=connectionEpoch){if(owned!=null)owned.Dispose();working=false;attach.IsEnabled=true;return;}
                    working=false;attach.IsEnabled=true;
                    if(result==null){detailText.Text=error??I18n.Text("Не удалось проверить игру.","Could not check the game.");return;}
                    if(result.Selected==null) {
                        if(!result.AnyProcess&&!string.IsNullOrEmpty(result.Diagnostic)){detailText.Text=result.Diagnostic;return;}
                        if(game!=null)Disconnect();
                        detailText.Text=result.Diagnostic??I18n.Text("Запусти Chained Beasts — подключимся автоматически.","Start Chained Beasts to connect automatically.");
                        if(!result.AnyProcess&&!discoveryAttempted)BeginDiscovery(false);return;
                    }
                    bool switched=!GameRuntime.Same(game,result.Selected);
                    if(switched){int old=game==null?0:game.Pid;Disconnect();game=result.Selected;hostLog.Write("game_process_selected","game_pid",game.Pid,"previous_pid",old,"assembly_path",game.AssemblyPath);}
                    else game=result.Selected;
                    if(owned!=null){if(channel!=null)channel.Dispose();channel=owned;lastPanelRead=DateTime.MinValue;}
                    if(healthy){connectFailures=0;return;}
                    if(loaded){nextConnectAttempt=DateTime.UtcNow.AddSeconds(15);detailText.Text=I18n.Text("Модуль загружен. Ожидаем игровой сигнал…","Module loaded. Waiting for a game signal…");}
                    else if(attempted){connectFailures++;nextConnectAttempt=DateTime.UtcNow.AddSeconds(RetryDelaySeconds(connectFailures));detailText.Text=error??I18n.Text("Подключение не завершено.","Connection did not complete.");}
                    if(installation==null&&runningInstallation!=null)SelectInstallation(runningInstallation);
                },delegate{if(owned!=null)owned.Dispose();});
            });
        }
        private void UpdateSnapshot(Snapshot s,bool focus) {
            bool alive=s.Attached!=0 && HeartbeatAlive(unchecked((uint)Environment.TickCount),s.NativeTick);
            bool player=alive && s.State==2;
            string nativeSignature=alive+"|"+s.State+"|"+s.NativeBuild+"|"+s.NativeRevision+"|"+s.TraceReady+"|"+(s.State==3?s.Status:"");
            if(nativeSignature!=lastNativeSignature){lastNativeSignature=nativeSignature;hostLog.Write("native_state","alive",alive,"state",s.State,"player_found",player,"native_build",s.NativeBuild,"native_revision",s.NativeRevision,"trace_ready",s.TraceReady,"dropped_log_records",s.DroppedLogRecords,"status",s.Status);}
            connectionText.Text=alive?I18n.Text("●  Подключено","●  Connected"):I18n.Text("○  Ожидаем модуль","○  Waiting for module");connectionText.Foreground=Brush(alive?"#C5DDA1":"#94A2A7");
            playerText.Text=player?I18n.Text("●  Герой найден","●  Player found"):I18n.Text("○  Герой не найден","○  Player not found");playerText.Foreground=Brush(player?"#C5DDA1":"#94A2A7");
            signalText.Text=player?(s.Threats>0?I18n.Text("●  Атака рядом","●  Incoming attack"):I18n.Text("●  Сигнал активен","●  Signal active")):I18n.Text("○  Сигнал не получен","○  No signal received");signalText.Foreground=Brush(player?(s.Threats>0?"#F4A087":"#C5DDA1"):"#94A2A7");
            stateText.Text=s.State==3?I18n.Text("ОШИБКА ПОДКЛЮЧЕНИЯ","CONNECTION ERROR"):(!alive?I18n.Text("ОЖИДАНИЕ ПОДКЛЮЧЕНИЯ","WAITING FOR CONNECTION"):(player?I18n.Text("В ИГРЕ","IN GAME"):I18n.Text("ПОДКЛЮЧЕНО","CONNECTED")));stateText.Foreground=Brush(s.State==3?"#FFA891":(player?"#D9EAC0":"#BEC9CD"));statePill.Background=Brush(player?"#2D3B29":"#283338");
            if(s.State==3 && !string.IsNullOrWhiteSpace(s.Status))detailText.Text=s.Status;
            else detailText.Text=alive&&NeedsNativeRestart(s.NativeBuild,s.NativeRevision)?I18n.Text("Обновление ещё не применено. Полностью закрой и заново запусти игру и Dodger.","The update has not been applied yet. Fully close and restart the game and Dodger."):(!alive?I18n.Text("Ожидаем ответ игрового модуля.","Waiting for the game module."):(player?I18n.Text("Красный контур — угроза. Зелёный сонар — полный блок без урона.","Red outline: incoming threat. Green sonar: a full block without damage."):I18n.Text("Подключено. Ожидаем появления героя в бою.","Connected. Waiting for a player in combat.")));
            if(alive&&NeedsNativeRestart(s.NativeBuild,s.NativeRevision)){stateText.Text=I18n.Text("ПЕРЕЗАПУСТИ ИГРУ","RESTART THE GAME");stateText.Foreground=Brush("#EBC078");statePill.Background=Brush("#403723");}
            if(!string.IsNullOrWhiteSpace(s.Info))infoText.Text=s.Info;
            threatsValue.Text=player?s.Threats.ToString():"—";blocksValue.Text=alive?s.Blocks.ToString():"—";dodgesValue.Text=alive?s.Dodges.ToString():"—";
            projectileDodgesValue.Text=alive&&s.NativeBuild>=2?s.ConfirmedProjectileDodges.ToString():"—";
            traceStateText.Text=TraceDescription(alive,s.NativeBuild,s.TraceReady,s.DroppedLogRecords);
            if(alive&&NeedsNativeRestart(s.NativeBuild,s.NativeRevision))traceStateText.Text+=I18n.Text(" Для последнего исправления защиты перезапустите игру."," Restart the game to apply the latest defense fix.");
            traceStateText.Foreground=Brush(alive&&(s.NativeBuild<6 || NeedsNativeRestart(s.NativeBuild,s.NativeRevision) || s.TraceReady==2 || s.DroppedLogRecords>0)?"#D4AC75":(alive&&s.TraceReady==1?"#A9C991":"#6F8992"));
            // The native overlay reads the same channel at presentation cadence, independently of this settings timer.
        }
        private ThreatWindow.HudFrame ReadHudFrame() {
            var binding=System.Threading.Volatile.Read(ref hudBinding);
            var frame=new ThreatWindow.HudFrame{GamePid=binding==null?0:binding.GamePid};
            if(binding==null || binding.Channel==null || binding.GamePid==0){frame.GamePid=0;frame.Suppressed=true;return frame;}
            if(!binding.Enabled||!Native.ForegroundIs(binding.GamePid)){frame.Suppressed=true;return frame;}
            try {
                var s=binding.Channel.ReadHud();if(s==null)return null;
                frame.Frame=s.Frame;frame.X=s.NativeBuild>=6?s.X:s.X-2;frame.Y=s.NativeBuild>=6?s.Y:s.Y+16;frame.Visible=s.Visible;frame.EtaMs=s.AttackMs;
                frame.Allowed=binding.Enabled&&Native.ForegroundIs(binding.GamePid)&&s.State==2&&s.Attached!=0&&HeartbeatAlive(unchecked((uint)Environment.TickCount),s.NativeTick);
                if(!frame.Allowed||!frame.Visible||!ThreatWindow.ValidAnchor(frame.X,frame.Y)){frame.Suppressed=true;return frame;}
                frame.DodgeUnavailable=(s.HudDangerFlags&3)==3;frame.Threats=s.Threats;frame.BlockSeq=s.ConfirmedBlockTick;frame.ActualDamageTick=s.ActualDamageTick;
                frame.DashSeq=s.NativeBuild>=5?s.DashSeq:s.Dodges;frame.ConfirmedProjectileDodgeSeq=s.NativeBuild>=5?s.ConfirmedProjectileDodgeSeq:s.ConfirmedProjectileDodges;
                if(s.NativeBuild>=5 && s.HudVersion>=1){frame.HudVersion=s.HudVersion;frame.ShieldType=s.ShieldType;frame.ShieldIdentity=s.ShieldIdentity;frame.ShieldBreakIdentity=s.ShieldBreakIdentity;frame.ConfirmedBlockSeq=s.ConfirmedBlockSeq;frame.ShieldBreakSeq=s.ShieldBreakSeq;frame.HasShield=(s.ShieldFlags&1)!=0;frame.ShieldCurrentValid=(s.ShieldFlags&2)!=0;frame.ShieldMaxKnown=(s.ShieldFlags&4)!=0;frame.ShieldNoWear=(s.ShieldFlags&32)!=0;frame.ShieldCurrent=s.ShieldCurrent;frame.ShieldMax=s.ShieldMax;frame.HitsRemaining=frame.ShieldCurrentValid&&!frame.ShieldNoWear?s.ShieldCurrent:-1;}
                return frame;
            }catch(ObjectDisposedException){return null;}catch(IOException){return null;}catch(InvalidOperationException){return null;}
        }
        private void Disconnect() {
            System.Threading.Volatile.Write(ref hudBinding,null);
            hostLog.Write("disconnected","game_pid",game==null?0:game.Pid);lastNativeSignature=null;
            if(channel!=null){try{channel.Write(false,false,false,true,settings);}catch{};channel.Dispose();channel=null;}
            game=null;connectionEpoch++;overlay.Dismiss();nextConnectAttempt=DateTime.MinValue;connectFailures=0;connectionText.Text=I18n.Text("○  Подключение","○  Connection");playerText.Text=I18n.Text("○  Герой","○  Player");signalText.Text=I18n.Text("○  Сигнал атаки","○  Attack signal");stateText.Text=I18n.Text("ОЖИДАНИЕ ИГРЫ","WAITING FOR GAME");threatsValue.Text=blocksValue.Text=dodgesValue.Text=projectileDodgesValue.Text="—";traceStateText.Text=TraceDescription(false,0,0,0);detailText.Text=I18n.Text("Игра закрыта. Ожидаем следующий запуск…","The game closed. Waiting for the next launch…");
        }
        internal static bool HeartbeatAlive(uint now,uint tick) { return unchecked(now-tick)<3000; }
        internal static void CheckPanel(Action<string,bool> check) {
            string previousLanguage=I18n.Language;
            using(var request=new EventWaitHandle(false,EventResetMode.AutoReset)){
                var view=new GuardWindow(new StartupOptions(),request,true,"ru");
                try {
                    check("panel fixture isolates game, hooks, tray and workers",view.overlay==null&&view.channel==null&&view.game==null&&view.timer==null&&view.hook==IntPtr.Zero&&view.tray==null);
                    check("launcher exposes launch folder search and retry",view.launchButton!=null&&view.folderButton!=null&&view.searchButton!=null&&view.attach!=null);
                    check("path label truncates with full tooltip",view.folderText.TextTrimming==TextTrimming.CharacterEllipsis&&view.folderText.ToolTip!=null);
                    check("disabled autoblock disables invulnerable child",!view.blockInvulnerable.IsEnabled);
                    view.autoBlock.IsChecked=true;check("enabling autoblock enables child",view.blockInvulnerable.IsEnabled);
                    view.autoBlock.IsChecked=false;
                    foreach(var toggle in new[]{view.autoBlock,view.autoDodge,view.priorityDefense,view.blockInvulnerable,view.interruptCharged,view.showOverlay})AnimateToggle(toggle,false);
                    var element=(FrameworkElement)view.Content;element.Measure(new Size(500,double.PositiveInfinity));element.Arrange(new Rect(0,0,500,element.DesiredSize.Height));element.UpdateLayout();
                    var image=new RenderTargetBitmap(500,(int)Math.Ceiling(element.ActualHeight),96,96,PixelFormats.Pbgra32);image.Render(element);
                    var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(image));using(var file=File.Create(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"panel-version"+Branding.Version+".png")))encoder.Save(file);
                    check("panel headless layout fits compact 500px width",element.ActualHeight>300&&element.ActualHeight<750&&element.ActualWidth==500);
                    check("shared panel brushes are cached and frozen",ReferenceEquals(Brush("#81969E"),Brush("#81969E"))&&Brush("#81969E").IsFrozen);
                    // Language changes update the existing controls; no workers, hooks or game session exist in this fixture.
                    view.autoBlock.IsChecked=true;view.autoDodge.IsChecked=true;view.priorityDefense.IsChecked=true;view.blockInvulnerable.IsChecked=true;
                    view.interruptCharged.IsChecked=false;view.showOverlay.IsChecked=false;view.statsExpander.IsExpanded=true;
                    view.discoveryWorking=true;view.working=true;view.connectionEpoch=17;view.discoveryEpoch=23;view.selectionEpoch=31;
                    view.Left=123;view.Top=234;double beforeWidth=view.Width;var beforeContent=view.Content;var beforeExpander=view.statsExpander;
                    var retainedInstallation=new GameDiscovery.Candidate{Folder=@"Q:\Games\Chained Beasts",Store="Direct"};view.installation=retainedInstallation;
                    using(var testChannel=new Channel(Process.GetCurrentProcess().Id)) {
                        view.channel=testChannel;view.game=new GameRuntime.Identity{Pid=9876};var beforeGame=view.game;
                        view.ApplyLanguage("en");
                        check("language switch preserves automation toggles and expander",view.autoBlock.IsChecked==true&&view.autoDodge.IsChecked==true&&view.priorityDefense.IsChecked==true&&view.blockInvulnerable.IsChecked==true&&view.interruptCharged.IsChecked==false&&view.showOverlay.IsChecked==false&&view.statsExpander.IsExpanded&&ReferenceEquals(beforeExpander,view.statsExpander));
                        check("language switch retains connection discovery selection and geometry",ReferenceEquals(view.channel,testChannel)&&ReferenceEquals(view.game,beforeGame)&&ReferenceEquals(view.installation,retainedInstallation)&&view.discoveryWorking&&view.working&&view.connectionEpoch==17&&view.discoveryEpoch==23&&view.selectionEpoch==31&&ReferenceEquals(view.Content,beforeContent)&&view.Left==123&&view.Top==234&&view.Width==beforeWidth);
                        check("active discovery retains Stop action in English",(string)view.searchButton.Content=="Stop");
                        view.channel=null;view.game=null;
                    }
                    check("English labels and tooltips contain no Cyrillic",!HasCyrillicLabels((DependencyObject)view.Content));
                    view.detailText.Text="Версия игры не поддержана: событие взрыва изменилось";
                    check("late native Russian fault is translated at UI boundary",view.detailText.Text=="Unsupported game build: explosion event changed");
                    view.detailText.Text="Не удалось загрузить модуль в игру.";
                    check("late worker error is translated at UI boundary",view.detailText.Text=="Could not load the module into the game.");
                    view.folderText.Text=@"R:\Игры\Найти\Chained Beasts";
                    check("localized UI preserves Cyrillic game paths exactly",view.folderText.Text==@"R:\Игры\Найти\Chained Beasts");
                    view.ApplyLanguage("ru");
                    check("active discovery retains Stop action after switching back",(string)view.searchButton.Content=="Стоп"&&view.settings.Language=="ru"&&view.statsExpander.IsExpanded);
                    view.discoveryWorking=false;view.working=false;view.installation=null;view.statsExpander.IsExpanded=false;
                    view.autoBlock.IsChecked=false;view.autoDodge.IsChecked=false;view.priorityDefense.IsChecked=false;view.blockInvulnerable.IsChecked=false;view.interruptCharged.IsChecked=true;view.showOverlay.IsChecked=true;
                    view.detailText.Text=I18n.Text("Запустите Chained Beasts — подключение произойдёт автоматически.","Start Chained Beasts to connect automatically.");
                    view.SetFolderMessage(I18n.Text("Папка игры будет найдена автоматически.","The game folder will be found automatically."));
                    foreach(string language in new[]{"ru","en"}) {
                        view.ApplyLanguage(language);
                        foreach(var toggle in new[]{view.autoBlock,view.autoDodge,view.priorityDefense,view.blockInvulnerable,view.interruptCharged,view.showOverlay})AnimateToggle(toggle,false);
                        element.Measure(new Size(500,double.PositiveInfinity));element.Arrange(new Rect(0,0,500,element.DesiredSize.Height));element.UpdateLayout();
                        var localizedImage=new RenderTargetBitmap(500,(int)Math.Ceiling(element.ActualHeight),96,96,PixelFormats.Pbgra32);localizedImage.Render(element);
                        var localizedEncoder=new PngBitmapEncoder();localizedEncoder.Frames.Add(BitmapFrame.Create(localizedImage));using(var file=File.Create(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"panel-version"+Branding.Version+"-"+language+".png")))localizedEncoder.Save(file);
                        check(language+" localized compact panel renders at 500px",element.ActualWidth==500&&element.ActualHeight>300&&element.ActualHeight<750);
                    }
                    view.ApplyLanguage("ru");

                    var steam=new GameDiscovery.Candidate{Folder=@"Q:\SteamLibrary\steamapps\common\Chained Beasts",Store="Steam",SteamAppId="3569420",LaunchUri="steam://rungameid/3569420"};
                    view.SelectInstallation(steam);view.SelectInstallation(new GameDiscovery.Candidate{Folder=steam.Folder,Store="Direct"});
                    check("reselecting same Steam folder preserves verified launch metadata",view.installation.Store=="Steam"&&view.installation.LaunchUri==steam.LaunchUri);
                    view.SelectInstallation(new GameDiscovery.Candidate{Folder=@"R:\Игры\Chained Beasts",Store="Direct"});
                    check("changing to a different folder drops previous Steam metadata",view.installation.Store=="Direct"&&view.installation.LaunchUri==null);
                }finally{view.hostLog.Dispose();view.Close();I18n.Language=previousLanguage;}
            }
        }
        private static bool HasCyrillicLabels(DependencyObject node) {
            var text=node as TextBlock;if(text!=null&&Tests.HasCyrillic(text.Text))return true;
            var control=node as ContentControl;if(control!=null&&control.Content is string&&Tests.HasCyrillic((string)control.Content))return true;
            var header=node as HeaderedContentControl;if(header!=null&&header.Header is string&&Tests.HasCyrillic((string)header.Header))return true;
            var element=node as FrameworkElement;if(element!=null&&element.ToolTip is string&&Tests.HasCyrillic((string)element.ToolTip))return true;
            foreach(object child in LogicalTreeHelper.GetChildren(node)){var dependency=child as DependencyObject;if(dependency!=null&&HasCyrillicLabels(dependency))return true;}
            return false;
        }
        internal static bool AutomationPaused(bool gameFocused) { return !gameFocused; }
        internal static int RetryDelaySeconds(int failures) { return failures<=1?5:(failures==2?15:30); }
        internal static uint NextMarker(uint current) { uint next=unchecked(current+1);return next==0?1:next; }
        internal static bool NeedsNativeRestart(uint build,uint revision) { return build==6&&revision<18; }
        internal static bool IsGuardModuleName(string name) {
            const string prefix="ChainedGuard.Native.loaded-";
            return string.Equals(name,"ChainedGuard.Native.dll",StringComparison.OrdinalIgnoreCase) ||
                (!string.IsNullOrEmpty(name) && name.StartsWith(prefix,StringComparison.OrdinalIgnoreCase) && name.EndsWith(".dll",StringComparison.OrdinalIgnoreCase) && name.Length>prefix.Length+4);
        }
        internal static string TraceDescription(bool alive,uint build,uint ready,uint dropped) {
            string text=!alive?I18n.Text("Боевой журнал: ожидаем подключение.","Combat log: waiting for connection."):(build<2?I18n.Text("Для обновлённого мода и боевого журнала перезапустите игру.","Restart the game to update the mod and combat log."):(ready==1?I18n.Text("Боевой журнал включён.","Combat log enabled."):(ready==2?I18n.Text("Ошибка записи боевого журнала.","Combat log write error."):I18n.Text("Боевой журнал запускается…","Combat log is starting…"))));
            if(alive && build>=2 && build<6)text+=I18n.Text(" Для нового индикатора и расчёта защиты перезапустите игру."," Restart the game for the updated indicator and defense calculation.");
            return dropped>0?text+I18n.Text(" Пропущено записей: "," Dropped records: ")+dropped+".":text;
        }
    }
    internal static class Tests {
        public static int Run() {
            string previousLanguage=I18n.Language;I18n.Language="ru";try {
            var log=new List<string>();int failed=0;
            Action<string,bool> check=delegate(string name,bool ok){log.Add((ok?"PASS ":"FAIL ")+name);if(!ok)failed++;};
            CheckLocalization(check);
            check("flags disabled+paused",Channel.Flags(false,false,true,true)==12);
            check("native version restart warning",GuardWindow.NeedsNativeRestart(6,0)&&GuardWindow.NeedsNativeRestart(6,1)&&GuardWindow.NeedsNativeRestart(6,2)&&GuardWindow.NeedsNativeRestart(6,3)&&GuardWindow.NeedsNativeRestart(6,4)&&GuardWindow.NeedsNativeRestart(6,5)&&GuardWindow.NeedsNativeRestart(6,6)&&GuardWindow.NeedsNativeRestart(6,7)&&GuardWindow.NeedsNativeRestart(6,8)&&GuardWindow.NeedsNativeRestart(6,9)&&GuardWindow.NeedsNativeRestart(6,10)&&GuardWindow.NeedsNativeRestart(6,11)&&GuardWindow.NeedsNativeRestart(6,12)&&GuardWindow.NeedsNativeRestart(6,13)&&GuardWindow.NeedsNativeRestart(6,14)&&GuardWindow.NeedsNativeRestart(6,15)&&GuardWindow.NeedsNativeRestart(6,16)&&GuardWindow.NeedsNativeRestart(6,17)&&!GuardWindow.NeedsNativeRestart(6,18)&&!GuardWindow.NeedsNativeRestart(7,0));
            check("flags both actions",Channel.Flags(true,true,true,false)==7);
            check("game focus alone controls automation pause",!GuardWindow.AutomationPaused(true)&&GuardWindow.AutomationPaused(false));
            check("optional priority defense bit",Channel.Flags(false,false,false,false,true)==16 && Channel.Flags(true,true,true,true,true)==31);
            check("invulnerable blocking bit defaults off",Channel.Flags(false,false,false,false)==0&&Channel.Flags(false,false,false,false,false,true)==0&&Channel.Flags(true,false,false,false,false,true)==33&&Channel.Flags(true,true,true,true,true,true)==63);
            var savedInvulnerable=new Settings{Overlay=false,BlockInvulnerable=true,InterruptChargedAttack=false,GameFolder=@"Z:\Игры & тест\Chained Beasts"};var roundtrip=Settings.FromXml(XElement.Parse(savedInvulnerable.ToXml().ToString()));check("invulnerable choice XML roundtrip and old defaults",roundtrip.BlockInvulnerable&&!roundtrip.Overlay&&!Settings.FromXml(new XElement("ChainedGuard")).BlockInvulnerable);
            check("game folder XML escaping and legacy default",roundtrip.GameFolder==savedInvulnerable.GameFolder&&Settings.FromXml(new XElement("ChainedGuard")).GameFolder==null);
            check("charged interruption XML roundtrip and legacy default",!roundtrip.InterruptChargedAttack&&Settings.FromXml(new XElement("ChainedGuard")).InterruptChargedAttack);check("charged interruption command bit",Channel.Flags(false,false,false,false,false,false,true)==64);
            var defaults=StartupOptions.Parse(new string[0]);check("startup defaults disabled",!defaults.Block&&!defaults.Dodge&&!defaults.Minimized&&!defaults.Preview&&!defaults.BlockInvulnerable&&!defaults.PriorityDefense);
            GuardWindow.CheckPanel(check);
            var explicitOptions=StartupOptions.Parse(new string[]{"--block","--dodge","--minimized","--block-invulnerable","--priority-defense"});check("startup explicit options",explicitOptions.Block&&explicitOptions.Dodge&&explicitOptions.Minimized&&!explicitOptions.Preview&&explicitOptions.BlockInvulnerable&&explicitOptions.PriorityDefense);
            var previewOptions=StartupOptions.Parse(new string[]{"--preview","--block","--dodge","--minimized","--block-invulnerable","--priority-defense"});check("preview isolates automation",previewOptions.Preview&&!previewOptions.Block&&!previewOptions.Dodge&&!previewOptions.Minimized&&!previewOptions.BlockInvulnerable&&!previewOptions.PriorityDefense);
            check("range clamp low",Settings.Clamp(-9,1,12)==1);
            check("range clamp high",Settings.Clamp(99,1,12)==12);
            check("NaN clamp",Settings.Clamp(double.NaN,1,12)==1);
            check("overlay valid anchor",ThreatWindow.ValidAnchor(400,250));check("overlay invalid anchor",!ThreatWindow.ValidAnchor(float.NaN,250));
            Point hud=ThreatWindow.HudOrigin(532,376,1,1);check("HUD native center has no hero offset",hud.X+ThreatWindow.CanvasCenter==532 && hud.Y+ThreatWindow.CanvasCenter==376);
            Point hudDpi=ThreatWindow.HudOrigin(532,376,1.5,1.5);check("HUD DPI preserves native bar center",hudDpi.X+ThreatWindow.CanvasCenter*1.5==532 && hudDpi.Y+ThreatWindow.CanvasCenter*1.5==376);
            check("connection retry bounded",GuardWindow.RetryDelaySeconds(1)==5&&GuardWindow.RetryDelaySeconds(2)==15&&GuardWindow.RetryDelaySeconds(50)==30);
            check("loaded bridge recognized after binary replacement",GuardWindow.IsGuardModuleName("ChainedGuard.Native.dll")&&GuardWindow.IsGuardModuleName("CHAINEDGUARD.NATIVE.DLL")&&GuardWindow.IsGuardModuleName("ChainedGuard.Native.loaded-rev2.dll"));
            check("unrelated modules do not count as bridge",!GuardWindow.IsGuardModuleName(null)&&!GuardWindow.IsGuardModuleName("GameAssembly.dll")&&!GuardWindow.IsGuardModuleName("ChainedGuard.Native.loaded-.dll")&&!GuardWindow.IsGuardModuleName("Other.ChainedGuard.Native.dll")&&!GuardWindow.IsGuardModuleName("ChainedGuard.Native.loaded-rev2.exe"));
            check("marker IDs increment and skip zero",GuardWindow.NextMarker(20)==21&&GuardWindow.NextMarker(uint.MaxValue)==1);
            check("native trace readiness and build version stay honest",GuardWindow.TraceDescription(true,0,0,0).Contains("перезапустите")&&GuardWindow.TraceDescription(true,2,1,0).Contains("включён")&&GuardWindow.TraceDescription(true,2,1,0).Contains("перезапустите")&&GuardWindow.TraceDescription(true,5,1,0).Contains("перезапустите")&&!GuardWindow.TraceDescription(true,6,1,0).Contains("перезапустите")&&GuardWindow.TraceDescription(true,2,2,4).Contains("Ошибка")&&GuardWindow.TraceDescription(true,2,2,4).Contains("4"));
            var processCandidates=new List<GameSelection.Candidate>{new GameSelection.Candidate{Pid=43156,Started=1,HasWindow=false,EngineLoaded=true,Foreground=false},new GameSelection.Candidate{Pid=48736,Started=2,HasWindow=true,EngineLoaded=true,Foreground=false}};
            check("attach real game window instead of earlier windowless stub",GameSelection.Pick(processCandidates)==48736);
            processCandidates.Add(new GameSelection.Candidate{Pid=3,Started=3,HasWindow=true,EngineLoaded=false,Foreground=true});
            check("window without loaded engine cannot receive bridge",GameSelection.Pick(processCandidates)==48736);
            processCandidates.Add(new GameSelection.Candidate{Pid=4,Started=4,HasWindow=true,EngineLoaded=true,Foreground=false});processCandidates[1].Foreground=true;
            check("foreground playable game wins over newer background instance",GameSelection.Pick(processCandidates)==48736);
            check("no playable window waits without injecting",GameSelection.Pick(new List<GameSelection.Candidate>{processCandidates[0],processCandidates[2]})==0);
            check("native heartbeat fresh",GuardWindow.HeartbeatAlive(5000,4500));
            check("native heartbeat stale",!GuardWindow.HeartbeatAlive(8000,4500));
            check("native heartbeat tick wrap",GuardWindow.HeartbeatAlive(20,uint.MaxValue-20));
            CheckLogger(check);
            CheckPanelRequest(check);
            CheckBranding(check);
            check("foreground grant allows only same-install legacy transition",Native.CompatibleHostPath(@"C:\app\ChainedGuard.exe",@"C:\app\Dodger.exe")&&!Native.CompatibleHostPath(@"C:\elsewhere\ChainedGuard.exe",@"C:\app\Dodger.exe")&&!Native.CompatibleHostPath(@"C:\app\other.exe",@"C:\app\Dodger.exe"));
            using(var c=new Channel(Process.GetCurrentProcess().Id,123)) {
                var settings=new Settings();c.Write(false,false,true,true,settings);
                check("protocol magic",c.View.ReadUInt32(0)==Channel.Magic);check("protocol version",c.View.ReadUInt32(4)==1);check("config coherent",(c.View.ReadUInt32(8)&1)==0);check("automation disabled",(c.View.ReadUInt32(12)&3)==0);check("paused initial",(c.View.ReadUInt32(12)&8)!=0);check("lead default",c.View.ReadSingle(16)==90);check("range default",c.View.ReadSingle(20)==5);check("heartbeat",c.View.ReadUInt32(28)!=0);check("host marker protocol",c.View.ReadUInt32(100)==123);
                c.View.Write(32,(uint)2);c.View.Write(36,(uint)2);c.View.Write(44,(uint)3);c.View.Write(56,123.5f);c.View.Write(60,444.25f);c.View.Write(64,1);c.View.Write(84,(uint)123456);c.View.Write(88,(uint)3456);c.View.Write(92,(uint)7);c.View.Write(96,(uint)7890);c.View.Write(104,(uint)3);c.View.Write(108,(uint)4);c.View.Write(112,(uint)1);c.View.Write(116,(uint)2);byte[] bytes=Encoding.UTF8.GetBytes("Проверка сигнала\0");c.View.WriteArray(128,bytes,0,bytes.Length);var s=c.Read();check("telemetry coherent",s!=null&&s.State==2&&s.Threats==3&&s.Visible&&s.X==123.5f&&s.Y==444.25f);check("native heartbeat protocol",s.NativeTick==123456);check("confirmed block event protocol",s.ConfirmedBlockTick==3456);check("actual damage protocol",s.ActualDamage==7);check("actual damage event protocol",s.ActualDamageTick==7890);check("extended native diagnostics protocol",s.ConfirmedProjectileDodges==3&&s.DroppedLogRecords==4&&s.TraceReady==1&&s.NativeBuild==2);check("UTF8 telemetry",s.Status=="Проверка сигнала");
                c.View.Write(116,(uint)5);c.View.Write(120,(uint)12);c.View.Write(124,(uint)7);c.View.Write(1536,(uint)1);c.View.Write(1540,(uint)7);c.View.Write(1544,202);c.View.Write(1548,2);c.View.Write(1552,4);c.View.Write(1556,(uint)42);c.View.Write(1560,(uint)55);c.View.Write(1564,(uint)1);
                var hudSnapshot=c.ReadHud();check("HUD5 coherent shield and repeated dash protocol without text allocation",hudSnapshot!=null&&hudSnapshot.HudVersion==1&&hudSnapshot.DashSeq==12&&hudSnapshot.ConfirmedProjectileDodgeSeq==7&&hudSnapshot.ShieldFlags==7&&hudSnapshot.ShieldType==202&&hudSnapshot.ShieldCurrent==2&&hudSnapshot.ShieldMax==4&&hudSnapshot.DashTick==42&&hudSnapshot.ConfirmedProjectileDodgeTick==55&&hudSnapshot.ShieldMaxSource==1&&hudSnapshot.Status==null&&hudSnapshot.Info==null);
                c.View.Write(116,(uint)6);c.View.Write(1568,(uint)4);var currentNative=c.ReadHud();check("native6 revision protocol",currentNative!=null&&currentNative.NativeBuild==6&&currentNative.NativeRevision==4);c.View.Write(1568,(uint)8);c.View.Write(1572,(uint)3);var dangerSnapshot=c.ReadHud();check("version8 coherent HUD dodge availability",dangerSnapshot!=null&&dangerSnapshot.NativeRevision==8&&dangerSnapshot.HudDangerFlags==3);
                c.View.Write(1568,(uint)16);c.View.Write(1536,(uint)2);c.View.Write(1580,(uint)3);c.View.Write(1584,(uint)9);c.View.Write(1592,0x100000001UL);c.View.Write(1600,0x200000002UL);var shieldFeedback=c.ReadHud();check("HUD2 authoritative full block and shield break identity",shieldFeedback!=null&&shieldFeedback.HudVersion==2&&shieldFeedback.ConfirmedBlockSeq==3&&shieldFeedback.ShieldBreakSeq==9&&shieldFeedback.ShieldIdentity==0x100000001UL&&shieldFeedback.ShieldBreakIdentity==0x200000002UL&&shieldFeedback.Status==null);
                c.View.Write(32,(uint)3);check("incomplete snapshot rejected",c.Read()==null&&c.ReadHud()==null);
            }
            log.Add("Result: "+(failed==0?"PASS":"FAIL")+"; failures="+failed+". No game process opened; no injection; no input sent.");
            File.WriteAllLines(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"self-test.txt"),log,Encoding.UTF8);return failed==0?0:2;
            }finally{I18n.Language=previousLanguage;}
        }
        internal static bool HasCyrillic(string value) { if(value==null)return false;foreach(char ch in value)if(ch>='\u0400'&&ch<='\u04ff')return true;return false; }
        private static void CheckLocalization(Action<string,bool> check) {
            string previous=I18n.Language;try {
                check("OS language default is Russian only for Russian cultures",I18n.LanguageForCulture(new CultureInfo("ru-RU"))=="ru"&&I18n.LanguageForCulture(new CultureInfo("en-US"))=="en"&&I18n.LanguageForCulture(new CultureInfo("de-DE"))=="en");
                check("language values normalize and unsupported values fall back",I18n.Normalize(" EN-us ")=="en"&&I18n.Normalize("Ru-RU")=="ru"&&I18n.Normalize("invalid")==I18n.DefaultLanguage);
                check("new settings use OS default and legacy settings remain Russian",new Settings().Language==I18n.DefaultLanguage&&Settings.FromXml(new XElement("ChainedGuard")).Language=="ru");
                var original=new Settings{Language="en-US",Overlay=false,BlockInvulnerable=true,InterruptChargedAttack=false,GameFolder=@"R:\Игры & тест\Chained Beasts"};var loaded=Settings.FromXml(XElement.Parse(original.ToXml().ToString()));
                check("language XML roundtrip preserves all prior saved settings",loaded.Language=="en"&&!loaded.Overlay&&loaded.BlockInvulnerable&&!loaded.InterruptChargedAttack&&loaded.GameFolder==original.GameFolder);
                loaded.Language="ru";check("Russian language XML roundtrip",Settings.FromXml(loaded.ToXml()).Language=="ru");
                I18n.Language="en";
                check("native faults and composed trace text are English",I18n.Translate("Защита остановлена: ошибка доступа к объекту")=="Defense stopped: object access error"&&!HasCyrillic(I18n.Translate("Боевой журнал включён. Для нового индикатора и расчёта защиты перезапустите игру. Пропущено записей: 4.")));
                string raw=@"R:\Игры\Найти\Ошибка записи боевого журнала.";
                check("message translation preserves paths and OS error payloads",I18n.Translate(raw)==raw&&I18n.Translate("Канал недоступен: "+raw)=="Channel unavailable: "+raw&&I18n.Translate("Не удалось открыть папку журнала: Доступ запрещён ОС")=="Could not open the log folder: Доступ запрещён ОС");
                check("unknown game messages remain verbatim",I18n.Translate("Произвольный текст движка")=="Произвольный текст движка");
                check("native formatted diagnostics translate without changing values",I18n.Translate("Мод в игре | щит: есть | защита: пауза | кадры: 12345")=="Module in game | shield: yes | defense: paused | frames: 12345");
                I18n.Language="ru";string oldError=new ArgumentException(I18n.Text("Проверяем выбранную папку…","Checking the selected folder…"),"candidate").Message;
                I18n.Language="en";int payloadStart=oldError.IndexOf('\n');string translatedError=I18n.Translate(oldError);
                check("late ArgumentException message translates only known app prefix",translatedError.StartsWith("Checking the selected folder…",StringComparison.Ordinal)&&payloadStart>=0&&translatedError.Substring(translatedError.IndexOf('\n'))==oldError.Substring(payloadStart));
                check("stale search progress translates exact message but preserves path",I18n.Translate("Поиск: Проверяем выбранную папку…")=="Searching: Checking the selected folder…"&&I18n.Translate("Поиск: "+raw)=="Searching: "+raw);

                var fallback=new HostStorage.Location(@"Q:\fixture",true,true);string enDiagnostic=fallback.Diagnostic;
                GameDiscovery.Candidate candidate;string reason;GameDiscovery.ValidateFolder(null,out candidate,out reason);
                check("helper validation errors and storage fallback follow English locale",!HasCyrillic(reason)&&!HasCyrillic(enDiagnostic));
                I18n.Language="ru";check("storage fallback language updates after initialization",HasCyrillic(fallback.Diagnostic)&&fallback.Diagnostic!=enDiagnostic);
                int failures=0;var threads=new Thread[4];for(int i=0;i<threads.Length;i++){threads[i]=new Thread(delegate(){try{for(int j=0;j<100;j++){string text=I18n.Text("Тест потока","Thread test");if(text!="Тест потока"&&text!="Thread test")Interlocked.Increment(ref failures);}}catch{Interlocked.Increment(ref failures);}});threads[i].Start();}foreach(var thread in threads)thread.Join();check("localization registration and worker reads are thread safe",failures==0);
            }finally{I18n.Language=previous;}
        }
        private static void CheckLogger(Action<string,bool> check) {
            string work=Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"..","..","work"));
            Directory.CreateDirectory(work);string filename="host-log-selftest-"+Guid.NewGuid().ToString("N")+".jsonl",fixture=Path.Combine(work,filename);
            try {
                string payload="Кириллица \"quote\" \\ path\r\nline\t\0";
                using(var recorder=new HostLog(work,filename)) {
                    recorder.Write("fixture","text",payload,"enabled",false,"empty",null,"number",1.25);
                    var parserType=Type.GetType("System.Web.Script.Serialization.JavaScriptSerializer, System.Web.Extensions, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",true);
                    object parser=Activator.CreateInstance(parserType);var deserialize=parserType.GetMethod("DeserializeObject",new Type[]{typeof(string)});
                    string[] first=ReadSharedLines(fixture);
                    var row=(Dictionary<string,object>)deserialize.Invoke(parser,new object[]{first[0]});
                    DateTime utc;bool validUtc=DateTime.TryParse((string)row["utc"],CultureInfo.InvariantCulture,DateTimeStyles.RoundtripKind,out utc)&&utc.Kind==DateTimeKind.Utc;
                    check("logger UTF8 JSON roundtrip and immediate flush",first.Length==1 && (string)row["text"]==payload && !(bool)row["enabled"] && row["empty"]==null && Convert.ToDouble(row["number"],CultureInfo.InvariantCulture)==1.25 && validUtc && row.ContainsKey("tick_ms") && row.ContainsKey("session"));
                    var writers=new Thread[4];for(int i=0;i<writers.Length;i++){int id=i;writers[i]=new Thread(delegate(){for(int j=0;j<16;j++)recorder.Write("parallel","worker",id,"item",j);});writers[i].Start();}foreach(var thread in writers)thread.Join();
                    string[] lines=ReadSharedLines(fixture);bool complete=lines.Length==65;foreach(string line in lines){var parsed=(Dictionary<string,object>)deserialize.Invoke(parser,new object[]{line});complete=complete&&parsed.ContainsKey("event");}
                    check("logger concurrent events remain complete JSON lines",complete && recorder.Error==null);
                }
                using(var append=new HostLog(work,filename))append.Write("append");
                check("logger appends existing daily file",File.ReadAllLines(fixture,Encoding.UTF8).Length==66);
                using(var unavailable=new HostLog(fixture)){unavailable.Write("ignored");check("logger write failure stays nonfatal",unavailable.Error!=null);}
            }catch(Exception ex){check("logger integration: "+ex.GetType().Name+" "+ex.Message,false);}
            finally{try{if(File.Exists(fixture))File.Delete(fixture);}catch{}}
        }
        private static void CheckPanelRequest(Action<string,bool> check) {
            string name="Local\\ChainedGuard.PanelRequestSelfTest."+Guid.NewGuid().ToString("N");
            try {
                bool absent=!PanelRequest.Notify(name,1,0);int received=0;
                using(var signal=new EventWaitHandle(false,EventResetMode.AutoReset,name))using(var done=new ManualResetEvent(false))using(var retired=new ManualResetEvent(false)) {
                    var registration=PanelRequest.Listen(signal,delegate{Interlocked.Increment(ref received);done.Set();});
                    bool first=PanelRequest.Notify(name,1,0)&&done.WaitOne(1000);done.Reset();
                    bool second=PanelRequest.Notify(name,1,0)&&done.WaitOne(1000);
                    bool released=registration.Unregister(retired)&&retired.WaitOne(1000);
                    check("second-launch IPC repeats and unregisters without UI",absent&&first&&second&&received==2&&released);
                }
            }catch(Exception ex){check("second-launch IPC: "+ex.GetType().Name,false);}
        }
        private static void CheckBranding(Action<string,bool> check) {
            try {
                using(var stream=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("Dodger.Icon"))using(var reader=new BinaryReader(stream))using(var tray=Branding.TrayIcon()) {
                    bool header=reader.ReadUInt16()==0&&reader.ReadUInt16()==1&&reader.ReadUInt16()==6;
                    var source=(BitmapSource)Branding.WindowIcon();
                    check("embedded Dodger icon decodes for window and tray",header&&source.PixelWidth==256&&source.PixelHeight==256&&tray.Width==32&&tray.Height==32);
                }
            }catch(Exception ex){check("embedded Dodger icon: "+ex.GetType().Name,false);}
        }
        private static string[] ReadSharedLines(string path) { using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))using(var reader=new StreamReader(stream,Encoding.UTF8)){return reader.ReadToEnd().Split(new string[]{"\r\n","\n"},StringSplitOptions.RemoveEmptyEntries);} }
    }
}

