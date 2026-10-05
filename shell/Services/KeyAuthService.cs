using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace PrimexGlass.Services;

// The ONLY file that touches the KeyAuth SDK. Every SDK call blocks,
// so each runs inside Task.Run — the UI thread never waits on auth.
public static class KeyAuthService
{
	private const string AppName = "PRIMEx"; // KeyAuth app name (not a secret)
	private static readonly string OwnerId = Environment.GetEnvironmentVariable("PRIMEX_KEYAUTH_OWNER_ID") ?? "<OWNER_ID>";
	private const string Version = "1.0";

	private static readonly KeyAuth.api Api = new(AppName, OwnerId, Version);
	private static readonly object Gate = new();

	public enum Tier { None, Trial, Monthly, Lifetime }

	public record AuthSession(
		bool Valid,
		string Username,
		Tier Tier,
		string TierLabel,
		string TimeLeft,
		long ExpiryUnix,
		string Hwid,
		bool Grace,
		string Avatar = "");

	private static AuthSession _current =
		new(false, "", Tier.None, "", "", 0, "", false);

	public static AuthSession Current => _current;

	// Set by DiscordAuthService after OAuth / offline restore.
	public static void SetCurrent(AuthSession s) => _current = s;

	// True when the live session came from Discord OAuth (not KeyAuth).
	public static bool DiscordActive { get; set; }
	public static string InitError { get; private set; } = "";
	public static bool InitNetworkOk { get; private set; }
	public static bool VersionMismatch { get; private set; }

	public static string LocalHwid()
	{
		try
		{
			// Stable machine+user fingerprint used as DPAPI entropy for auth.json.
			string sid = System.Security.Principal.WindowsIdentity.GetCurrent().User?.Value ?? "";
			string machine = "";
			try
			{
				using var k = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Cryptography");
				machine = k?.GetValue("MachineGuid") as string ?? "";
			}
			catch { }
			if (string.IsNullOrEmpty(machine)) machine = Environment.MachineName;
			return $"{sid}::{machine}";
		}
		catch { return Environment.MachineName; }
	}

	// ── init (call once at startup; bounded 25s, never hangs) ──
	public static async Task<bool> InitAsync()
	{
		try
		{
			AuthStore.MigrateLegacy();
			var work = Task.Run(() =>
			{
				lock (Gate)
				{
					try
					{
						Api.init();
					}
					catch (Exception ex)
					{
						InitNetworkOk = false;
						InitError = ex.Message;
						return false;
					}
					if (!Api.response.success)
					{
						InitNetworkOk = false;
						InitError = Api.response.message ?? "init failed";
						string m = (InitError ?? "").ToLowerInvariant();
						if (m.Contains("version") || m.Contains("update") || m.Contains("download"))
							VersionMismatch = true;
						return false;
					}
					InitNetworkOk = true;
					return true;
				}
			});
			var done = await Task.WhenAny(work, Task.Delay(TimeSpan.FromSeconds(25)));
			if (done != work)
			{
				InitNetworkOk = false;
				InitError = "Auth servers timed out.";
				return false;
			}
			return await work;
		}
		catch (Exception ex)
		{
			InitNetworkOk = false;
			InitError = ex.Message;
			return false;
		}
	}

	// ── tier mapping: Lifetime > Monthly > Free Trial ──
	private static (Tier tier, string label, string timeLeft, long expiry) MapTier()
	{
		var subs = Api.user_data.subscriptions;
		if (subs == null || subs.Count == 0)
			return (Tier.None, "No license", "", 0);

		Tier best = Tier.None;
		string label = "", timeLeft = "";
		long expiry = 0;
		foreach (var s in subs)
		{
			string n = (s.subscription ?? "").ToLowerInvariant();
			Tier t = Tier.Trial;
			string l = s.subscription ?? "Trial";
			if (n.Contains("lifetime") || n.Contains("life"))
			{
				t = Tier.Lifetime;
				l = "Lifetime";
			}
			else if (n.Contains("month") || n.Contains("30") || n.Contains("weekly") || n.Contains("week"))
			{
				t = Tier.Monthly;
				l = n.Contains("week") ? "Weekly" : "Monthly";
			}
			else if (n.Contains("trial") || n.Contains("free") || n.Contains("default"))
			{
				t = Tier.Trial;
				l = "Free Trial";
			}
			if ((int)t > (int)best)
			{
				best = t;
				label = l;
				timeLeft = s.timeleft ?? "";
				if (!long.TryParse(s.expiry, out expiry))
					expiry = 0;
			}
		}
		if (best == Tier.None)
			return (Tier.None, "No license", "", 0);
		return (best, label, timeLeft, expiry);
	}

	private static AuthSession Snapshot(bool grace)
	{
		var (tier, label, timeLeft, expiry) = MapTier();
		return new AuthSession(
			tier != Tier.None,
			Api.user_data.username ?? "",
			tier, label, timeLeft, expiry,
			Api.user_data.hwid ?? "",
			grace);
	}

	// ── login ──
	public static async Task<(bool ok, string error, AuthSession session)> LoginAsync(
		string username, string password)
	{
		return await Task.Run(() =>
		{
			lock (Gate)
			{
				try
				{
					Api.login(username, password);
				}
				catch (Exception ex)
				{
					return (false, ex.Message, _current);
				}
				if (!Api.response.success)
				{
					string msg = Api.response.message ?? "Login failed.";
					if (IsHwidError(msg))
						return (false, "HWID_MISMATCH::" + msg, _current);
					if (IsBanned(msg))
						return (false, "BANNED::" + msg, _current);
					return (false, msg, _current);
				}
				_current = Snapshot(grace: false);
				if (!_current.Valid)
					return (false, "No active subscription on this account.", _current);
				SaveSession(username, password, _current);
				return (true, "", _current);
			}
		});
	}

	// ── register (empty key = dashboard default Free Trial) ──
	public static async Task<(bool ok, string error, AuthSession session)> RegisterAsync(
		string username, string password)
	{
		return await Task.Run(() =>
		{
			lock (Gate)
			{
				try
				{
					Api.register(username, password, "");
				}
				catch (Exception ex)
				{
					return (false, ex.Message, _current);
				}
				if (!Api.response.success)
				{
					return (false, Api.response.message ?? "Registration failed.", _current);
				}
				_current = Snapshot(grace: false);
				if (!_current.Valid)
					return (false, "Registered, but no trial subscription was granted. Contact support.", _current);
				SaveSession(username, password, _current);
				return (true, "", _current);
			}
		});
	}

	// ── key activation (upgrade) ──
	public static async Task<(bool ok, string error, AuthSession session)> ActivateAsync(string key)
	{
		return await Task.Run(() =>
		{
			lock (Gate)
			{
				try
				{
					Api.license(key, null);
				}
				catch (Exception ex)
				{
					return (false, ex.Message, _current);
				}
				if (!Api.response.success)
				{
					return (false, MapKeyError(Api.response.message), _current);
				}
				_current = Snapshot(grace: false);
				if (!_current.Valid)
					return (false, "Key accepted, but no active subscription found.", _current);
				// refresh stored password-based session stays; update snapshot only
				SaveSnapshot(_current);
				return (true, "", _current);
			}
		});
	}

	public static async Task LogoutAsync()
	{
		await Task.Run(() =>
		{
			lock (Gate)
			{
				try { if (!DiscordActive) Api.logout(); } catch { }
				_current = new AuthSession(false, "", Tier.None, "", "", 0, "", false);
				DiscordActive = false;
				WipeSession();
				try { DiscordAuthService.WipeSessionPublic(); } catch { }
			}
		});
	}

	// Re-validate with the server (source of truth). Optionally silent.
	public static async Task<AuthSession> RevalidateAsync()
	{
		if (DiscordActive)
		{
			var (ok, s, _) = await DiscordAuthService.AutoLoginAsync();
			if (ok && s.Valid) { _current = s; return s; }
			// network blip — keep current; fatal failures clear via AutoLogin note
			return _current;
		}
		return await Task.Run(() =>
		{
			lock (Gate)
			{
				try
				{
					Api.check();
					if (!Api.response.success)
					{
						_current = new AuthSession(false, "", Tier.None, "", "", 0, "", false);
						return _current;
					}
				}
				catch
				{
					return _current;
				}
				_current = Snapshot(grace: _current.Grace);
				SaveSnapshot(_current);
				return _current;
			}
		});
	}

	private static bool IsHwidError(string msg)
	{
		string m = msg.ToLowerInvariant();
		return m.Contains("hwid") && (m.Contains("mismatch") || m.Contains("mismatched") ||
			m.Contains("locked") || m.Contains("another") || m.Contains("different") ||
			m.Contains("don't match") || m.Contains("does not match"));
	}

	private static bool IsBanned(string msg)
	{
		string m = msg.ToLowerInvariant();
		return m.Contains("banned") || m.Contains("blacklisted") || m.Contains("blocked");
	}

	private static string MapKeyError(string? msg)
	{
		string m = (msg ?? "").ToLowerInvariant();
		if (m.Contains("banned")) return "BANNED::" + msg;
		if (m.Contains("used") || m.Contains("redeemed")) return "This key was already used.";
		if (m.Contains("invalid") || m.Contains("not found") || m.Contains("exist"))
			return "Invalid key. Check it and try again.";
		return msg ?? "Key activation failed.";
	}

	// ── auth.json section (DPAPI + HWID entropy; never in the EXE) ──
	private record StoredSession(
		string Username, string Password, string Tier, string TierLabel,
		string TimeLeft, long ExpiryUnix, long SavedUtc, string Hwid);

	private static void SaveSession(string username, string password, AuthSession s)
	{
		try
		{
			var stored = new StoredSession(username, password, s.Tier.ToString(),
				s.TierLabel, s.TimeLeft, s.ExpiryUnix,
				DateTimeOffset.UtcNow.ToUnixTimeSeconds(), s.Hwid);
			AuthStore.SaveSection(AuthStore.KeyAuthKey, JsonSerializer.Serialize(stored));
		}
		catch { }
	}

	private static void SaveSnapshot(AuthSession s)
	{
		try
		{
			string? raw = AuthStore.LoadSection(AuthStore.KeyAuthKey);
			if (raw is null) return;
			var stored = JsonSerializer.Deserialize<StoredSession>(raw);
			if (stored == null) return;
			var updated = stored with
			{
				Tier = s.Tier.ToString(),
				TierLabel = s.TierLabel,
				TimeLeft = s.TimeLeft,
				ExpiryUnix = s.ExpiryUnix,
				SavedUtc = DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
				Hwid = s.Hwid
			};
			AuthStore.SaveSection(AuthStore.KeyAuthKey, JsonSerializer.Serialize(updated));
		}
		catch { }
	}

	private static StoredSession? LoadSession()
	{
		try
		{
			string? raw = AuthStore.LoadSection(AuthStore.KeyAuthKey);
			if (raw is null) return null;
			return JsonSerializer.Deserialize<StoredSession>(raw);
		}
		catch { return null; }
	}

	private static void WipeSession()
	{
		try { AuthStore.DeleteSection(AuthStore.KeyAuthKey); }
		catch { }
	}

	// Silent auto-login on launch: server first, 24h offline grace fallback.
	public static async Task<(bool ok, AuthSession session, string note)> AutoLoginAsync()
	{
		var stored = LoadSession();
		if (stored == null || string.IsNullOrEmpty(stored.Username))
			return (false, _current, "no saved session");

		var (ok, error, session) = await LoginAsync(stored.Username, stored.Password);
		if (ok) return (true, session, "");

		// Offline grace: network failure + fresh snapshot (<24h) → limited unlock.
		bool networkish = IsNetworkError(error);
		long ageH = (DateTimeOffset.UtcNow.ToUnixTimeSeconds() - stored.SavedUtc) / 3600;
		if (networkish && ageH < 24 && Enum.TryParse<Tier>(stored.Tier, out var t) && t != Tier.None)
		{
			var grace = new AuthSession(true, stored.Username, t,
				stored.TierLabel + " (offline)", stored.TimeLeft,
				stored.ExpiryUnix, stored.Hwid, true);
			_current = grace;
			return (true, grace, "offline grace");
		}
		if (!networkish)
			WipeSession(); // bad creds / banned / expired — don't retry forever
		return (false, _current, error);
	}

	private static bool IsNetworkError(string msg)
	{
		string m = (msg ?? "").ToLowerInvariant();
		return m.Contains("network") || m.Contains("connection") ||
			m.Contains("timed out") || m.Contains("timeout") ||
			m.Contains("unreachable") || m.Contains("socket") ||
			m.Contains("dns") || m.Contains("host") ||
			m.Contains("could not reach") || m.Contains("failed to connect");
	}
}
