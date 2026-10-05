using System.Diagnostics;

namespace PrimexGlass.Services;

public static class SystemManager
{
	private static void Run(string exe, string args)
	{
		try
		{
			Process.Start(new ProcessStartInfo
			{
				FileName = exe,
				Arguments = args,
				WindowStyle = ProcessWindowStyle.Hidden,
				CreateNoWindow = true
			})?.WaitForExit();
		}
		catch { }
	}

	public static void FlushDNS() => Run("ipconfig", "/flushdns");

	public static void ClearArp() => Run("arp", "-d *");

	public static void RestartExplorer() =>
		Run("cmd.exe", "/c taskkill /f /im explorer.exe & start explorer.exe");

	public static void KillProcesses(string[] names)
	{
		foreach (string n in names)
		{
			foreach (Process p in Process.GetProcessesByName(n.Replace(".exe", "")))
			{
				try { p.Kill(); } catch { }
			}
		}
	}
}
