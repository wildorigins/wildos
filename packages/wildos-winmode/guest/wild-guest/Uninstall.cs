using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using Microsoft.Win32;

namespace WildOS.Guest
{
    // Uninstalling a Windows app from Wild OS. A program from the Store is
    // removed here and now; any other program is removed by its own
    // uninstaller, the entry Windows' Installed apps lists for it, which Wild
    // OS opens in a window so the person sees it through.
    static class Uninstall
    {
        const string Key = @"Software\Microsoft\Windows\CurrentVersion\Uninstall";
        const string WowKey = @"Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall";

        public static Dictionary<string, object> Run(string path, string account)
        {
            var result = new Dictionary<string, object>();
            string aumid = Packages.AumidFor(path ?? "");
            if (aumid != null)
            {
                result["type"] = "uninstalled";
                string error = RemovePackage(Packages.Family(aumid));
                if (error != null)
                {
                    result["type"] = "error";
                    result["error"] = error;
                }
                return result;
            }
            var entry = Find(path, account);
            if (entry == null)
            {
                result["type"] = "error";
                result["error"] = "no-uninstaller";
                return result;
            }
            result["type"] = "uninstaller";
            result["name"] = entry.Name;
            // Split here, where the quoting rules are Windows' own: many
            // uninstallers leave a path with spaces unquoted.
            string program = Program(entry.Command);
            int at = entry.Command.IndexOf(program, StringComparison.OrdinalIgnoreCase);
            string rest = at < 0 ? "" : entry.Command.Substring(at + program.Length).TrimStart(new[] { '"' }).Trim();
            result["program"] = program;
            result["arguments"] = rest;
            return result;
        }

        // Removed for every account, as the service runs as LocalSystem; the
        // Windows mode account is the only one that uses it. The family
        // comes from Packages' own pattern, so it is never more than letters,
        // digits, dots, dashes and one underscore.
        static string RemovePackage(string family)
        {
            if (string.IsNullOrEmpty(family)) return "not-a-package";
            string script = "$ErrorActionPreference='Stop'; " +
                "$p = @(Get-AppxPackage -AllUsers | Where-Object { $_.PackageFamilyName -eq '" + family + "' -and -not $_.IsFramework }); " +
                "if ($p.Count -eq 0) { exit 3 }; " +
                "foreach ($x in $p) { Remove-AppxPackage -Package $x.PackageFullName -AllUsers }; exit 0";
            var start = new ProcessStartInfo("powershell.exe", "-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + script + "\"");
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            var p = Process.Start(start);
            if (p == null) return "powershell-failed";
            if (!p.WaitForExit(180000)) return "timeout";
            if (p.ExitCode == 3) return "not-installed";
            return p.ExitCode == 0 ? null : "remove-failed";
        }

        class Entry
        {
            public string Name;
            public string Command;
            public int Score;
        }

        static Entry Find(string target, string account)
        {
            if (string.IsNullOrEmpty(target)) return null;
            string program = Unquote(target);
            string folder = Path.GetDirectoryName(program) ?? "";
            Entry best = null;
            bool tied = false;
            var roots = new List<RegistryKey> { Registry.LocalMachine };
            RegistryKey user = AccountHive(account);
            if (user != null) roots.Add(user);
            foreach (RegistryKey root in roots)
            {
                foreach (string keyPath in new[] { Key, WowKey })
                {
                    RegistryKey list;
                    try { list = root.OpenSubKey(keyPath); }
                    catch (Exception) { continue; }
                    if (list == null) continue;
                    using (list)
                    {
                        foreach (string name in list.GetSubKeyNames())
                        {
                            Entry e = Read(list, name, program, folder);
                            if (e == null) continue;
                            if (best == null || e.Score > best.Score) { best = e; tied = false; }
                            else if (e.Score == best.Score && e.Command != best.Command) tied = true;
                        }
                    }
                }
            }
            if (user != null) user.Dispose();
            return tied ? null : best;
        }

        static Entry Read(RegistryKey list, string name, string program, string folder)
        {
            RegistryKey k;
            try { k = list.OpenSubKey(name); }
            catch (Exception) { return null; }
            if (k == null) return null;
            using (k)
            {
                string uninstall = k.GetValue("UninstallString") as string;
                if (string.IsNullOrEmpty(uninstall) || Number(k.GetValue("SystemComponent")) == 1 || k.GetValue("ParentKeyName") != null) return null;

                int score = 0;
                string icon = k.GetValue("DisplayIcon") as string;
                if (!string.IsNullOrEmpty(icon))
                {
                    string iconPath = Unquote(icon);
                    int comma = iconPath.LastIndexOf(',');
                    if (comma > 2 && !File.Exists(iconPath)) iconPath = iconPath.Substring(0, comma);
                    if (string.Equals(iconPath, program, StringComparison.OrdinalIgnoreCase)) score = 300;
                }
                string location = k.GetValue("InstallLocation") as string;
                if (score == 0 && Owns(location, folder)) score = 200 + Unquote(location).Length;
                // An uninstaller beside the program is its own. One that is
                // the program itself may be a launcher removing something
                // else it installed (Steam removing a game), so it ranks
                // below, and a tie between two entries is never guessed.
                string remover = Program(uninstall);
                if (score == 0 && Owns(Path.GetDirectoryName(remover), folder))
                {
                    score = string.Equals(remover, program, StringComparison.OrdinalIgnoreCase) ? 50 : 100;
                }
                if (score == 0) return null;

                // An MSI entry's UninstallString often opens Change (/I), so
                // the product code is uninstalled directly instead.
                string command = uninstall;
                if (Number(k.GetValue("WindowsInstaller")) == 1 && name.StartsWith("{", StringComparison.Ordinal) && name.EndsWith("}", StringComparison.Ordinal))
                {
                    command = "MsiExec.exe /X" + name;
                }
                return new Entry { Name = (k.GetValue("DisplayName") as string) ?? name, Command = command, Score = score };
            }
        }

        // Whether an app's own folder holds the program. Folders every app
        // shares (Program Files, AppData, Windows) never count, or every
        // program in them would match.
        static bool Owns(string owner, string folder)
        {
            if (string.IsNullOrEmpty(owner) || string.IsNullOrEmpty(folder)) return false;
            string o = Unquote(owner).TrimEnd(new[] { '\\' });
            if (o.Length < 4 || Shared(o)) return false;
            return string.Equals(folder, o, StringComparison.OrdinalIgnoreCase)
                || folder.StartsWith(o + "\\", StringComparison.OrdinalIgnoreCase);
        }

        static bool Shared(string folder)
        {
            var shared = new List<string>
            {
                Environment.GetEnvironmentVariable("ProgramFiles"),
                Environment.GetEnvironmentVariable("ProgramFiles(x86)"),
                Environment.GetEnvironmentVariable("ProgramData"),
                Environment.GetEnvironmentVariable("CommonProgramFiles"),
                Environment.GetEnvironmentVariable("CommonProgramFiles(x86)"),
                Environment.GetEnvironmentVariable("SystemRoot"),
            };
            foreach (string s in shared)
            {
                if (!string.IsNullOrEmpty(s) && string.Equals(folder, s.TrimEnd(new[] { '\\' }), StringComparison.OrdinalIgnoreCase)) return true;
            }
            // Everything an account's own installs share: its profile,
            // AppData and the folders under it that hold many apps.
            string[] parts = folder.Split(new[] { '\\' });
            int users = -1;
            for (int i = 0; i < parts.Length; i++)
            {
                if (string.Equals(parts[i], "Users", StringComparison.OrdinalIgnoreCase)) { users = i; break; }
            }
            if (users < 0) return false;
            int below = parts.Length - users - 2;
            if (below <= 0) return true;
            string rest = string.Join("\\", parts, users + 2, below).ToLowerInvariant();
            return rest == "appdata" || rest == "appdata\\local" || rest == "appdata\\roaming" || rest == "appdata\\locallow"
                || rest == "appdata\\local\\programs" || rest == "appdata\\local\\microsoft" || rest == "appdata\\roaming\\microsoft";
        }

        // The account's own registry, where programs installed for it alone
        // list themselves. It is loaded while the account is signed in,
        // which Windows mode's session is whenever an app is open.
        static RegistryKey AccountHive(string account)
        {
            if (string.IsNullOrEmpty(account)) return null;
            try
            {
                using (var profiles = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList"))
                {
                    if (profiles == null) return null;
                    foreach (string sid in profiles.GetSubKeyNames())
                    {
                        using (var p = profiles.OpenSubKey(sid))
                        {
                            string home = p == null ? null : p.GetValue("ProfileImagePath") as string;
                            if (home != null && home.EndsWith("\\" + account, StringComparison.OrdinalIgnoreCase))
                            {
                                return Registry.Users.OpenSubKey(sid);
                            }
                        }
                    }
                }
            }
            catch (Exception e)
            {
                Log.Write("uninstall: " + e.Message);
            }
            return null;
        }

        // The program a command line starts: quoted, or up to the first
        // ".exe" for the unquoted ones with spaces in their folder.
        static string Program(string command)
        {
            string c = command.Trim();
            if (c.StartsWith("\"", StringComparison.Ordinal))
            {
                int end = c.IndexOf('"', 1);
                return end > 1 ? c.Substring(1, end - 1) : c.Trim(new[] { '"' });
            }
            string lower = c.ToLowerInvariant();
            int exe = lower.IndexOf(".exe", StringComparison.Ordinal);
            return exe > 0 ? c.Substring(0, exe + 4) : c;
        }

        static string Unquote(string value)
        {
            return (value ?? "").Trim().Trim(new[] { '"' });
        }

        static long Number(object value)
        {
            if (value == null) return 0;
            try { return Convert.ToInt64(value, CultureInfo.InvariantCulture); }
            catch (Exception) { return 0; }
        }
    }
}
