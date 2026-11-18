// launch.cs - запускает команду в интерактивной сессии пользователя.
// Нужно потому, что терминал агента живёт в session 0 под SYSTEM, а vswhere 3.1.7 там
// падает с 0x80070008 (ERROR_NOT_ENOUGH_MEMORY).
// Сборка: csc.exe /out:launch.exe launch.cs
using System;
using System.Runtime.InteropServices;

static class Launch
{
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    struct LUID { public uint Low; public int High; }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    struct TOKEN_PRIVILEGES { public uint Count; public LUID Luid; public uint Attributes; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO
    {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2;
        public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool LookupPrivilegeValue(string system, string name, out LUID luid);
    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TOKEN_PRIVILEGES newState,
        uint bufferLength, IntPtr previousState, IntPtr returnLength);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("wtsapi32.dll", SetLastError = true)]
    static extern bool WTSQueryUserToken(int sessionId, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool DuplicateTokenEx(IntPtr existing, uint access, IntPtr attributes, int impersonationLevel,
        int tokenType, out IntPtr newToken);
    [DllImport("userenv.dll", SetLastError = true)]
    static extern bool CreateEnvironmentBlock(out IntPtr env, IntPtr token, bool inherit);
    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool CreateProcessAsUser(IntPtr token, string app, string cmd, IntPtr procAttrs,
        IntPtr threadAttrs, bool inherit, uint flags, IntPtr env, string dir,
        ref STARTUPINFO si, out PROCESS_INFORMATION pi);

    static void EnablePrivilege(IntPtr token, string name)
    {
        LUID luid;
        if (!LookupPrivilegeValue(null, name, out luid)) { Console.WriteLine("priv {0}: lookup failed {1}", name, Marshal.GetLastWin32Error()); return; }
        TOKEN_PRIVILEGES tp = new TOKEN_PRIVILEGES();
        tp.Count = 1; tp.Luid = luid; tp.Attributes = 2; // SE_PRIVILEGE_ENABLED
        bool ok = AdjustTokenPrivileges(token, false, ref tp, 0, IntPtr.Zero, IntPtr.Zero);
        Console.WriteLine("priv {0}: {1} (err {2})", name, ok, Marshal.GetLastWin32Error());
    }

    static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.WriteLine("usage: launch.exe <sessionId> <commandLine> [workDir]");
            return 2;
        }
        int session = int.Parse(args[0]);
        string commandLine = args[1];
        string workDir = args.Length > 2 ? args[2] : null;

        IntPtr self;
        OpenProcessToken(GetCurrentProcess(), 0x28 /* TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY */, out self);
        EnablePrivilege(self, "SeAssignPrimaryTokenPrivilege");
        EnablePrivilege(self, "SeIncreaseQuotaPrivilege");
        EnablePrivilege(self, "SeTcbPrivilege");

        IntPtr userToken;
        if (!WTSQueryUserToken(session, out userToken))
        {
            Console.WriteLine("WTSQueryUserToken failed: {0}", Marshal.GetLastWin32Error());
            return 1;
        }
        IntPtr primary;
        if (!DuplicateTokenEx(userToken, 0x02000000 /* MAXIMUM_ALLOWED */, IntPtr.Zero, 2 /* SecurityImpersonation */,
                1 /* TokenPrimary */, out primary))
        {
            Console.WriteLine("DuplicateTokenEx failed: {0}", Marshal.GetLastWin32Error());
            return 1;
        }
        IntPtr env = IntPtr.Zero;
        CreateEnvironmentBlock(out env, primary, false);

        STARTUPINFO si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        si.lpDesktop = "winsta0\\default";
        PROCESS_INFORMATION pi;
        bool ok = CreateProcessAsUser(primary, null, commandLine, IntPtr.Zero, IntPtr.Zero, false,
            0x00000400 /* CREATE_UNICODE_ENVIRONMENT */
            | 0x08000000 /* CREATE_NO_WINDOW: no console window flashing on the user desktop */,
            env, workDir, ref si, out pi);
        if (!ok)
        {
            Console.WriteLine("CreateProcessAsUser failed: {0}", Marshal.GetLastWin32Error());
            return 1;
        }
        Console.WriteLine("started pid={0} in session {1}", pi.dwProcessId, session);
        return 0;
    }
}
