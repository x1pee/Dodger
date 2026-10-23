using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading;
using System.Text.RegularExpressions;

namespace ChainedGuard {
    // Messages are translated only at known application boundaries. Unknown text,
    // paths and OS error payloads remain verbatim.
    internal static class I18n {
        private sealed class Pair { public string Ru,En; }
        private static readonly object gate=new object();
        private static readonly Dictionary<string,Pair> known=new Dictionary<string,Pair>(StringComparer.Ordinal);
        private static readonly List<Pair> pairs=new List<Pair>();
        private static string language=DefaultLanguage;
        public static string DefaultLanguage { get { return LanguageForCulture(CultureInfo.CurrentUICulture); } }
        internal static string LanguageForCulture(CultureInfo culture) { return culture!=null&&culture.TwoLetterISOLanguageName=="ru"?"ru":"en"; }
        public static string Normalize(string value) {
            if(string.IsNullOrWhiteSpace(value))return DefaultLanguage;
            value=value.Trim().ToLowerInvariant();
            if(value=="ru"||value.StartsWith("ru-",StringComparison.Ordinal))return "ru";
            if(value=="en"||value.StartsWith("en-",StringComparison.Ordinal))return "en";
            return DefaultLanguage;
        }
        public static string Language { get { return Volatile.Read(ref language); } set { Volatile.Write(ref language,Normalize(value)); } }
        static I18n() {
            Add("Нет доступа к процессу. Запустите панель с теми же правами, что и игру.","Access denied. Run Dodger with the same permissions as the game.");
            Add("Не удалось выделить память для подключения.","Could not allocate memory for the connection.");
            Add("Не удалось передать путь модуля.","Could not transfer the module path.");
            Add("LoadLibraryW недоступен.","LoadLibraryW is unavailable.");
            Add("Не найдена системная функция загрузки в игровом процессе.","The system loader was not found in the game process.");
            Add("Не удалось загрузить модуль в игру.","Could not load the module into the game.");
            Add("Игра ещё загружает модуль. Ожидайте сигнала подключения.","The game is still loading the module. Waiting for a connection signal.");
            Add("Игра не смогла загрузить ChainedGuard.Native.dll.","The game could not load ChainedGuard.Native.dll.");
            Add("Открыть панель","Open panel");
            Add("Завершить","Exit");
            Add("Метка F9 недоступна. Боевой журнал записывается автоматически.","F9 markers are unavailable. Combat logging is automatic.");
            Add("Свернуть окно настроек","Minimize settings");
            Add("ОЖИДАНИЕ ИГРЫ","WAITING FOR GAME");
            Add("●  Автоподключение","●  Auto-connect");
            Add("Запустите Chained Beasts — подключение произойдёт автоматически.","Start Chained Beasts to connect automatically.");
            Add("Запустить игру","Launch game");
            Add("Папка…","Folder…");
            Add("Выбрать папку с распакованной игрой","Select the extracted game folder");
            Add("Найти","Find");
            Add("Повторить подключение","Reconnect");
            Add("Папка игры будет найдена автоматически.","The game folder will be found automatically.");
            Add("Автоблок","Auto-block");
            Add("Поворачивает героя к удару и ставит блок. Нужен щит или перк защиты рукой. С перком блок рукой поглощает 75% урона.","Faces the incoming attack and blocks. Requires a shield or the arm-guard perk. With the perk, arm blocks absorb 75% of damage.");
            Add("ПКМ","RMB");
            Add("Блок при неуязвимости","Block while invulnerable");
            Add("Разрешает автоблок даже во время неуязвимости — ради оглушения противника. Щит при этом может терять прочность.","Allows auto-block while invulnerable to stun attackers. This can consume shield durability.");
            Add("Автоуклонение","Auto-dodge");
            Add("Полноценный уворот, если блок невозможен. Учитывает настоящие кадры неуязвимости, выносливость и травмы.","Uses a full dodge when blocking is unavailable. Accounts for actual invulnerability frames, stamina and injuries.");
            Add("ПРОБЕЛ","SPACE");
            Add("Приоритет защиты","Defense priority");
            Add("Задерживает новый удар, если замах помешает вовремя поставить блок или начать автоуклонение. Учитывает время до включения неуязвимости. Пропускает проверенное добивание обычным ближним ударом, если нет других угроз. Уже начатые атаки подчиняются правилам игры.","Delays a new attack if its windup would prevent a timely block or dodge. Accounts for invulnerability startup. Allows a verified normal melee finisher when no other threats remain. Attacks already started follow the game's cancellation rules.");
            Add("Прерывать усиленный удар","Interrupt charged attacks");
            Add("Разрешает автоматике отменить усиленный удар в блок или рывок, когда это разрешает игра. Неотменяемый замах не обходится. Требуется включённый автоблок или автоуклонение.","Allows a charged attack to be canceled into a block or dodge when the game permits it. Cannot bypass an uncancelable windup. Requires auto-block or auto-dodge.");
            Add("Индикатор угрозы","Threat indicator");
            Add("Чёрный круг с цветной дугой — прочность щита. При угрозе появляется контур и знак !. Переход из оранжевого в красный начинается за 0,65 с и завершается за 0,3 с до попадания. Последние деления щита мигают чаще.","The black circle's colored arc shows shield durability. Incoming attacks add an outline and !. Orange turns red from 0.65 to 0.3 seconds before impact. The final shield segments blink faster.");
            Add("Статистика и журнал","Statistics and logs");
            Add("Успешных блоков","Successful blocks");
            Add("Считаются только удары, которые действительно удалось заблокировать.","Counts only attacks actually blocked.");
            Add("Рывков выполнено","Dashes performed");
            Add("Снарядов избегнуто","Projectiles evaded");
            Add("Журнал записывается автоматически.\nПосле прогона опиши проблему в чате.","Logs are recorded automatically.\nDescribe any issue after your session.");
            Add("Не удалось открыть журнал: ","Could not open the log: ");
            Add("Снимки экрана не сохраняются.","Screenshots are not saved.");
            Add("F9 — отметить проблему; затем опиши её в чате.","F9 — mark an issue, then describe it afterwards.");
            Add("Боевой журнал: ожидаем подключение.","Combat log: waiting for connection.");
            Add("Открыть журнал","Open logs");
            Add("Не удалось открыть папку журнала: ","Could not open the log folder: ");
            Add("Игра уже запущена — поиск дисков отложен.","The game is already running. Disk search is postponed.");
            Add("Стоп","Stop");
            Add("Ищем игру на локальных дисках…","Searching local drives for the game…");
            Add("Поиск: ","Searching: ");
            Add("Игра запущена. Ожидаем подключение…","The game is running. Waiting for connection…");
            Add("Ошибка поиска. Выбери папку вручную.","Search failed. Select the folder manually.");
            Add("Поиск остановлен. Можно выбрать папку вручную.","Search stopped. You can select the folder manually.");
            Add("Игра не найдена в пределах поиска. Нажми «Папка…».","The game was not found within the search limits. Click “Folder…”.");
            Add("Выбери папку с ChainedBeasts.exe и всей распакованной игрой","Select the folder containing ChainedBeasts.exe and the extracted game");
            Add("Проверяем выбранную папку…","Checking the selected folder…");
            Add(" Нажми «Папка…» или «Найти»."," Click “Folder…” or “Find”.");
            Add("Запуск отменён.","Launch canceled.");
            Add("Игра запускается. Подключимся автоматически…","The game is starting. Connecting automatically…");
            Add("Ожидаем игру…","Waiting for the game…");
            Add("Не удалось записать журнал: ","Could not write the log: ");
            Add("Канал недоступен: ","Channel unavailable: ");
            Add("Эта сборка игры пока не поддерживается: GameAssembly.dll отличается от проверенной версии.","This game build is not supported: GameAssembly.dll differs from the verified build.");
            Add("Не найден ChainedGuard.Native.dll рядом с Dodger.exe.","ChainedGuard.Native.dll was not found next to Dodger.exe.");
            Add("Подключение отменено.","Connection canceled.");
            Add("Не удалось проверить игру.","Could not check the game.");
            Add("Запусти Chained Beasts — подключимся автоматически.","Start Chained Beasts to connect automatically.");
            Add("Модуль загружен. Ожидаем игровой сигнал…","Module loaded. Waiting for a game signal…");
            Add("Подключение не завершено.","Connection did not complete.");
            Add("●  Подключено","●  Connected");
            Add("○  Ожидаем модуль","○  Waiting for module");
            Add("●  Герой найден","●  Player found");
            Add("○  Герой не найден","○  Player not found");
            Add("●  Атака рядом","●  Incoming attack");
            Add("●  Сигнал активен","●  Signal active");
            Add("○  Сигнал не получен","○  No signal received");
            Add("ОШИБКА ПОДКЛЮЧЕНИЯ","CONNECTION ERROR");
            Add("ОЖИДАНИЕ ПОДКЛЮЧЕНИЯ","WAITING FOR CONNECTION");
            Add("В ИГРЕ","IN GAME");
            Add("ПОДКЛЮЧЕНО","CONNECTED");
            Add("Обновление ещё не применено. Полностью закрой и заново запусти игру и Dodger.","The update has not been applied yet. Fully close and restart the game and Dodger.");
            Add("Ожидаем ответ игрового модуля.","Waiting for the game module.");
            Add("Красный контур — угроза. Зелёный сонар — полный блок без урона.","Red outline: incoming threat. Green sonar: a full block without damage.");
            Add("Подключено. Ожидаем появления героя в бою.","Connected. Waiting for a player in combat.");
            Add("ПЕРЕЗАПУСТИ ИГРУ","RESTART THE GAME");
            Add(" Для последнего исправления защиты перезапустите игру."," Restart the game to apply the latest defense fix.");
            Add("○  Подключение","○  Connection");
            Add("○  Герой","○  Player");
            Add("○  Сигнал атаки","○  Attack signal");
            Add("Игра закрыта. Ожидаем следующий запуск…","The game closed. Waiting for the next launch…");
            Add("Для обновлённого мода и боевого журнала перезапустите игру.","Restart the game to update the mod and combat log.");
            Add("Боевой журнал включён.","Combat log enabled.");
            Add("Ошибка записи боевого журнала.","Combat log write error.");
            Add("Боевой журнал запускается…","Combat log is starting…");
            Add(" Для нового индикатора и расчёта защиты перезапустите игру."," Restart the game for the updated indicator and defense calculation.");
            Add(" Пропущено записей: "," Dropped records: ");
            Add("Герой неактивен","Player inactive");
            Add("События игры подключены","Game events connected");
            Add("Защита остановлена: ошибка доступа к объекту","Defense stopped: object access error");
            Add("Ожидание загрузки движка","Waiting for game engine");
            Add("Движок не загрузился: перезапустите игру","Game engine did not load: restart the game");
            Add("API IL2CPP не найден","IL2CPP API not found");
            Add("Движок ещё не готов: перезапустите игру","Game engine is not ready: restart the game");
            Add("Не удалось подключить поток к движку","Could not attach a thread to the game engine");
            Add("Версия игры не поддержана: функции не найдены","Unsupported game build: required functions not found");
            Add("Версия игры не поддержана: диагностика недоступна","Unsupported game build: diagnostics unavailable");
            Add("Версия игры не поддержана: адреса изменились","Unsupported game build: function addresses changed");
            Add("Версия игры не поддержана: ранние события снарядов изменились","Unsupported game build: early projectile events changed");
            Add("Версия игры не поддержана: событие взрыва изменилось","Unsupported game build: explosion event changed");
            Add("Версия игры не поддержана: событие щита изменилось","Unsupported game build: shield event changed");
            Add("Версия игры не поддержана: события боссов изменились","Unsupported game build: boss events changed");
            Add("Не удалось подключить события","Could not connect game events");
            Add("Подключено. Ожидание героя","Connected. Waiting for player");
        }
        private static void Add(string ru,string en) {
            if(known.ContainsKey(ru))return;
            var pair=new Pair{Ru=ru,En=en};known[ru]=pair;known[en]=pair;pairs.Add(pair);
        }
        public static string Text(string ru,string en) {
            lock(gate)Add(ru,en);
            return Language=="ru"?ru:en;
        }
        public static string Translate(string text) {
            if(string.IsNullOrEmpty(text)||IsPath(text))return text;
            bool ru=Language=="ru";
            if(text.StartsWith("Мод в игре | щит: ",StringComparison.Ordinal)||text.StartsWith("Module in game | shield: ",StringComparison.Ordinal)){string nativeInfo=TranslateNativeInfo(text,ru);if(nativeInfo!=null)return nativeInfo;}
            lock(gate)return TranslateKnown(text,ru,0);
        }
        private static bool IsPath(string text) {
            return text.Length>2&&char.IsLetter(text[0])&&text[1]==':'&&(text[2]=='\\'||text[2]=='/') || text.StartsWith("\\\\",StringComparison.Ordinal);
        }
        private static string TranslateKnown(string text,bool ru,int depth) {
            Pair exact;if(known.TryGetValue(text,out exact))return ru?exact.Ru:exact.En;
            if(depth>8||IsPath(text))return text;
            foreach(var pair in pairs) {
                string from=ru?pair.En:pair.Ru,to=ru?pair.Ru:pair.En;
                // A colon prefix owns only its label; never inspect its path/OS payload.
                if(from.EndsWith(": ",StringComparison.Ordinal)&&text.StartsWith(from,StringComparison.Ordinal)) {
                    string payload=text.Substring(from.Length);Pair message;
                    // Search progress may contain either an untouched path or one exact application message.
                    return to+(known.TryGetValue(payload,out message)?(ru?message.Ru:message.En):payload);
                }
                // Sentence sequences used by trace status and the explicit launch hint.
                if(text.StartsWith(from,StringComparison.Ordinal)&&text.Length>from.Length) {
                    string tail=text.Substring(from.Length);
                    if(tail.StartsWith("\r\n",StringComparison.Ordinal)||tail.StartsWith("\n",StringComparison.Ordinal))return to+tail;
                    if(KnownSuffix(tail,ru))return to+TranslateKnown(tail,ru,depth+1);
                }
                if(from.StartsWith(" ",StringComparison.Ordinal)&&text.EndsWith(from,StringComparison.Ordinal))return TranslateKnown(text.Substring(0,text.Length-from.Length),ru,depth+1)+to;
            }
            return text;
        }
        private static string TranslateNativeInfo(string text,bool ru) {
            // The native DLL remains version 18. Translate its complete diagnostic grammar only.
            var match=Regex.Match(text,@"^Мод в игре \| щит: (есть|нет) \| защита: (готова|пауза) \| кадры: ([0-9]+)$",RegexOptions.CultureInvariant);
            if(match.Success)return ru?text:"Module in game | shield: "+(match.Groups[1].Value=="есть"?"yes":"no")+" | defense: "+(match.Groups[2].Value=="готова"?"ready":"paused")+" | frames: "+match.Groups[3].Value;
            match=Regex.Match(text,@"^Module in game \| shield: (yes|no) \| defense: (ready|paused) \| frames: ([0-9]+)$",RegexOptions.CultureInvariant);
            if(match.Success)return ru?"Мод в игре | щит: "+(match.Groups[1].Value=="yes"?"есть":"нет")+" | защита: "+(match.Groups[2].Value=="ready"?"готова":"пауза")+" | кадры: "+match.Groups[3].Value:text;
            return null;
        }
        private static bool KnownSuffix(string tail,bool ru) {
            foreach(var pair in pairs) { string from=ru?pair.En:pair.Ru;if(from.StartsWith(" ",StringComparison.Ordinal)&&tail.StartsWith(from,StringComparison.Ordinal))return true; }
            return false;
        }
    }
}
