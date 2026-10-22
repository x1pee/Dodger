using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security;

namespace ChainedGuard {
    // Read-only process discovery. Call Probe/Capture/Verify from a worker;
    // module enumeration and process metadata can block on the OS.
    internal static class GameRuntime {
        internal sealed class Identity {
            public int Pid;
            public long StartedUtcTicks;
            public string ExePath,AssemblyPath;
            public IntPtr Window;
            public bool Foreground;
        }

        internal sealed class ProbeResult {
            public Identity Selected;
            public bool AnyProcess;
            public string Diagnostic;
        }

        private const string ProcessName="ChainedBeasts";
        private const string ExecutableName="ChainedBeasts.exe";
        private const string AssemblyName="GameAssembly.dll";
        private const string AccessDeniedMessage="Нет доступа к процессу Chained Beasts. Запустите Dodger с теми же правами, что и игру.";
        private const string ChangedMessage="Игровой процесс изменился или ещё не готов. Ожидаем повторного подключения.";

        [DllImport("user32.dll")]
        private static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);

        internal static string NormalizePath(string path) {
            if(string.IsNullOrWhiteSpace(path))return null;
            try {
                string normalized=Path.GetFullPath(path.Replace('/',Path.DirectorySeparatorChar));
                return normalized.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar);
            }catch(ArgumentException){return null;}
            catch(NotSupportedException){return null;}
            catch(PathTooLongException){return null;}
            catch(SecurityException){return null;}
        }

        private static bool SamePath(string a,string b) {
            string first=NormalizePath(a),second=NormalizePath(b);
            return first!=null&&second!=null&&string.Equals(first,second,StringComparison.OrdinalIgnoreCase);
        }

        internal static bool Same(Identity a,Identity b) {
            return a!=null&&b!=null&&a.Pid>0&&a.Pid==b.Pid&&a.StartedUtcTicks>0&&a.StartedUtcTicks==b.StartedUtcTicks&&
                SamePath(a.ExePath,b.ExePath)&&SamePath(a.AssemblyPath,b.AssemblyPath);
        }

        private static bool Ready(Identity candidate) {
            if(candidate==null||candidate.Pid<=0||candidate.StartedUtcTicks<=0||candidate.Window==IntPtr.Zero)return false;
            string exe=NormalizePath(candidate.ExePath),assembly=NormalizePath(candidate.AssemblyPath);
            return exe!=null&&assembly!=null&&
                string.Equals(Path.GetFileName(exe),ExecutableName,StringComparison.OrdinalIgnoreCase)&&
                string.Equals(Path.GetFileName(assembly),AssemblyName,StringComparison.OrdinalIgnoreCase);
        }

        internal static Identity Pick(IList<Identity> candidates) {
            Identity best=null;if(candidates==null)return null;
            foreach(Identity candidate in candidates) {
                if(!Ready(candidate))continue;
                if(best==null||(candidate.Foreground&&!best.Foreground)||
                    (candidate.Foreground==best.Foreground&&candidate.StartedUtcTicks>best.StartedUtcTicks))best=candidate;
            }
            return best;
        }

        // Shared with the pure fixtures: an inaccessible named process must not
        // be presented as an absent game or a successful ready candidate.
        internal static ProbeResult Complete(IList<Identity> candidates,bool anyProcess,bool accessDenied) {
            Identity selected=Pick(candidates);
            return new ProbeResult {
                Selected=selected,
                AnyProcess=anyProcess||selected!=null,
                Diagnostic=selected!=null?null:(accessDenied?AccessDeniedMessage:
                    (anyProcess?"Chained Beasts запускается. Ожидаем готовое игровое окно и движок.":null))
            };
        }

        internal static bool IsAccessDenied(Exception error) {
            if(error is UnauthorizedAccessException||error is SecurityException)return true;
            var native=error as Win32Exception;
            return native!=null&&native.NativeErrorCode==5;
        }

        // Capture never disposes its argument: the caller owns this Process.
        // A loading stub or a non-playable process is a normal null result.
        internal static Identity Capture(Process process) {
            if(process==null)throw new ArgumentNullException("process");
            process.Refresh();if(process.HasExited)return null;
            ProcessModule main=process.MainModule;
            string exe=main==null?null:NormalizePath(main.FileName);
            if(exe==null||!string.Equals(Path.GetFileName(exe),ExecutableName,StringComparison.OrdinalIgnoreCase))return null;
            IntPtr window=process.MainWindowHandle;if(window==IntPtr.Zero)return null;
            string assembly=null;
            foreach(ProcessModule module in process.Modules) {
                if(!string.Equals(module.ModuleName,AssemblyName,StringComparison.OrdinalIgnoreCase))continue;
                assembly=NormalizePath(module.FileName);break;
            }
            if(assembly==null||!string.Equals(Path.GetFileName(assembly),AssemblyName,StringComparison.OrdinalIgnoreCase))return null;
            var identity=new Identity {
                Pid=process.Id,StartedUtcTicks=process.StartTime.ToUniversalTime().Ticks,
                ExePath=exe,AssemblyPath=assembly,Window=window
            };
            uint foreground;GetWindowThreadProcessId(GetForegroundWindow(),out foreground);
            identity.Foreground=foreground==(uint)identity.Pid;
            return identity;
        }

        internal static ProbeResult Probe() {
            Process[] processes=null;var candidates=new List<Identity>();bool anyProcess=false,accessDenied=false;
            try {
                processes=Process.GetProcessesByName(ProcessName);anyProcess=processes.Length!=0;
                foreach(Process process in processes) {
                    try {Identity candidate=Capture(process);if(candidate!=null)candidates.Add(candidate);}
                    catch(Exception error) {if(IsAccessDenied(error))accessDenied=true;}
                }
                return Complete(candidates,anyProcess,accessDenied);
            }catch(Exception error) {
                var result=Complete(candidates,anyProcess,accessDenied||IsAccessDenied(error));
                if(result.Selected==null&&result.Diagnostic==null)result.Diagnostic="Не удалось проверить процесс Chained Beasts: "+error.Message;
                return result;
            }finally {
                if(processes!=null)foreach(Process process in processes)if(process!=null)process.Dispose();
            }
        }

        internal static bool Verify(Identity expected,out string error) {
            error=null;if(!Ready(expected)){error=ChangedMessage;return false;}
            try {
                using(Process process=Process.GetProcessById(expected.Pid)) {
                    Identity actual=Capture(process);
                    if(!Same(expected,actual)){error=ChangedMessage;return false;}
                }
                return true;
            }catch(Exception failure) {
                error=IsAccessDenied(failure)?AccessDeniedMessage:ChangedMessage;return false;
            }
        }
    }
}
