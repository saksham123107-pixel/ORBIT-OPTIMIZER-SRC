using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace PrimexGlass.Services;

// Single on-disk token store: %AppData%\PRIMEx Optimizer\auth.json
// Each section is DPAPI(CurrentUser) with the machine HWID as entropy —
// never plaintext, never baked into the EXE, only this machine+user can decrypt.
public static class AuthStore
{
	public const string KeyAuthKey = "keyauth";
	public const string DiscordKey = "discord";

	private static readonly object Gate = new();

	public static string Path()
	{
		string dir = System.IO.Path.Combine(
			Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
			"PRIMEx Optimizer");
		Directory.CreateDirectory(dir);
		return System.IO.Path.Combine(dir, "auth.json");
	}

	private static byte[] HwidEntropy()
	{
		string hwid = KeyAuthService.LocalHwid();
		if (string.IsNullOrEmpty(hwid)) hwid = "PRIMEx";
		return Encoding.UTF8.GetBytes("PRIMEx::" + hwid);
	}

	private static Dictionary<string, JsonElement> ReadRoot()
	{
		try
		{
			string path = Path();
			if (!File.Exists(path)) return new Dictionary<string, JsonElement>();
			using var doc = JsonDocument.Parse(File.ReadAllText(path));
			var map = new Dictionary<string, JsonElement>();
			foreach (var p in doc.RootElement.EnumerateObject())
				map[p.Name] = p.Value.Clone();
			return map;
		}
		catch { return new Dictionary<string, JsonElement>(); }
	}

	private static void WriteRoot(Dictionary<string, JsonElement> root)
	{
		string path = Path();
		using var stream = new MemoryStream();
		using (var w = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = false }))
		{
			w.WriteStartObject();
			w.WriteNumber("v", 1);
			w.WriteString("hwid", KeyAuthService.LocalHwid());
			w.WriteNumber("savedUtc", DateTimeOffset.UtcNow.ToUnixTimeSeconds());
			foreach (var kv in root)
			{
				w.WritePropertyName(kv.Key);
				kv.Value.WriteTo(w);
			}
			w.WriteEndObject();
		}
		File.WriteAllText(path, Encoding.UTF8.GetString(stream.ToArray()));
	}

	public static void SaveSection(string section, string plainJson)
	{
		if (string.IsNullOrEmpty(section) || plainJson is null) return;
		lock (Gate)
		{
			try
			{
				byte[] enc = ProtectedData.Protect(
					Encoding.UTF8.GetBytes(plainJson),
					HwidEntropy(),
					DataProtectionScope.CurrentUser);
				var root = ReadRoot();
				root[section] = JsonSerializer.SerializeToElement(
					new { enc = Convert.ToBase64String(enc) });
				WriteRoot(root);
			}
			catch { }
		}
	}

	public static string? LoadSection(string section)
	{
		if (string.IsNullOrEmpty(section)) return null;
		lock (Gate)
		{
			try
			{
				var root = ReadRoot();
				if (!root.TryGetValue(section, out var node)) return null;
				if (node.ValueKind != JsonValueKind.Object) return null;
				if (!node.TryGetProperty("enc", out var encEl)) return null;
				string b64 = encEl.GetString() ?? "";
				if (string.IsNullOrEmpty(b64)) return null;
				byte[] enc = Convert.FromBase64String(b64);
				byte[] plain = ProtectedData.Unprotect(
					enc, HwidEntropy(), DataProtectionScope.CurrentUser);
				return Encoding.UTF8.GetString(plain);
			}
			catch { return null; }
		}
	}

	public static void DeleteSection(string section)
	{
		if (string.IsNullOrEmpty(section)) return;
		lock (Gate)
		{
			try
			{
				var root = ReadRoot();
				if (root.Remove(section)) WriteRoot(root);
			}
			catch { }
		}
	}

	public static void WipeAll()
	{
		lock (Gate)
		{
			try
			{
				string path = Path();
				if (File.Exists(path))
				{
					byte[] junk = new byte[512];
					Random.Shared.NextBytes(junk);
					File.WriteAllBytes(path, junk);
					File.Delete(path);
				}
			}
			catch { }
		}
	}

	// One-time move of legacy single-file blobs into auth.json (if still present).
	public static void MigrateLegacy()
	{
		lock (Gate)
		{
			try
			{
				string app = System.IO.Path.Combine(
					Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
					"PRIMEx Optimizer");
				string local = System.IO.Path.Combine(
					Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
					"PRIMEx Optimizer");

				// KeyAuth: %LocalAppData%\PRIMEx Optimizer\session.dat
				string kaOld = System.IO.Path.Combine(local, "session.dat");
				if (File.Exists(kaOld) && LoadSection(KeyAuthKey) is null)
				{
					try
					{
						byte[] enc = File.ReadAllBytes(kaOld);
						byte[] plain = ProtectedData.Unprotect(enc, null, DataProtectionScope.CurrentUser);
						SaveSection(KeyAuthKey, Encoding.UTF8.GetString(plain));
					}
					catch { }
					try { File.Delete(kaOld); } catch { }
				}

				// Discord: legacy single-file (Roaming or LocalAppData)
				string[] dcCandidates = {
					System.IO.Path.Combine(app, "discord-session.dat"),
					System.IO.Path.Combine(local, "discord-session.dat"),
				};
				if (LoadSection(DiscordKey) is null)
				{
					foreach (string dcOld in dcCandidates)
					{
						if (!File.Exists(dcOld)) continue;
						try
						{
							byte[] enc = File.ReadAllBytes(dcOld);
							byte[] plain = ProtectedData.Unprotect(enc, null, DataProtectionScope.CurrentUser);
							SaveSection(DiscordKey, Encoding.UTF8.GetString(plain));
						}
						catch { }
						try { File.Delete(dcOld); } catch { }
						if (LoadSection(DiscordKey) is not null) break;
					}
				}
			}
			catch { }
		}
	}
}
