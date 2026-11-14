using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.ServiceProcess;
using System.Text;
using System.Threading;

namespace WildOS.Guest
{
    // The wild-guest Windows service. It runs as LocalSystem in session 0
    // and owns the virtio-serial port org.wildos.guest.0 to Wild OS. Work
    // that has to happen inside the person's own session (their windows,
    // starting programs, their colours) goes to the session helper, a copy
    // of this program started in each session of the Windows mode account,
    // over the pipe \\.\pipe\wildos-guest.
    //
    // The protocol, one JSON object per line, is described in
    // winmode/guestlink.py on the Wild OS side.
    sealed class GuestService : ServiceBase
    {
        public const string Version = "1";
        const string PortName = @"\\.\Global\org.wildos.guest.0";
        const string PipeName = "wildos-guest";
        const string SettingsFile = @"C:\ProgramData\WildOS\settings.json";

        readonly QueuedWriter host = new QueuedWriter();
        readonly Dictionary<uint, SessionClient> sessions = new Dictionary<uint, SessionClient>();
        readonly Dictionary<uint, DateTime> spawned = new Dictionary<uint, DateTime>();
        string account = "wild";
        // The last warm list, for a session helper that starts later.
        volatile Dictionary<string, object> lastWarm;
        volatile bool stopping;

        public GuestService()
        {
            ServiceName = "WildGuest";
            CanHandleSessionChangeEvent = true;
            CanShutdown = true;
        }

        protected override void OnStart(string[] args)
        {
            Start();
        }

        protected override void OnStop()
        {
            stopping = true;
        }

        public void Start()
        {
            Log.Write("wild-guest " + Version + " starting");
            try
            {
                var settings = Json.ParseObject(File.ReadAllText(SettingsFile));
                account = Json.Str(settings, "account") ?? account;
            }
            catch (Exception)
            {
            }
            new Thread(PortLoop) { IsBackground = true, Name = "port" }.Start();
            new Thread(PipeLoop) { IsBackground = true, Name = "pipe" }.Start();
            new Thread(SessionWatch) { IsBackground = true, Name = "sessions" }.Start();
        }

        // Control code 128, sent by Wild OS through the guest agent right
        // after Windows is restored from disk: the channel's other end is a
        // new one, which a read already waiting would never hear from.
        public const int ReopenPort = 128;
        volatile Port current;

        protected override void OnCustomCommand(int command)
        {
            var port = current;
            if (command == ReopenPort && port != null) port.CancelAll();
        }

        protected override void OnSessionChange(SessionChangeDescription change)
        {
            uint id = (uint)change.SessionId;
            host.Send(Message("session-change", "session", (long)id, "event", change.Reason.ToString()));
            if (change.Reason == SessionChangeReason.SessionLogon || change.Reason == SessionChangeReason.RemoteConnect ||
                change.Reason == SessionChangeReason.SessionUnlock)
            {
                ThreadPool.QueueUserWorkItem(delegate { EnsureHelper(id, true); });
            }
        }

        static Dictionary<string, object> Message(string type, params object[] pairs)
        {
            var m = new Dictionary<string, object>();
            m["type"] = type;
            for (int i = 0; i + 1 < pairs.Length; i += 2) m[(string)pairs[i]] = pairs[i + 1];
            return m;
        }

        void PortLoop()
        {
            int lastError = 0;
            bool everOpened = false;
            while (!stopping)
            {
                int error;
                Port port = Port.Open(PortName, out error);
                if (port == null)
                {
                    if (error != lastError) Log.Write("port " + PortName + " not open yet (" + error + ")");
                    lastError = error;
                    Thread.Sleep(2000);
                    continue;
                }
                if (lastError != 0 || !everOpened) Log.Write("port open");
                lastError = 0;
                everOpened = true;
                using (port)
                {
                    current = port;
                    var lines = new Lines(port);
                    host.SetTarget(lines, port);
                    // When Wild OS's end of the channel closes and opens again,
                    // a read already waiting never sees what it sends next.
                    // Wild OS pings every 20 seconds, so a port that has been
                    // silent for longer is opened afresh.
                    DateTime opened = DateTime.UtcNow;
                    var watchdog = new Timer(delegate
                    {
                        if ((DateTime.UtcNow - opened).TotalSeconds > 45 && host.SilentSeconds() > 45)
                        {
                            port.CancelAll();
                        }
                    }, null, 15000, 15000);
                    try
                    {
                        string line;
                        while (!stopping && (line = lines.ReadLine()) != null)
                        {
                            host.Heard();
                            if (line.Length == 0) continue;
                            Dictionary<string, object> message;
                            try { message = Json.ParseObject(line); }
                            catch (Exception) { continue; }
                            if (message != null) HandleHost(message);
                        }
                    }
                    catch (OperationCanceledException)
                    {
                    }
                    catch (IOException)
                    {
                    }
                    catch (Exception e)
                    {
                        Log.Write("port: " + e.Message);
                    }
                    watchdog.Dispose();
                }
                Thread.Sleep(1000);
            }
        }

        Dictionary<string, object> Hello(object id)
        {
            var list = new List<object>();
            lock (sessions)
            {
                foreach (uint s in sessions.Keys) list.Add((long)s);
            }
            string edition = null;
            try
            {
                using (var k = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion"))
                {
                    if (k != null) edition = k.GetValue("EditionID") as string;
                }
            }
            catch (Exception)
            {
            }
            return Message("hello", "id", id, "version", Version, "account", account, "sessions", list, "edition", edition,
                "boot", DateTime.UtcNow.AddMilliseconds(-Environment.TickCount).ToString("s") + "Z");
        }

        void Reply(object id, Dictionary<string, object> body)
        {
            body["id"] = id;
            host.Send(body);
        }

        // A request that fails answers with its error. Thrown out of here it
        // would end the read loop and drop the channel to Wild OS.
        void HandleHost(Dictionary<string, object> m)
        {
            object id;
            m.TryGetValue("id", out id);
            try
            {
                Dispatch(m, id);
            }
            catch (Exception e)
            {
                Log.Write("request " + (Json.Str(m, "type") ?? "") + ": " + e.Message);
                Reply(id, Message("error", "error", e.Message));
            }
        }

        void Dispatch(Dictionary<string, object> m, object id)
        {
            string type = Json.Str(m, "type") ?? "";
            switch (type)
            {
                case "hello":
                case "ping":
                    host.Send(Hello(id));
                    break;
                case "time":
                    Reply(id, SetClock(m));
                    break;
                case "apps":
                    ThreadPool.QueueUserWorkItem(delegate
                    {
                        try
                        {
                            bool icons = Json.Bool(m, "icons", true);
                            var list = Apps.List(account, icons);
                            Apps.AddPackaged(list, PackagedApps(icons));
                            Reply(id, Message("apps", "apps", list));
                        }
                        catch (Exception e) { Reply(id, Message("error", "error", e.Message)); }
                    });
                    break;
                case "icon":
                    ThreadPool.QueueUserWorkItem(delegate
                    {
                        byte[] ico = null;
                        // The service reads as LocalSystem: only a program on
                        // a local disk, outside every other account's
                        // profile, never \\server\share.
                        string path = Paths.ForService(Json.Str(m, "path"), account);
                        if (path != null && (path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)))
                        {
                            try { ico = Icons.FromExecutable(path); } catch (Exception) { }
                        }
                        Reply(id, Message("icon", "path", Json.Str(m, "path"), "icon", ico == null ? null : Convert.ToBase64String(ico)));
                    });
                    break;
                case "uninstall":
                    ThreadPool.QueueUserWorkItem(delegate
                    {
                        Dictionary<string, object> answer;
                        try { answer = Uninstall.Run(Json.Str(m, "path"), account); }
                        catch (Exception e) { answer = Message("error", "error", e.Message); }
                        Reply(id, answer);
                    });
                    break;
                case "theme":
                    try { Theme.Save(m); } catch (Exception e) { Log.Write("theme: " + e.Message); }
                    int sent = 0;
                    foreach (var client in Clients()) { client.Send(m); sent++; }
                    if (sent == 0) Reply(id, Message("theme", "ok", true, "applied", false));
                    break;
                case "sessions":
                    Reply(id, Message("sessions", "sessions", SessionList()));
                    break;
                case "warm":
                    // Every session of the account keeps the same apps warm.
                    lastWarm = m;
                    int told = 0;
                    foreach (var client in Clients()) { client.Send(m); told++; }
                    if (told == 0) Reply(id, Message("warm", "ok", true, "applied", false));
                    break;
                case "windows":
                case "window":
                case "running":
                case "launch":
                case "warm-list":
                case "documents":
                    var target = Target(m);
                    if (target != null)
                    {
                        target.Send(m);
                    }
                    else if (type == "windows")
                    {
                        Reply(id, Message("windows", "windows", new List<object>(), "session", null));
                    }
                    else
                    {
                        Reply(id, Message("error", "error", "no-session"));
                    }
                    break;
                default:
                    Reply(id, Message("error", "error", "unknown type " + type));
                    break;
            }
        }

        const string PackagedCache = @"C:\ProgramData\WildOS\packaged-apps.json";

        // The account's Store apps, from its session helper, since only the
        // account itself can see them. With no session open (Windows has
        // just started, or the person signed out) the last list stands in,
        // so their Start entries don't come and go with the session.
        List<object> PackagedApps(bool icons)
        {
            List<object> apps = null;
            var client = Target(new Dictionary<string, object>());
            if (client != null)
            {
                var reply = Ask(client, Message("packaged-apps", "icons", true), 60000);
                if (reply != null && Json.Str(reply, "type") == "packaged-apps") apps = Json.List(reply, "apps");
                else Log.Write("packaged apps: " + (reply == null ? "no answer from the session" : Json.Str(reply, "error")));
                if (apps != null)
                {
                    try { File.WriteAllText(PackagedCache, Json.Write(apps)); }
                    catch (Exception e) { Log.Write("packaged apps: " + e.Message); }
                }
            }
            if (apps == null)
            {
                try { apps = Json.Parse(File.ReadAllText(PackagedCache)) as List<object>; }
                catch (Exception) { }
            }
            apps = apps ?? new List<object>();
            if (!icons)
            {
                foreach (object a in apps)
                {
                    var d = a as Dictionary<string, object>;
                    if (d != null) d.Remove("icon");
                }
            }
            return apps;
        }

        sealed class Asked
        {
            public readonly ManualResetEvent Done = new ManualResetEvent(false);
            public Dictionary<string, object> Reply;
        }

        readonly Dictionary<string, Asked> asked = new Dictionary<string, Asked>();
        int asking;

        // A request of the service's own to a session helper. Its id is a
        // string, which Wild OS's never are, so FromSession can tell the
        // reply from one meant for Wild OS.
        Dictionary<string, object> Ask(SessionClient client, Dictionary<string, object> m, int timeoutMs)
        {
            string id = "service-" + Interlocked.Increment(ref asking);
            var box = new Asked();
            lock (asked) asked[id] = box;
            try
            {
                m["id"] = id;
                client.Send(m);
                box.Done.WaitOne(timeoutMs);
                return box.Reply;
            }
            finally
            {
                lock (asked) asked.Remove(id);
                box.Done.Close();
            }
        }

        Dictionary<string, object> SetClock(Dictionary<string, object> m)
        {
            long ms = Json.Num(m, "unix_ms", 0);
            if (ms <= 0 || ms > SessionHelper.MaxUnixMs) return Message("time", "ok", false, "error", "no time given");
            DateTime target = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc).AddMilliseconds(ms);
            double skew = (DateTime.UtcNow - target).TotalMilliseconds;
            bool was;
            Native.RtlAdjustPrivilege(12, true, false, out was);
            var st = new Native.SYSTEMTIME
            {
                Year = (ushort)target.Year, Month = (ushort)target.Month, Day = (ushort)target.Day,
                Hour = (ushort)target.Hour, Minute = (ushort)target.Minute, Second = (ushort)target.Second,
                Milliseconds = (ushort)target.Millisecond, DayOfWeek = (ushort)target.DayOfWeek,
            };
            bool ok = Native.SetSystemTime(ref st);
            return Message("time", "ok", ok, "skew_ms", (long)skew, "error", ok ? null : "SetSystemTime failed: " + Marshal.GetLastWin32Error());
        }

        List<SessionClient> Clients()
        {
            lock (sessions)
            {
                return new List<SessionClient>(sessions.Values);
            }
        }

        SessionClient Target(Dictionary<string, object> m)
        {
            long wanted = Json.Num(m, "session", -1);
            lock (sessions)
            {
                SessionClient best = null;
                foreach (var client in sessions.Values)
                {
                    if (wanted >= 0 && client.Session == wanted) return client;
                    if (best == null || client.Since > best.Since) best = client;
                }
                return wanted >= 0 ? null : best;
            }
        }

        List<object> SessionList()
        {
            var list = new List<object>();
            IntPtr info;
            uint count;
            if (!Native.WTSEnumerateSessions(IntPtr.Zero, 0, 1, out info, out count)) return list;
            try
            {
                int size = Marshal.SizeOf(typeof(Native.WTS_SESSION_INFO));
                for (int i = 0; i < count; i++)
                {
                    var s = (Native.WTS_SESSION_INFO)Marshal.PtrToStructure(new IntPtr(info.ToInt64() + i * size), typeof(Native.WTS_SESSION_INFO));
                    if (s.SessionId == 0) continue;
                    bool helper;
                    lock (sessions) helper = sessions.ContainsKey(s.SessionId);
                    list.Add(Message("session", "session", (long)s.SessionId, "station", s.WinStationName, "state", s.State,
                        "user", UserOf(s.SessionId), "helper", helper));
                }
            }
            finally
            {
                Native.WTSFreeMemory(info);
            }
            return list;
        }

        static string UserOf(uint session)
        {
            IntPtr buffer;
            uint bytes;
            if (!Native.WTSQuerySessionInformation(IntPtr.Zero, session, Native.WTSUserName, out buffer, out bytes)) return null;
            try { return Marshal.PtrToStringUni(buffer); }
            finally { Native.WTSFreeMemory(buffer); }
        }

        // Sessions already running when the service starts (it was updated,
        // or Windows was restored from a save) still need their helper.
        void SessionWatch()
        {
            while (!stopping)
            {
                foreach (Dictionary<string, object> s in SessionList())
                {
                    int state = (int)s["state"];
                    if (state == Native.WTSActive || state == Native.WTSConnected || state == Native.WTSDisconnected)
                    {
                        EnsureHelper((uint)(long)s["session"], false);
                    }
                }
                Thread.Sleep(5000);
            }
        }

        void EnsureHelper(uint session, bool soon)
        {
            string user = UserOf(session);
            if (string.IsNullOrEmpty(user) || !string.Equals(user, account, StringComparison.OrdinalIgnoreCase)) return;
            lock (sessions)
            {
                if (sessions.ContainsKey(session)) return;
                DateTime last;
                if (spawned.TryGetValue(session, out last) && (DateTime.UtcNow - last).TotalSeconds < (soon ? 3 : 15)) return;
                spawned[session] = DateTime.UtcNow;
            }
            string error = Spawn(session, System.Reflection.Assembly.GetExecutingAssembly().Location, "--session");
            Log.Write(error == null ? "started the session helper in session " + session : "session " + session + ": " + error);
        }

        static string Spawn(uint session, string exe, string args)
        {
            IntPtr token = IntPtr.Zero, primary = IntPtr.Zero, environment = IntPtr.Zero;
            try
            {
                if (!Native.WTSQueryUserToken(session, out token)) return "no user token (" + Marshal.GetLastWin32Error() + ")";
                if (!Native.DuplicateTokenEx(token, Native.MAXIMUM_ALLOWED, IntPtr.Zero, Native.SecurityImpersonation, Native.TokenPrimary, out primary))
                    return "token copy failed (" + Marshal.GetLastWin32Error() + ")";
                Native.CreateEnvironmentBlock(out environment, primary, false);
                var startup = new Native.STARTUPINFO();
                startup.cb = Marshal.SizeOf(typeof(Native.STARTUPINFO));
                startup.lpDesktop = @"winsta0\default";
                var command = new StringBuilder("\"" + exe + "\" " + args);
                Native.PROCESS_INFORMATION info;
                if (!Native.CreateProcessAsUser(primary, null, command, IntPtr.Zero, IntPtr.Zero, false,
                        Native.CREATE_UNICODE_ENVIRONMENT | Native.CREATE_NO_WINDOW, environment, Path.GetDirectoryName(exe), ref startup, out info))
                    return "CreateProcessAsUser failed (" + Marshal.GetLastWin32Error() + ")";
                Native.CloseHandle(info.hThread);
                Native.CloseHandle(info.hProcess);
                return null;
            }
            finally
            {
                if (environment != IntPtr.Zero) Native.DestroyEnvironmentBlock(environment);
                if (primary != IntPtr.Zero) Native.CloseHandle(primary);
                if (token != IntPtr.Zero) Native.CloseHandle(token);
            }
        }

        void PipeLoop()
        {
            var security = new PipeSecurity();
            security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null), PipeAccessRights.FullControl,
                AccessControlType.Allow));
            security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null),
                PipeAccessRights.FullControl, AccessControlType.Allow));
            security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid, null),
                PipeAccessRights.ReadWrite, AccessControlType.Allow));
            while (!stopping)
            {
                NamedPipeServerStream pipe = null;
                try
                {
                    pipe = new NamedPipeServerStream(PipeName, PipeDirection.InOut, NamedPipeServerStream.MaxAllowedServerInstances,
                        PipeTransmissionMode.Byte, PipeOptions.Asynchronous, 65536, 65536, security);
                    pipe.WaitForConnection();
                    uint session;
                    if (!Native.GetNamedPipeClientSessionId(pipe.SafePipeHandle.DangerousGetHandle(), out session) || !IsHelper(pipe, session))
                    {
                        pipe.Dispose();
                        continue;
                    }
                    var client = new SessionClient(this, pipe, session);
                    new Thread(client.Run) { IsBackground = true, Name = "session " + session }.Start();
                }
                catch (Exception e)
                {
                    Log.Write("pipe: " + e.Message);
                    if (pipe != null) pipe.Dispose();
                    Thread.Sleep(1000);
                }
            }
        }

        // Any signed-in account can open the pipe. Only this program, in a
        // session of the Windows mode account, is a session helper: anything
        // else would be handed the programs Wild OS starts and could answer
        // for the session.
        bool IsHelper(NamedPipeServerStream pipe, uint session)
        {
            if (session == 0 || !string.Equals(UserOf(session), account, StringComparison.OrdinalIgnoreCase)) return false;
            uint pid;
            if (!Native.GetNamedPipeClientProcessId(pipe.SafePipeHandle.DangerousGetHandle(), out pid)) return false;
            string image = Windows.ImagePath(pid);
            bool ok = image != null && string.Equals(image, System.Reflection.Assembly.GetExecutingAssembly().Location, StringComparison.OrdinalIgnoreCase);
            if (!ok) Log.Write("pipe: refused " + (image ?? "process " + pid) + " in session " + session);
            return ok;
        }

        internal void Register(SessionClient client)
        {
            SessionClient old;
            lock (sessions)
            {
                sessions.TryGetValue(client.Session, out old);
                sessions[client.Session] = client;
            }
            if (old != null && old != client) old.Close();
            var warm = lastWarm;
            if (warm != null)
            {
                var copy = new Dictionary<string, object>(warm);
                copy.Remove("id");
                client.Send(copy);
            }
            host.Send(Message("session", "session", (long)client.Session, "state", "up"));
        }

        internal void Unregister(SessionClient client)
        {
            bool removed = false;
            lock (sessions)
            {
                SessionClient current;
                if (sessions.TryGetValue(client.Session, out current) && current == client)
                {
                    sessions.Remove(client.Session);
                    removed = true;
                }
            }
            if (removed)
            {
                host.Send(Message("session", "session", (long)client.Session, "state", "down"));
                host.Send(Message("windows", "windows", new List<object>(), "session", (long)client.Session));
            }
        }

        internal void FromSession(SessionClient client, Dictionary<string, object> message)
        {
            object id;
            var key = message.TryGetValue("id", out id) ? id as string : null;
            if (key != null)
            {
                lock (asked)
                {
                    Asked box;
                    if (asked.TryGetValue(key, out box))
                    {
                        box.Reply = message;
                        box.Done.Set();
                        return;
                    }
                }
            }
            message["session"] = (long)client.Session;
            host.Send(message);
        }
    }

    sealed class SessionClient
    {
        readonly GuestService service;
        readonly NamedPipeServerStream pipe;
        readonly Lines lines;
        public readonly uint Session;
        public DateTime Since = DateTime.UtcNow;

        public SessionClient(GuestService service, NamedPipeServerStream pipe, uint session)
        {
            this.service = service;
            this.pipe = pipe;
            Session = session;
            lines = new Lines(pipe);
        }

        public void Send(Dictionary<string, object> message)
        {
            try { lines.Write(message); }
            catch (Exception) { Close(); }
        }

        public void Close()
        {
            try { pipe.Dispose(); } catch (Exception) { }
        }

        public void Run()
        {
            bool registered = false;
            try
            {
                string line;
                while ((line = lines.ReadLine()) != null)
                {
                    Dictionary<string, object> message;
                    try { message = Json.ParseObject(line); }
                    catch (Exception) { continue; }
                    if (message == null) continue;
                    if (Json.Str(message, "type") == "session-hello")
                    {
                        if (!registered) service.Register(this);
                        registered = true;
                        continue;
                    }
                    if (registered) service.FromSession(this, message);
                }
            }
            catch (Exception)
            {
            }
            finally
            {
                if (registered) service.Unregister(this);
                Close();
            }
        }
    }
}
