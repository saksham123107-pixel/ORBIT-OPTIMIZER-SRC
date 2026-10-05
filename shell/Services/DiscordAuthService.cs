using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace PrimexGlass.Services;

// Discord OAuth2 (PKCE + localhost callback) — mirrors native auth.cpp.
// Token lives in %AppData%\PRIMEx Optimizer\auth.json (DPAPI + HWID entropy).
public static class DiscordAuthService
{
	private static readonly string ClientId = Environment.GetEnvironmentVariable("PRIMEX_DISCORD_CLIENT_ID") ?? "<CLIENT_ID>";
	private static readonly string GuildId = Environment.GetEnvironmentVariable("PRIMEX_DISCORD_GUILD_ID") ?? "<GUILD_ID>";
	private static readonly string TrialRole = Environment.GetEnvironmentVariable("PRIMEX_TRIAL_ROLE") ?? "<TRIAL_ROLE_ID>";
	private static readonly string PremiumRole = Environment.GetEnvironmentVariable("PRIMEX_PREMIUM_ROLE") ?? "<PREMIUM_ROLE_ID>";
	private const int OAuthPort = 48731;
	private const string RedirectUri = "http://127.0.0.1:48731/callback";
	private const string BotToken = ""; // optional; filled from env if present

	private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(15) };
	private static string _verifier = "";

	public static bool HasSessionFile() => AuthStore.LoadSection(AuthStore.DiscordKey) is not null;

	private static string EnvToken()
	{
		string t = Environment.GetEnvironmentVariable("PRIMEX_DISCORD_BOT_TOKEN") ?? "";
		return string.IsNullOrWhiteSpace(t) ? BotToken : t.Trim();
	}

	private static string UrlEncode(string s) =>
		Uri.EscapeDataString(s);

	private static string Base64Url(byte[] b) =>
		Convert.ToBase64String(b).TrimEnd('=').Replace('+', '-').Replace('/', '_');

	private static (string access, string refresh) SaveSession(JsonElement saved)
	{
		AuthStore.SaveSection(AuthStore.DiscordKey, saved.GetRawText());
		return ("", "");
	}

	private static JsonElement? LoadSession()
	{
		try
		{
			string? raw = AuthStore.LoadSection(AuthStore.DiscordKey);
			if (raw is null) return null;
			using var doc = JsonDocument.Parse(raw);
			return doc.RootElement.Clone();
		}
		catch { return null; }
	}

	public static void WipeSession()
	{
		try { AuthStore.DeleteSection(AuthStore.DiscordKey); } catch { }
	}

	// Called from KeyAuthService.LogoutAsync.
	public static void WipeSessionPublic() => WipeSession();

	private static async Task<string> HttpAsync(string method, string url, string? authHeader, StringContent? body)
	{
		using var req = new HttpRequestMessage(new HttpMethod(method), url);
		if (!string.IsNullOrEmpty(authHeader)) req.Headers.TryAddWithoutValidation("Authorization", authHeader);
		if (body != null) req.Content = body;
		using var resp = await Http.SendAsync(req);
		string text = await resp.Content.ReadAsStringAsync();
		if (!resp.IsSuccessStatusCode)
		{
			string msg = text;
			try
			{
				using var j = JsonDocument.Parse(text);
				if (j.RootElement.TryGetProperty("message", out var m)) msg = m.GetString() ?? msg;
				if (j.RootElement.TryGetProperty("error_description", out var d)) msg = d.GetString() ?? msg;
			} catch { }
			throw new Exception(string.IsNullOrWhiteSpace(msg)
				? $"Discord API error (HTTP {(int)resp.StatusCode})." : msg);
		}
		return text;
	}

	private static async Task<JsonDocument> JsonAsync(string method, string url, string? auth, object? body = null)
	{
		StringContent? content = null;
		if (body != null)
			content = new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, "application/json");
		string text = await HttpAsync(method, url, auth, content);
		return JsonDocument.Parse(text);
	}

	private static async Task<(bool ok, JsonElement member, string err)> FetchMemberAsync(string userToken, string userId)
	{
		JsonElement member = default;
		string err = "";
		string bot = EnvToken();
		if (!string.IsNullOrEmpty(bot))
		{
			try
			{
				using var doc = await JsonAsync("GET",
					$"https://discord.com/api/v10/guilds/{GuildId}/members/{userId}",
					$"Bot {bot}");
				return (true, doc.RootElement.Clone(), "");
			}
			catch (Exception ex) { err = ex.Message; }
		}
		if (!string.IsNullOrEmpty(userToken))
		{
			try
			{
				using var doc = await JsonAsync("GET",
					$"https://discord.com/api/v10/users/@me/guilds/{GuildId}/member",
					$"Bearer {userToken}");
				return (true, doc.RootElement.Clone(), "");
			}
			catch (Exception ex) { err = ex.Message; }
		}
		if (string.IsNullOrEmpty(bot) && string.IsNullOrEmpty(userToken))
			err = "Bot token is not configured.";
		return (false, member, err);
	}

	private static async Task<bool> JoinGuildAsync(string userToken, string userId)
	{
		string bot = EnvToken();
		if (string.IsNullOrEmpty(bot))
			throw new Exception("The server cannot add new members until the trusted auth host is configured.");
		using var _ = await JsonAsync("PUT",
			$"https://discord.com/api/v10/guilds/{GuildId}/members/{userId}",
			$"Bot {bot}",
			new { access_token = userToken });
		return true;
	}

	private static (bool premium, bool trial) Roles(JsonElement member)
	{
		bool premium = false, trial = false;
		if (member.TryGetProperty("roles", out var roles) && roles.ValueKind == JsonValueKind.Array)
		{
			foreach (var r in roles.EnumerateArray())
			{
				string id = r.ValueKind == JsonValueKind.String ? r.GetString()! :
					(r.TryGetProperty("id", out var i) ? i.GetString() ?? "" : "");
				if (id == PremiumRole) premium = true;
				if (id == TrialRole) trial = true;
			}
		}
		return (premium, trial);
	}

	private static KeyAuthService.AuthSession BuildSession(string username, string avatar, bool premium, long trialExpiry)
	{
		var tier = premium ? KeyAuthService.Tier.Lifetime : KeyAuthService.Tier.Trial;
		string label = premium ? "LIFETIME PREMIUM" : "FREEMIUM LIFETIME";
		string left = "Lifetime";
		return new KeyAuthService.AuthSession(
			true, username, tier, label, left, trialExpiry, "", false, avatar);
	}

	private static string FormatLeft(long seconds)
	{
		if (seconds < 0) seconds = 0;
		long d = seconds / 86400, h = (seconds % 86400) / 3600, m = (seconds % 3600) / 60;
		return d > 0 ? $"{d}d {h:00}h" : $"{h:00}h {m:00}m";
	}

	private static async Task<KeyAuthService.AuthSession> AuthorizeAsync(string code, JsonElement? oldSession)
	{
		string form =
			$"client_id={UrlEncode(ClientId)}&grant_type=authorization_code" +
			$"&code={UrlEncode(code)}&redirect_uri={UrlEncode(RedirectUri)}" +
			$"&code_verifier={UrlEncode(_verifier)}";
		string tokText = await HttpAsync("POST", "https://discord.com/api/oauth2/token",
			null, new StringContent(form, Encoding.UTF8, "application/x-www-form-urlencoded"));
		using var tok = JsonDocument.Parse(tokText);
		string access = tok.RootElement.GetProperty("access_token").GetString() ?? "";
		string refresh = tok.RootElement.TryGetProperty("refresh_token", out var rf) ? rf.GetString() ?? "" : "";
		if (string.IsNullOrEmpty(access) || string.IsNullOrEmpty(refresh))
			throw new Exception("Discord did not return a usable session.");

		using var userDoc = await JsonAsync("GET", "https://discord.com/api/users/@me", $"Bearer {access}");
		string uid = userDoc.RootElement.GetProperty("id").GetString() ?? "";
		string name = userDoc.RootElement.TryGetProperty("global_name", out var gn) && gn.GetString() is { Length: > 0 } g
			? g : userDoc.RootElement.GetProperty("username").GetString() ?? uid;
		string avatar = AvatarUrl(userDoc.RootElement, uid);
		if (string.IsNullOrEmpty(uid)) throw new Exception("Discord user identity is unavailable.");

		var (mok, member, merr) = await FetchMemberAsync(access, uid);
		if (!mok)
		{
			if (merr.Contains("10007") || merr.Contains("Unknown Member"))
			{
				await JoinGuildAsync(access, uid);
				var (rok, member2, merr2) = await FetchMemberAsync(access, uid);
				if (!rok) throw new Exception(merr2);
				member = member2;
			}
			else throw new Exception(merr);
		}

		var (premium, trial) = Roles(member);
		long now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
		if (!premium && !trial)
			throw new Exception("ROLE_REQUIRED::Your Discord account is connected, but you do not have a PRIMEx access role.");
		long expiry = 0;

		var saved = JsonSerializer.SerializeToElement(new
		{
			userId = uid,
			username = name,
			avatar,
			refreshToken = refresh,
			savedUtc = now,
			tier = premium ? "Lifetime" : "Freemium",
			tierLabel = premium ? "LIFETIME PREMIUM" : "FREEMIUM LIFETIME",
			trialExpiry = expiry
		});
		AuthStore.SaveSection(AuthStore.DiscordKey, saved.GetRawText());

		return BuildSession(name, avatar, premium, expiry);
	}

	private static string AvatarUrl(JsonElement user, string uid)
	{
		string hash = user.TryGetProperty("avatar", out var a) && a.ValueKind == JsonValueKind.String
			? a.GetString() ?? "" : "";
		if (string.IsNullOrEmpty(uid)) return "";
		if (!string.IsNullOrEmpty(hash))
			return $"https://cdn.discordapp.com/avatars/{uid}/{hash}.png?size=128";
		int idx = 0;
		try { idx = (int)((Convert.ToUInt64(uid) >> 22) % 6); } catch { }
		return $"https://cdn.discordapp.com/embed/avatars/{idx}.png";
	}

	public static async Task<KeyAuthService.AuthSession> LoginAsync()
	{
		_verifier = RandomString(64);
		string challenge = PkceChallenge(_verifier);
		string state = RandomString(32);
		string url =
			"https://discord.com/oauth2/authorize?client_id=" + UrlEncode(ClientId) +
			"&response_type=code&redirect_uri=" + UrlEncode(RedirectUri) +
			"&scope=identify%20guilds.join%20guilds.members.read" +
			"&state=" + UrlEncode(state) +
			"&code_challenge_method=S256&code_challenge=" + UrlEncode(challenge);

		Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
		string code = await WaitForCallbackAsync(state);
		JsonElement? old = LoadSession();
		return await AuthorizeAsync(code, old);
	}

	private static async Task<string> WaitForCallbackAsync(string expectedState)
	{
		var listener = new HttpListener();
		listener.Prefixes.Add($"http://127.0.0.1:{OAuthPort}/");
		try { listener.Start(); }
		catch { throw new Exception($"Local authorization port {OAuthPort} is unavailable. Close any app using this port and retry."); }

		try
		{
			var ctxTask = await Task.WhenAny(
				listener.GetContextAsync(),
				Task.Delay(TimeSpan.FromMinutes(3)));
			if (ctxTask is not Task<HttpListenerContext> ctxTask2 || !ctxTask2.IsCompletedSuccessfully)
				throw new Exception("Authorization timed out.");
			var ctx = await ctxTask2;
			var q = ParseQuery(ctx.Request.Url?.Query ?? "");
			string code = q.TryGetValue("code", out var c) ? c : "";
			string state = q.TryGetValue("state", out var st) ? st : "";
			string error = q.TryGetValue("error", out var er) ? er : "";
			byte[] html = Encoding.UTF8.GetBytes(
				"<html><body style='font-family:Segoe UI;background:#111;color:#fff;text-align:center;padding:80px'>" +
				"<h2>PRIMEx authorization complete</h2>" +
				"<p>You can close this browser tab and return to PRIMEx.</p></body></html>");
			ctx.Response.ContentType = "text/html; charset=utf-8";
			ctx.Response.OutputStream.Write(html);
			ctx.Response.Close();
			if (state != expectedState) throw new Exception("Invalid OAuth state.");
			if (!string.IsNullOrEmpty(error)) throw new Exception(error);
			if (string.IsNullOrEmpty(code)) throw new Exception("Discord authorization was cancelled.");
			return code;
		}
		finally { listener.Stop(); listener.Close(); }
	}

	private static Dictionary<string, string> ParseQuery(string query)
	{
		var d = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		if (string.IsNullOrEmpty(query)) return d;
		if (query.StartsWith("?")) query = query.Substring(1);
		foreach (var pair in query.Split('&', StringSplitOptions.RemoveEmptyEntries))
		{
			int eq = pair.IndexOf('=');
			if (eq <= 0) continue;
			string k = Uri.UnescapeDataString(pair.Substring(0, eq));
			string v = Uri.UnescapeDataString(pair.Substring(eq + 1).Replace('+', ' '));
			d[k] = v;
		}
		return d;
	}

	// Silent restore: refresh token, else offline grace from saved session.
	public static async Task<(bool ok, KeyAuthService.AuthSession session, string note)> AutoLoginAsync()
	{
		var saved = LoadSession();
		if (saved is null) return (false, KeyAuthService.Current, "no saved session");
		var s = saved.Value;
		if (!s.TryGetProperty("refreshToken", out var rt) || string.IsNullOrEmpty(rt.GetString()))
			return (false, KeyAuthService.Current, "no saved session");

		try
		{
			string form =
				$"client_id={UrlEncode(ClientId)}&grant_type=refresh_token&refresh_token={UrlEncode(rt.GetString()!)}";
			string tokText = await HttpAsync("POST", "https://discord.com/api/oauth2/token",
				null, new StringContent(form, Encoding.UTF8, "application/x-www-form-urlencoded"));
			using var tok = JsonDocument.Parse(tokText);
			string access = tok.RootElement.GetProperty("access_token").GetString() ?? "";
			string newRt = tok.RootElement.TryGetProperty("refresh_token", out var nrt) ? nrt.GetString() ?? rt.GetString()! : rt.GetString()!;
			string uid = s.TryGetProperty("userId", out var u) ? u.GetString() ?? "" : "";
			if (string.IsNullOrEmpty(access) || string.IsNullOrEmpty(uid))
				return Offline(s, "Discord session restored (offline)");

			var (mok, member, merr) = await FetchMemberAsync(access, uid);
			if (!mok)
			{
				if (merr.Contains("ROLE_REQUIRED") || merr.Contains("EXPIRED") ||
					merr.Contains("invalid_grant") || merr.Contains("Unknown Member") ||
					merr.Contains("Missing Access"))
					return (false, KeyAuthService.Current, merr);
				return Offline(s, "Discord session restored (offline)");
			}

			var (premium, trial) = Roles(member);
			long now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
			if (!premium && !trial)
				return (false, KeyAuthService.Current, "ROLE_REQUIRED::Your PRIMEx Discord access role is missing.");
			long expiry = 0;

			// persist rotated refresh token
			string username = s.TryGetProperty("username", out var un) ? un.GetString() ?? "" : "";
			string avatar = s.TryGetProperty("avatar", out var av) ? av.GetString() ?? "" : "";
			var updated = JsonSerializer.SerializeToElement(new
			{
				userId = uid,
				username,
				avatar,
				refreshToken = newRt,
				savedUtc = now,
				tier = premium ? "Lifetime" : "Freemium",
				tierLabel = premium ? "LIFETIME PREMIUM" : "FREEMIUM LIFETIME",
				trialExpiry = expiry
			});
			AuthStore.SaveSection(AuthStore.DiscordKey, updated.GetRawText());

			var session = BuildSession(username, avatar, premium, expiry);
			return (true, session, "Discord session restored");
		}
		catch (Exception ex)
		{
			string m = ex.Message;
			bool fatal = m.Contains("ROLE_REQUIRED") || m.Contains("EXPIRED") ||
				m.Contains("invalid_grant") || m.Contains("BANNED");
			if (fatal) return (false, KeyAuthService.Current, m);
			return Offline(s, "Discord session restored (offline)");
		}
	}

	private static (bool, KeyAuthService.AuthSession, string) Offline(JsonElement s, string note)
	{
		string tier = s.TryGetProperty("tier", out var t) ? t.GetString() ?? "" : "";
		string username = s.TryGetProperty("username", out var un) ? un.GetString() ?? "" : "";
		string avatar = s.TryGetProperty("avatar", out var av) ? av.GetString() ?? "" : "";
		if (string.IsNullOrEmpty(username))
			return (false, KeyAuthService.Current, "no saved session");
		bool life = tier == "Lifetime";
		var sess = new KeyAuthService.AuthSession(
			true, username,
			life ? KeyAuthService.Tier.Lifetime : KeyAuthService.Tier.Trial,
			life ? "LIFETIME PREMIUM" : "FREEMIUM LIFETIME",
			"Lifetime",
			0, "", true, avatar);
		return (true, sess, note);
	}

	private static string RandomString(int n)
	{
		const string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";
		var rng = RandomNumberGenerator.Create();
		var b = new byte[n];
		rng.GetBytes(b);
		var sb = new StringBuilder(n);
		foreach (var x in b) sb.Append(chars[x % chars.Length]);
		return sb.ToString();
	}

	private static string PkceChallenge(string verifier)
	{
		byte[] hash = SHA256.HashData(Encoding.ASCII.GetBytes(verifier));
		return Base64Url(hash);
	}
}
