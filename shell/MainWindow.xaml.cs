using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using Microsoft.Web.WebView2.Core;
using PrimexGlass.Services;

namespace PrimexGlass;

public partial class MainWindow : Window
{
	private readonly HotkeyManager _hotkeys = new();
	private string? _hotkeyTarget;
	private int _dragOX;
	private int _dragOY;

	public MainWindow()
	{
		InitializeComponent();
		_hotkeys.Log += m => PostEvent("notify", new { title = "Hotkey", body = m });
		Loaded += async (_, _) => { ApplyRoundedCorners(); await InitWebViewAsync(); };
		Closing += (_, _) => { _hotkeys.Dispose(); };
		StateChanged += (_, _) => PostEvent("win.state",
			new { maximized = WindowState == WindowState.Maximized });
	}

	private const int DwmwaWindowCornerPreference = 33;
	private const int DwmwcpRound = 2;

	private void ApplyRoundedCorners()
	{
		try
		{
			var hwnd = new System.Windows.Interop.WindowInteropHelper(this).Handle;
			if (hwnd == IntPtr.Zero) return;
			int pref = DwmwcpRound;
			DwmSetWindowAttribute(hwnd, DwmwaWindowCornerPreference, ref pref, sizeof(int));
		}
		catch { }
	}

	[DllImport("dwmapi.dll", PreserveSig = true)]
	private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

	private async Task InitWebViewAsync()
	{
		var env = await CoreWebView2Environment.CreateAsync();
		await Web.EnsureCoreWebView2Async(env);
		Web.CoreWebView2.Settings.AreDefaultContextMenusEnabled = false;
		Web.CoreWebView2.Settings.AreDevToolsEnabled = true;
		Web.CoreWebView2.WebMessageReceived += OnMessage;

		string? dev = Environment.GetEnvironmentVariable("PRIMEUX_DEV");
		if (!string.IsNullOrEmpty(dev))
		{
			Web.CoreWebView2.Navigate("http://localhost:1420/");
			return;
		}
		string dist = Path.Combine(AppContext.BaseDirectory, "dist");
		if (!Directory.Exists(dist))
		{
			// Dev fallback: frontend folder next to shell during development.
			dist = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "frontend", "dist"));
		}
		if (!Directory.Exists(dist))
		{
			MessageBox.Show(
				"UI folder not found:\n" + dist + "\n\nRun the EXE from its publish folder (keep dist\\ next to it).",
				"ORBIT OPTIMIZER", MessageBoxButton.OK, MessageBoxImage.Error);
			Close();
			return;
		}
		try
		{
			Web.CoreWebView2.SetVirtualHostNameToFolderMapping(
				"primex.local", dist, CoreWebView2HostResourceAccessKind.Allow);
			Web.CoreWebView2.Navigate("https://primex.local/index.html?v=2.15.0");
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed to start UI:\n" + ex.Message,
				"ORBIT OPTIMIZER", MessageBoxButton.OK, MessageBoxImage.Error);
			Close();
		}
	}

	private void Post(object payload)
	{
		try
		{
			Dispatcher.Invoke(() =>
				Web.CoreWebView2?.PostWebMessageAsJson(JsonSerializer.Serialize(payload)));
		}
		catch { }
	}

	private void PostEvent(string name, object? payload) =>
		Post(new { type = "event", name, payload });

	private void PostProgress(int pct, string msg) =>
		PostEvent("progress", new { pct, msg });

	private void OnMessage(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
	{
		JsonNode? root;
		try { root = JsonNode.Parse(e.WebMessageAsJson); }
		catch { return; }
		if (root is null) return;

		int id = root["id"]?.GetValue<int>() ?? -1;
		string cmd = root["cmd"]?.GetValue<string>() ?? "";
		JsonNode? args = root["args"];

		// Window chrome runs on the UI thread; everything else on a worker.
		if (cmd.StartsWith("win."))
		{
			Dispatcher.Invoke(() => HandleWindowCommand(cmd, args));
			Respond(id, new { });
			return;
		}

		_ = Task.Run(async () =>
		{
			try
			{
				object? data = await DispatchAsync(cmd, args);
				Respond(id, data, ok: true);
			}
			catch (Exception ex)
			{
				Respond(id, new { }, ok: false, error: ex.Message);
			}
		});
	}

	private void Respond(int id, object? data, bool ok = true, string error = "")
	{
		if (ok) Post(new { id, ok = true, data });
		else Post(new { id, ok = false, error });
	}

	private void HandleWindowCommand(string cmd, JsonNode? args)
	{
		switch (cmd)
		{
			case "win.min": WindowState = WindowState.Minimized; break;
			case "win.toggle":
				WindowState = WindowState == WindowState.Maximized
					? WindowState.Normal : WindowState.Maximized;
				break;
			case "win.close": Close(); break;
			case "win.drag":
				try { DragMove(); } catch { }
				break;
			case "win.dragstart":
			{
				if (WindowState != WindowState.Normal) break;
				_dragOX = (args?["x"]?.GetValue<int>() ?? 0) - (int)Left;
				_dragOY = (args?["y"]?.GetValue<int>() ?? 0) - (int)Top;
				break;
			}
			case "win.dragmove":
			{
				if (WindowState != WindowState.Normal) break;
				try
				{
					int x = args?["x"]?.GetValue<int>() ?? 0;
					int y = args?["y"]?.GetValue<int>() ?? 0;
					Left = x - _dragOX;
					Top = y - _dragOY;
				}
				catch { }
				break;
			}
		}
		PostEvent("win.state", new { maximized = WindowState == WindowState.Maximized });
	}

	private async Task<object?> DispatchAsync(string cmd, JsonNode? args)
	{
		switch (cmd)
		{
		case "sys.getInfo":
		{
			var (w, h) = Resolution.GetCurrent();
			return new { width = w, height = h, admin = IsAdmin() };
		}
		case "sys.disk":
		{
			var d = new System.IO.DriveInfo(System.IO.Path.GetPathRoot(Environment.SystemDirectory) ?? "C:\\");
			return new { total = d.TotalSize, free = d.AvailableFreeSpace, label = d.Name };
		}
		case "auth:getStatus":
		{
			var s = KeyAuthService.Current;
			bool pending = App.InitTask != null && !App.InitTask.IsCompleted;
			return new
			{
				pending,
				initOk = App.AuthInitOk,
				offline = App.AuthOffline,
				versionMismatch = App.AuthVersionMismatch,
				initError = App.AuthInitError,
				valid = s.Valid,
				username = s.Username,
				tier = s.Tier.ToString(),
				tierLabel = s.TierLabel,
				timeLeft = s.TimeLeft,
				expiry = s.ExpiryUnix,
				grace = s.Grace
			};
		}
		case "auth:autoLogin":
		{
			// Prefer Discord session restore (silent), then KeyAuth offline grace.
			try
			{
				var (dok, dsession, dnote) = await DiscordAuthService.AutoLoginAsync();
				if (dok && dsession.Valid)
				{
					KeyAuthService.SetCurrent(dsession);
					KeyAuthService.DiscordActive = true;
						return new
						{
							ok = true,
							note = dnote,
							valid = dsession.Valid,
							username = dsession.Username,
							avatar = dsession.Avatar,
							tier = dsession.Tier.ToString(),
							tierLabel = dsession.TierLabel,
							timeLeft = dsession.TimeLeft,
							expiry = dsession.ExpiryUnix,
							grace = dsession.Grace
						};
				}
			}
			catch { /* fall through to KeyAuth */ }
			var (ok, session, note) = await KeyAuthService.AutoLoginAsync();
			return new
			{
				ok,
				note,
				valid = session.Valid,
				username = session.Username,
				avatar = session.Avatar,
				tier = session.Tier.ToString(),
				tierLabel = session.TierLabel,
				timeLeft = session.TimeLeft,
				expiry = session.ExpiryUnix,
				grace = session.Grace
			};
		}
		case "auth:discordLogin":
		{
			var s = await DiscordAuthService.LoginAsync();
			KeyAuthService.SetCurrent(s);
			KeyAuthService.DiscordActive = true;
			return SessionDto(s);
		}
		case "auth:login":
		{
			string u = args?["username"]?.GetValue<string>() ?? "";
			string p = args?["password"]?.GetValue<string>() ?? "";
			if (string.IsNullOrWhiteSpace(u) || string.IsNullOrEmpty(p))
				throw new Exception("Enter your username and password.");
			var (ok, error, session) = await KeyAuthService.LoginAsync(u, p);
			if (!ok) throw new Exception(error);
			return SessionDto(session);
		}
		case "auth:register":
		{
			string u = args?["username"]?.GetValue<string>() ?? "";
			string p = args?["password"]?.GetValue<string>() ?? "";
			if (u.Length < 3) throw new Exception("Username needs at least 3 characters.");
			if (p.Length < 4) throw new Exception("Password needs at least 4 characters.");
			var (ok, error, session) = await KeyAuthService.RegisterAsync(u, p);
			if (!ok) throw new Exception(error);
			return SessionDto(session);
		}
		case "auth:activateKey":
		{
			string k = args?["key"]?.GetValue<string>()?.Trim() ?? "";
			if (string.IsNullOrEmpty(k)) throw new Exception("Paste a license key first.");
			var (ok, error, session) = await KeyAuthService.ActivateAsync(k);
			if (!ok) throw new Exception(error);
			return SessionDto(session);
		}
		case "auth:logout":
			await KeyAuthService.LogoutAsync();
			return new { message = "Signed out." };
		case "auth:getHWID":
			return new { hwid = KeyAuthService.LocalHwid() };
		case "auth:revalidate":
		{
			var session = await KeyAuthService.RevalidateAsync();
			return SessionDto(session);
		}
		case "sys.openUrl":
		{
			string url = args?["url"]?.GetValue<string>() ?? "";
			if (!url.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
				throw new Exception("Only https links allowed.");
			Process.Start(new ProcessStartInfo { FileName = url, UseShellExecute = true });
			return new { message = "Opened." };
		}
		case "aim.status":
			return new { hasBackup = AimRegistry.HasBackup() };
		case "aimreg.apply":
			await Task.Run(() => AimRegistry.ApplyAimReg());
			return new { message = "AIM REG applied (your values). Log off + back on to load it." };
		case "aimreg.revert":
		{
			bool ok = AimRegistry.Restore();
			if (!ok) throw new Exception("No backup found Ã¢â‚¬â€ nothing to restore.");
			return new { message = "Mouse settings restored. Log off + back on to load it." };
		}
		case "mouse.state":
		{
			var (accelOn, precisionOn) = AimRegistry.GetState();
			return new { accel = accelOn, precision = precisionOn };
		}
		case "mouse.accel":
		{
			bool on = args?["on"]?.GetValue<bool>() ?? true;
			await Task.Run(() => AimRegistry.SetAccel(on));
			return new { message = on ? "Acceleration curves on." : "Acceleration OFF (flat 1:1). Log off to load." };
		}
		case "mouse.precision":
		{
			bool on = args?["on"]?.GetValue<bool>() ?? true;
			await Task.Run(() => AimRegistry.SetPrecision(on));
			return new { message = on ? "Pointer precision on." : "Pointer precision OFF. Log off to load." };
		}
			case "aim.apply":
				AimRegistry.Apply();
				return new { message = "Aim smoothing applied. Log off + back on to load it." };
			case "aim.restore":
			{
				bool ok = AimRegistry.Restore();
				if (!ok) throw new Exception("No backup found Ã¢â‚¬â€ nothing to restore.");
				return new { message = "Mouse settings restored. Log off + back on to load it." };
			}
		case "sys.stats":
		{
			var s = SysStats.Get();
			return new
			{
				cpu = s.CpuLoad, cpuTemp = s.CpuTemp,
				gpu = s.GpuLoad, gpuTemp = s.GpuTemp, gpuName = s.GpuName,
				ramPct = s.RamUsedPct, ramGb = s.RamUsedGb, ramTotal = s.RamTotalGb
			};
		}
		case "booster.run":
		{
			string msg = await Booster.RunAsync((pct, m) => PostProgress(pct, m));
			return new { message = msg };
		}
		case "game.boost":
		{
			var boosted = await Task.Run(() => Booster.PrioritizeGames());
			string who = boosted.Count > 0 ? string.Join(", ", boosted) : "no game running";
			return new { message = $"Priority boosted: {who}." };
		}
			case "clean.boost":
				return CleanBoost();
			case "clean.adv":
				return AdvBoost();
			case "clean.game":
				return GameBoost();
			case "clean.deep":
				return DeepClean();
			case "hotkey.start":
			{
				int vk = args?["vk"]?.GetValue<int>() ?? 0;
				if (vk == 0) throw new Exception("No key selected.");
				var (w, h) = Resolution.GetCurrent();
				_hotkeyTarget = $"{w}x{h}";
				_hotkeys.StartVk(vk, () => _ = AimFixViaIpc(w, h));
				return new { message = $"Hotkey bound -> Aim Optimizer ({w}x{h})." };
			}
			case "hotkey.stop":
				_hotkeys.Stop();
				return new { message = "Hotkey stopped." };
			case "hotkey.status":
				return new { running = _hotkeys.IsRunning, bound = _hotkeys.BoundName };
			case "tweaks.groups":
				return TweaksService.ListGroups().Select(g => new
				{
					id = g.Id, title = g.Title, desc = g.Desc,
					danger = g.Danger, count = g.Count, dynamic = g.Dynamic,
					category = g.Category,
					premium = g.Premium,
					backup = TweaksService.HasBackup(g.Id)
				}).ToList();
		case "tweaks.apply":
		{
			string id0 = args?["id"]?.GetValue<string>() ?? "";
			if (string.IsNullOrEmpty(id0)) throw new Exception("No group id.");
			RequirePremium(id0);
			string msg0 = await Task.Run(() =>
				TweaksService.ApplyGroup(id0, (pct, m) => PostProgress(pct, m)));
			return new { message = msg0 };
		}
		case "tweaks.entries":
		{
			string id = args?["id"]?.GetValue<string>() ?? "";
			if (string.IsNullOrEmpty(id)) throw new Exception("No group id.");
			return TweaksService.ListEntries(id);
		}
		case "tweaks.applyEntries":
		{
			string id = args?["id"]?.GetValue<string>() ?? "";
			var arr = args?["entries"]?.AsArray();
			if (string.IsNullOrEmpty(id)) throw new Exception("No group id.");
			if (arr == null || arr.Count == 0) throw new Exception("No entries selected.");
			RequirePremium(id);
			string msg = await Task.Run(() =>
				TweaksService.ApplyEntryList(id, arr.ToList(), (pct, m) => PostProgress(pct, m)));
			return new { message = msg };
		}
			case "tweaks.revert":
			{
				string id = args?["id"]?.GetValue<string>() ?? "";
				if (string.IsNullOrEmpty(id)) throw new Exception("No group id.");
				string msg = await Task.Run(() =>
					TweaksService.RevertGroup(id, (pct, m) => PostProgress(pct, m)));
				return new { message = msg };
			}
		case "debloat.list":
			return await Task.Run(() => DebloatService.ListPackages());
		case "debloat.presets":
			return DebloatService.Presets();
		case "debloat.remove":
		{
			var pkgs = args?["packages"]?.AsArray()
				?.Select(n => n?.GetValue<string>())
				?.Where(s => !string.IsNullOrEmpty(s))
				?.Cast<string>()
				?.ToList() ?? new List<string>();
			if (pkgs.Count == 0) throw new Exception("No packages selected.");
			if (!IsAdmin()) throw new Exception("Administrator required to remove apps.");
			string dmsg = await Task.Run(() =>
				DebloatService.Remove(pkgs, (pct, m) => PostProgress(pct, m)));
			return new { message = dmsg };
		}
		case "services.list":
			return await Task.Run(() => WinServices.List());
		case "services.set":
		{
			string name = args?["name"]?.GetValue<string>() ?? "";
			string action = args?["action"]?.GetValue<string>() ?? "";
			if (string.IsNullOrEmpty(name)) throw new Exception("No service name.");
			if (string.IsNullOrEmpty(action)) throw new Exception("No service action.");
			if (!IsAdmin()) throw new Exception("Administrator required to change services.");
			return await Task.Run(() => WinServices.Configure(name, action));
		}
		case "services.safeDisable":
		{
			if (!IsAdmin()) throw new Exception("Administrator required to disable services.");
			return await Task.Run(() =>
				WinServices.SafeDisable((pct, m) => PostProgress(pct, m)));
		}
		case "services.disablePack":
		{
			string pack = args?["pack"]?.GetValue<string>() ?? "";
			if (string.IsNullOrEmpty(pack)) throw new Exception("No service pack.");
			if (!IsAdmin()) throw new Exception("Administrator required to disable services.");
			return await Task.Run(() =>
				WinServices.DisableNamed(pack, (pct, m) => PostProgress(pct, m)));
		}
		default:
				throw new Exception($"Unknown command '{cmd}'.");
		}
	}

	// Server-side premium gate (mirrors native IsPremiumFeature/RequirePremium).
	private void RequirePremium(string featureId)
	{
		if (!TweaksService.IsPremium(featureId)) return;
		var tier = KeyAuthService.Current.Tier;
		if (tier != KeyAuthService.Tier.Lifetime)
			throw new Exception("PREMIUM::This feature requires PRIMEx Premium.");
	}

	private static object SessionDto(KeyAuthService.AuthSession s) => new
	{
		valid = s.Valid,
		username = s.Username,
		avatar = s.Avatar,
		tier = s.Tier.ToString(),
		tierLabel = s.TierLabel,
		timeLeft = s.TimeLeft,
		expiry = s.ExpiryUnix,
		grace = s.Grace
	};

	private async Task AimFixViaIpc(int w, int h)
	{
		try { await Booster.RunAsync((pct, msg) => PostProgress(pct, msg)); }
		catch { }
		PostEvent("notify", new { title = "Hotkey", body = "Booster finished." });
	}

	private static bool IsAdmin()
	{
		try
		{
			var id = System.Security.Principal.WindowsIdentity.GetCurrent();
			return new System.Security.Principal.WindowsPrincipal(id)
				.IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator);
		}
		catch { return false; }
	}


	private object CleanBoost()
	{
		int n = 0;
		n += Cleaner.CleanPath(Path.GetTempPath());
		n += Cleaner.CleanPath(@"C:\Windows\Temp");
		n += Cleaner.CleanPath(@"C:\Windows\Prefetch");
		RamBooster.FlushCore();
		SystemManager.FlushDNS();
		return new { message = $"Clean & Boost complete Ã¢â‚¬â€ {n} items removed." };
	}

	private object AdvBoost()
	{
		SystemManager.KillProcesses(new[] { "OneDrive", "GameBar", "Widgets" });
		RamBooster.FlushCore();
		SystemManager.RestartExplorer();
		Thread.Sleep(1000);
		SystemManager.FlushDNS();
		SystemManager.ClearArp();
		return new { message = "Advanced Boost complete." };
	}

	private object GameBoost()
	{
		SystemManager.KillProcesses(new[] { "chrome", "msedge", "firefox" });
		SystemManager.KillProcesses(new[] { "Spotify", "Teams" });
		RamBooster.FlushCore();
		return new { message = "Game Boost complete Ã¢â‚¬â€ background apps closed." };
	}

	private object DeepClean()
	{
		string temp = Path.GetTempPath().TrimEnd('\\');
		string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
		string[] dirs = new[]
		{
			temp,
			@"C:\Windows\Temp",
			@"C:\Windows\Prefetch",
			@"C:\Windows\SoftwareDistribution\Download",
			@"C:\Windows\Logs\CBS",
			Path.Combine(local, @"Microsoft\Windows\INetCache"),
			Path.Combine(local, @"Microsoft\Windows\Explorer"),
			Path.Combine(local, @"Microsoft\Windows\WER"),
			@"C:\ProgramData\Microsoft\Windows\WER\ReportArchive",
			@"C:\ProgramData\Microsoft\Windows\WER\ReportQueue",
			@"C:\Windows\Minidump",
			@"C:\Windows\LiveKernelReports",
		}.Where(d =>
			!string.IsNullOrWhiteSpace(d) &&
			!d.StartsWith(temp + "\\", StringComparison.OrdinalIgnoreCase) // never wipe our own run dir
		).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();

		int n = 0;
		int i = 0;
		foreach (string d in dirs)
		{
			i++;
			n += Cleaner.ForceCleanCount(d);
			PostProgress(i * 100 / dirs.Length, $"Cleaning {d}Ã¢â‚¬Â¦");
		}

		// Recycle bin + delivery optimization cache.
		try
		{
			n += Cleaner.ForceCleanCount(@"C:\$Recycle.Bin");
		}
		catch { }
		try
		{
			var doCache = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),
				@"ServiceProfiles\NetworkService\AppData\Local\Microsoft\Windows\DeliveryOptimization\Cache");
			n += Cleaner.ForceCleanCount(doCache);
		}
		catch { }

		// Clear Windows event logs (safe Ã¢â‚¬â€ frees space, no config lost).
		try
		{
			foreach (var log in System.Diagnostics.EventLog.GetEventLogs())
			{
				try { log.Clear(); n++; } catch { }
			}
		}
		catch { }

		RamBooster.FlushCore();
		SystemManager.FlushDNS();
		return new { message = $"Deep Clean complete Ã¢â‚¬â€ {n} items deleted (temp, prefetch, update cache, WER, logs, recycle bin)." };
	}
}
