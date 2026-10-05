using System.Diagnostics;
using System.Text.Json;

namespace PrimexGlass.Services;

// AppX package list/remove for the Debloat tab (mirrors native debloat.cpp).
public static class DebloatService
{
	public record Pkg(string Name, string FullName, string DisplayName);

	private static string RunPs(string script, int timeoutMs = 60000)
	{
		var psi = new ProcessStartInfo
		{
			FileName = "powershell.exe",
			Arguments = "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" +
				script.Replace("\"", "\\\"") + "\"",
			UseShellExecute = false,
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			CreateNoWindow = true,
			StandardOutputEncoding = System.Text.Encoding.UTF8
		};
		using var p = Process.Start(psi) ?? throw new Exception("Failed to start PowerShell.");
		string stdout = p.StandardOutput.ReadToEnd();
		_ = p.StandardError.ReadToEnd();
		if (!p.WaitForExit(timeoutMs))
		{
			try { p.Kill(entireProcessTree: true); } catch { }
			throw new Exception("PowerShell timed out.");
		}
		return stdout.TrimStart('﻿');
	}

	public static List<Pkg> ListPackages()
	{
		var raw = RunPs(
			"$ErrorActionPreference='SilentlyContinue'; " +
			"Get-AppxPackage | Select-Object Name,PackageFullName,DisplayName | ConvertTo-Json -Compress -Depth 3",
			45000);
		if (string.IsNullOrWhiteSpace(raw)) return new List<Pkg>();
		try
		{
			using var doc = JsonDocument.Parse(raw);
			var root = doc.RootElement;
			var list = new List<Pkg>();
			if (root.ValueKind == JsonValueKind.Object)
				list.Add(ReadPkg(root));
			else if (root.ValueKind == JsonValueKind.Array)
			{
				foreach (var o in root.EnumerateArray())
					if (o.ValueKind == JsonValueKind.Object)
						list.Add(ReadPkg(o));
			}
			return list.Where(p => !string.IsNullOrEmpty(p.FullName)).ToList();
		}
		catch { return new List<Pkg>(); }
	}

	private static Pkg ReadPkg(JsonElement o)
	{
		string Get(string k, string fb = "") =>
			o.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.String
				? v.GetString() ?? fb : fb;
		string name = Get("Name");
		string display = Get("DisplayName", name);
		return new Pkg(name, Get("PackageFullName"), display);
	}

		public static object Presets() => new[]
		{
			new
			{
				id = "xbox",
				label = "Xbox & gaming stubs",
				patterns = new[]
				{
					"Microsoft.GamingApp", "Microsoft.XboxApp", "Microsoft.XboxGamingOverlay",
					"Microsoft.XboxIdentityProvider", "Microsoft.XboxSpeechToTextOverlay",
					"Microsoft.XboxTcUI", "Microsoft.GamingServices", "Microsoft.XboxGameCallableUI"
				}
			},
			new
			{
				id = "social",
				label = "Social & phone-link stubs",
				patterns = new[]
				{
					"Microsoft.SkypeApp", "Microsoft.People", "Microsoft.YourPhone",
					"Microsoft.WindowsCommunicationsApps", "MicrosoftTeams"
				}
			},
			new
			{
				id = "bing",
				label = "Bing content apps",
				patterns = new[]
				{
					"Microsoft.BingNews", "Microsoft.BingWeather", "Microsoft.BingFinance",
					"Microsoft.BingSports", "Microsoft.BingFoodAndDrink", "Microsoft.BingHealthAndFitness",
					"Microsoft.BingTravel", "Microsoft.GetHelp", "Microsoft.Getstarted",
					"Microsoft.MicrosoftOfficeHub", "Microsoft.MicrosoftSolitaireCollection",
					"Microsoft.WindowsFeedbackHub", "Microsoft.WindowsMaps", "Microsoft.WindowsAlarms",
					"Microsoft.WindowsSoundRecorder", "Microsoft.PowerAutomateDesktop", "Microsoft.Todos",
					"Microsoft.Clipchamp", "Microsoft.MicrosoftStickyNotes", "Microsoft.OutlookForWindows"
				}
			},
			new
			{
				id = "cortana-ai",
				label = "Cortana / Windows AI stubs",
				patterns = new[]
				{
					"Microsoft.549981C3F5F10", "Microsoft.Windows.Copilot", "Microsoft.Copilot",
					"Microsoft.MicrosoftPCManager"
				}
			}
		};

	public static string Remove(List<string> packageFullNames, Action<int, string> report)
	{
		int removed = 0, failed = 0;
		int total = packageFullNames.Count;
		var fails = new List<string>();
		for (int i = 0; i < total; i++)
		{
			string fn = packageFullNames[i];
			report(i * 100 / Math.Max(total, 1), $"Removing {fn}...");
			string esc = fn.Replace("'", "''");
			string outp = RunPs(
				$"try {{ Remove-AppxPackage -Package '{esc}' -ErrorAction Stop; 'OK' }} catch {{ 'ERR:' + $_.Exception.Message }}",
				90000);
			if (outp.Contains("OK") && !outp.Contains("ERR:")) removed++;
			else { failed++; fails.Add(fn); }
		}
		report(100, $"Removed {removed}/{total}");
		string msg = $"Debloat complete: {removed} removed, {failed} failed.";
		if (fails.Count > 0)
			msg += " Failed: " + string.Join(", ", fails.Take(5)) + (fails.Count > 5 ? "…" : "");
		return msg;
	}
}
