using System;
using System.Diagnostics;
using System.IO;
using System.Security;

namespace ChainedGuard {
    // Produces a launch request only. The caller validates the complete game
    // folder first and owns any later Process.Start call.
    internal static class GameLaunch {
        private const string SteamPrefix="steam://rungameid/";

        private static ArgumentException InvalidPath() {
            return new ArgumentException("Выбери полностью распакованную папку игры с ChainedBeasts.exe.","candidate");
        }

        private static string AbsoluteLocalPath(string path) {
            if(string.IsNullOrWhiteSpace(path)||path.Length<3||
                !((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))||
                path[1]!=':'||(path[2]!='\\'&&path[2]!='/'))throw InvalidPath();
            // Candidate paths are filesystem paths, never shell expressions,
            // quoted command lines, device namespaces or additional arguments.
            for(int i=0;i<path.Length;i++) {
                char ch=path[i];
                if(ch<32||ch=='"'||ch=='<'||ch=='>'||ch=='|'||ch=='?'||ch=='*'||(ch==':'&&i!=1))throw InvalidPath();
            }
            try {
                return Path.GetFullPath(path.Replace('/',Path.DirectorySeparatorChar));
            }catch(ArgumentException){throw InvalidPath();}
            catch(NotSupportedException){throw InvalidPath();}
            catch(PathTooLongException){throw InvalidPath();}
            catch(SecurityException){throw InvalidPath();}
        }

        private static string FolderPath(string path) {
            string full=AbsoluteLocalPath(path),root=Path.GetPathRoot(full);
            return full.Length>root.Length?full.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar):root;
        }

        private static bool PositiveDigits(string digits) {
            if(string.IsNullOrEmpty(digits))return false;bool positive=false;
            foreach(char ch in digits){if(ch<'0'||ch>'9')return false;if(ch!='0')positive=true;}
            return positive;
        }

        internal static ProcessStartInfo Plan(GameDiscovery.Candidate candidate) {
            if(candidate==null)throw new ArgumentNullException("candidate");
            string exe=AbsoluteLocalPath(candidate.ExePath),folder=FolderPath(candidate.Folder);
            if(!string.Equals(Path.GetFileName(exe),"ChainedBeasts.exe",StringComparison.OrdinalIgnoreCase)||
                !string.Equals(Path.GetDirectoryName(exe),folder,StringComparison.OrdinalIgnoreCase))throw InvalidPath();

            string launch=exe;
            if(!string.IsNullOrEmpty(candidate.LaunchUri)) {
                string uri=candidate.LaunchUri;
                if(!string.Equals(candidate.Store,"Steam",StringComparison.OrdinalIgnoreCase)||
                    !uri.StartsWith(SteamPrefix,StringComparison.Ordinal))throw InvalidSteamUri();
                string id=uri.Substring(SteamPrefix.Length);
                if(!PositiveDigits(id)||!string.Equals(id,candidate.SteamAppId,StringComparison.Ordinal))throw InvalidSteamUri();
                launch=uri;
            }
            return new ProcessStartInfo {FileName=launch,WorkingDirectory=folder,UseShellExecute=true,Arguments=string.Empty};
        }

        private static ArgumentException InvalidSteamUri() {
            return new ArgumentException("Ссылка запуска Steam не распознана. Повтори поиск папки игры.","candidate");
        }
    }
}
