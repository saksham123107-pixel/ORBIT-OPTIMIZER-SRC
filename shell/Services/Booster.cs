using System.Diagnostics;
using System.IO;

namespace PrimexGlass.Services;

// One-shot BOOSTER: bloatware kill + RAM trim + cleanup + deep clean +
// background-app kill + running-game priority (HD-Player first).
public static class Booster
{
	private static readonly string[] Bloatware =
		new[] { "OneDrive", "GameBar", "Widgets" };

	private static readonly string[] BackgroundApps =
		new[] { "chrome", "msedge", "firefox", "Spotify", "Teams" };

	// Checked in order — HD-Player (emulator) first.
	private static readonly string[] GameProcesses = new[]
	{
		"HD-Player",
		"cs2",
		"VALORANT-Win64-Shipping",
		"FortniteClient-Win64-Shipping",
		"r5apex",
		"GTA5",
		"RainbowSix",
		"ModernWarfare",
		"Overwatch",
		"RocketLeague"
	};

	public static async Task<string> RunAsync(Action<int, string> report)
	{
		report(5, "Killing bloatware…");
		await Task.Run(() => SystemManager.KillProcesses(Bloatware));

		report(20, "Trimming RAM…");
		await Task.Run(() => RamBooster.FlushCore());

		report(35, "Cleaning temp + prefetch…");
		int n = 0;
		n += await Task.Run(() => Cleaner.CleanPath(Path.GetTempPath()));
		n += await Task.Run(() => Cleaner.CleanPath(@"C:\Windows\Temp"));
		n += await Task.Run(() => Cleaner.CleanPath(@"C:\Windows\Prefetch"));

		report(50, "DNS flush…");
		await Task.Run(() => SystemManager.FlushDNS());

		report(60, "Deep clean…");
		n += await Task.Run(() => Cleaner.ForceCleanCount(Path.GetTempPath()));
		n += await Task.Run(() => Cleaner.ForceCleanCount(@"C:\Windows\Temp"));
		n += await Task.Run(() => Cleaner.ForceCleanCount(@"C:\Windows\Prefetch"));

		report(75, "Closing background apps…");
		await Task.Run(() => SystemManager.KillProcesses(BackgroundApps));

		report(88, "Boosting running games…");
		List<string> boosted = await Task.Run(PrioritizeGames);

		report(100, "Booster complete.");
		string who = boosted.Count > 0 ? string.Join(", ", boosted) : "no game running";
		return $"Booster complete — {n} items cleaned. Priority: {who}.";
	}

	public static List<string> PrioritizeGames()
	{
		var boosted = new List<string>();
		foreach (string name in GameProcesses)
		{
			Process[] ps;
			try { ps = Process.GetProcessesByName(name); }
			catch { continue; }
			foreach (Process p in ps)
			{
				try
				{
					p.PriorityClass = ProcessPriorityClass.High;
					if (!boosted.Contains(name))
						boosted.Add(name);
				}
				catch { }
				finally { try { p.Dispose(); } catch { } }
			}
		}
		return boosted;
	}
}
