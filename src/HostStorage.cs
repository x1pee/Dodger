using System;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace ChainedGuard {
    // Application data only. Game discovery paths are never supplied to this helper.
    internal static class HostStorage {
        internal sealed class Location {
            public readonly string BaseFolder,SettingsPath,LogFolder,ErrorFile,Diagnostic;
            public readonly bool UsesFallback,IsWritable;
            internal Location(string folder,bool fallback,bool writable,string diagnostic) {
                BaseFolder=folder;SettingsPath=Path.Combine(folder,"settings.xml");
                LogFolder=Path.Combine(folder,"logs");ErrorFile=Path.Combine(folder,"host-error.log");
                UsesFallback=fallback;IsWritable=writable;Diagnostic=diagnostic;
            }
        }
        private static readonly Lazy<Location> current=new Lazy<Location>(delegate {
            return Resolve(AppDomain.CurrentDomain.BaseDirectory,Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
        },LazyThreadSafetyMode.ExecutionAndPublication);

        public static string BaseFolder { get {return current.Value.BaseFolder;} }
        public static string SettingsPath { get {return current.Value.SettingsPath;} }
        public static string LogFolder { get {return current.Value.LogFolder;} }
        public static string ErrorFile { get {return current.Value.ErrorFile;} }
        public static bool UsesFallback { get {return current.Value.UsesFallback;} }
        public static bool IsWritable { get {return current.Value.IsWritable;} }
        public static string Diagnostic { get {return current.Value.Diagnostic;} }

        // An injected probe keeps permission/refusal tests deterministic and confined to fixture trees.
        internal static Location Resolve(string applicationFolder,string localAppData,Func<string,bool> probe=null) {
            string application=Normalize(applicationFolder);
            Func<string,bool> canWrite=probe??TryWritableFolder;
            if(canWrite(application))return new Location(application,false,true,null);
            string fallback=Path.Combine(Normalize(localAppData),"Dodger",CopyKey(application));
            bool writable=canWrite(fallback);
            return new Location(fallback,true,writable,writable
                ?"Папка Dodger доступна только для чтения. Настройки и журнал сохраняются в LocalAppData."
                :"Настройки и журнал недоступны для записи: нет прав ни в папке Dodger, ни в LocalAppData.");
        }
        internal static string CopyKey(string applicationFolder) {
            string normalized=Normalize(applicationFolder).ToUpperInvariant();
            using(SHA256 hash=SHA256.Create()) {
                byte[] digest=hash.ComputeHash(Encoding.UTF8.GetBytes(normalized));var key=new StringBuilder(16);
                for(int i=0;i<8;i++)key.Append(digest[i].ToString("x2",CultureInfo.InvariantCulture));return key.ToString();
            }
        }
        private static string Normalize(string folder) {
            if(string.IsNullOrWhiteSpace(folder))throw new ArgumentException("Storage directory is missing.","folder");
            string path=Path.GetFullPath(folder);string root=Path.GetPathRoot(path);
            while(path.Length>root.Length && (path[path.Length-1]==Path.DirectorySeparatorChar || path[path.Length-1]==Path.AltDirectorySeparatorChar))path=path.Substring(0,path.Length-1);
            return path;
        }
        internal static bool TryWritableFolder(string folder) {
            string file=null;bool created=false;
            try {
                Directory.CreateDirectory(folder);
                file=Path.Combine(folder,".dodger-write-test-"+Guid.NewGuid().ToString("N")+".tmp");
                using(var stream=new FileStream(file,FileMode.CreateNew,FileAccess.Write,FileShare.None,1,FileOptions.DeleteOnClose)) {
                    created=true;stream.WriteByte(0);stream.Flush();
                }
                return true;
            } catch(IOException){return false;}
              catch(UnauthorizedAccessException){return false;}
              catch(System.Security.SecurityException){return false;}
              catch(ArgumentException){return false;}
              catch(NotSupportedException){return false;}
            finally {
                // Only the unique file this invocation created can be removed. Existing user files are untouched.
                if(created && file!=null)try{File.Delete(file);}catch(IOException){}catch(UnauthorizedAccessException){}catch(System.Security.SecurityException){}
            }
        }
    }
}
