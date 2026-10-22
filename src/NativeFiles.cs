using System;
using System.Globalization;
using System.IO;
using System.Security;
using System.Security.Cryptography;
using System.Text;

namespace ChainedGuard {
    // Copies only the bundled bridge into application data. Never receives a
    // game directory, loads a module, or replaces an existing cache file.
    internal static class NativeFiles {
        private const int MaximumNativeFolderLength=170;
        private const int MaximumDllPathLength=259;

        private static string DirectoryPath(string path) {
            if(string.IsNullOrWhiteSpace(path))throw new ArgumentException("Папка данных Dodger не указана.","dataFolder");
            string full=Path.GetFullPath(path),root=Path.GetPathRoot(full);
            return full.Length>root.Length?full.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar):root;
        }

        private static void CheckFolderLength(string folder) {
            if(folder.Length>MaximumNativeFolderLength)
                throw new PathTooLongException("Путь к папке модуля Dodger слишком длинный. Используй папку с путём не длиннее 170 символов, чтобы игровой журнал работал корректно.");
        }

        private static void CheckDllLength(string path) {
            if(path.Length>MaximumDllPathLength)
                throw new PathTooLongException("Путь к модулю Dodger слишком длинный. Используй путь не длиннее 259 символов.");
        }

        private static string ChildPath(string folder,string name) {
            string path=Path.GetFullPath(Path.Combine(folder,name));
            if(!string.Equals(DirectoryPath(Path.GetDirectoryName(path)),folder,StringComparison.OrdinalIgnoreCase))
                throw new IOException("Не удалось подготовить модуль внутри папки данных Dodger.");
            CheckDllLength(path);return path;
        }

        private static byte[] Digest(Stream stream) {
            using(SHA256 hash=SHA256.Create())return hash.ComputeHash(stream);
        }

        private static void VerifyHash(string path,byte[] expected) {
            byte[] actual;
            using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read,65536,FileOptions.SequentialScan))actual=Digest(file);
            bool equal=actual.Length==expected.Length;
            for(int i=0;equal&&i<actual.Length;i++)equal=actual[i]==expected[i];
            if(!equal)throw new IOException("Существующая копия модуля Dodger отличается от ожидаемой. Файл сохранён без изменений: "+path);
        }

        internal static string Prepare(string source,string dataFolder,bool useFallback) {
            if(string.IsNullOrWhiteSpace(source))throw new ArgumentException("Путь к ChainedGuard.Native.dll не указан.","source");
            string original=Path.GetFullPath(source);CheckDllLength(original);
            if(!useFallback) {
                CheckFolderLength(DirectoryPath(Path.GetDirectoryName(original)));
                using(var file=new FileStream(original,FileMode.Open,FileAccess.Read,FileShare.Read)){}
                return original;
            }

            string folder=DirectoryPath(dataFolder);CheckFolderLength(folder);
            Directory.CreateDirectory(folder);
            // Keep this handle open until publication so hash and copied bytes
            // describe the same source, without allowing a concurrent writer.
            using(var input=new FileStream(original,FileMode.Open,FileAccess.Read,FileShare.Read,65536,FileOptions.SequentialScan)) {
                byte[] digest=Digest(input);var suffix=new StringBuilder(16);
                for(int i=0;i<8;i++)suffix.Append(digest[i].ToString("x2",CultureInfo.InvariantCulture));
                string target=ChildPath(folder,"ChainedGuard.Native.loaded-"+suffix+".dll");
                if(File.Exists(target)){VerifyHash(target,digest);return target;}

                string temporary=ChildPath(folder,".dodger-native-"+Guid.NewGuid().ToString("N")+".tmp");bool created=false;
                try {
                    using(var output=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None,65536,FileOptions.SequentialScan)) {
                        created=true;input.Position=0;input.CopyTo(output);output.Flush(true);
                    }
                    VerifyHash(temporary,digest);
                    try {File.Move(temporary,target);created=false;}
                    catch(IOException) {
                        // Another Prepare invocation may have won publication.
                        // A different existing file is never overwritten.
                        if(!File.Exists(target))throw;
                        VerifyHash(target,digest);
                    }
                    VerifyHash(target,digest);return target;
                } finally {
                    // Only this invocation's unique, successfully created file
                    // is eligible for cleanup. Cache and user files are kept.
                    if(created)try{File.Delete(temporary);}
                        catch(IOException){}catch(UnauthorizedAccessException){}catch(SecurityException){}
                }
            }
        }
    }
}
