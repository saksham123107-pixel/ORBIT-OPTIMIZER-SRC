using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;

namespace PrimexGlass.Services;

// PRIMEx tweak-pack backend: curated registry groups from the shipped pack.
// Every apply snapshots prior values first; revert restores them
// (and deletes values the group created).
public static class TweaksService
{
	private const string BackupRoot = @"SOFTWARE\PRIMEx Optimizer\TweaksBackup";

	private static JsonDocument? _doc;

	private static JsonDocument Doc()
	{
		if (_doc != null) return _doc;
		var asm = typeof(TweaksService).Assembly;
		string? name = asm.GetManifestResourceNames()
			.FirstOrDefault(n => n.EndsWith("tweaks.json", StringComparison.OrdinalIgnoreCase));
		if (name == null) throw new Exception("Embedded tweaks.json missing.");
		using var s = asm.GetManifestResourceStream(name)!;
		_doc = JsonDocument.Parse(s);
		return _doc;
	}

	public record GroupInfo(string Id, string Title, string Desc, bool Danger, int Count, string? Dynamic, string? Category, bool Premium);

	// Paid packs: full registry packs + network/CPU performance packs + mouse.
	public static bool IsPremium(string id)
	{
		string baseId = id;
		int sep = id.IndexOf("::", StringComparison.Ordinal);
		if (sep >= 0) baseId = id.Substring(0, sep);
		return baseId is "primex-full" or "mouse" or "primex-performance"
			or "network" or "cpu-amd" or "cpu-intel";
	}

	public static List<GroupInfo> ListGroups()
	{
		var list = new List<GroupInfo>();
		foreach (var g in Doc().RootElement.GetProperty("groups").EnumerateArray())
		{
			string id = g.GetProperty("id").GetString()!;
			string? dyn = g.TryGetProperty("dynamic", out var d) && d.ValueKind == JsonValueKind.String
				? d.GetString() : null;
			// Explicit category groups are one card each.
			if (g.TryGetProperty("category", out var catEl) && catEl.ValueKind == JsonValueKind.String)
			{
				int n = g.TryGetProperty("count", out var cnt) && cnt.ValueKind == JsonValueKind.Number
					? cnt.GetInt32()
					: g.GetProperty("entries").GetArrayLength();
				list.Add(new GroupInfo(
					id,
					g.GetProperty("title").GetString()!,
					g.GetProperty("desc").GetString() ?? "",
					g.GetProperty("danger").GetBoolean(),
					n,
					dyn,
					catEl.GetString(),
					IsPremium(id)));
				continue;
			}
			// Split multi-category groups into one card per category.
			var cats = new List<string>();
			foreach (var e in g.GetProperty("entries").EnumerateArray())
			{
				string cat = e.TryGetProperty("cat", out var c) && c.ValueKind == JsonValueKind.String
					? c.GetString()! : "MISC";
				if (!cats.Contains(cat)) cats.Add(cat);
			}
			string packTitle = g.GetProperty("title").GetString() ?? id;
			if (cats.Count > 1)
			{
				foreach (string cat in cats)
				{
					int n = g.GetProperty("entries").EnumerateArray()
						.Count(e => (e.TryGetProperty("cat", out var c) &&
							c.ValueKind == JsonValueKind.String ? c.GetString() : "MISC") == cat);
					list.Add(new GroupInfo($"{id}::{cat}", cat,
						$"{n} keys",
						g.GetProperty("danger").GetBoolean() && RiskyCat(cat), n, null,
						CleanCategory(packTitle, cat),
						IsPremium(id)));
				}
				continue;
			}
			list.Add(new GroupInfo(
				id,
				packTitle,
				g.GetProperty("desc").GetString() ?? "",
				g.GetProperty("danger").GetBoolean(),
				g.TryGetProperty("count", out var c2) && c2.ValueKind == JsonValueKind.Number
					? c2.GetInt32()
					: g.GetProperty("entries").GetArrayLength(),
				dyn,
				CleanCategory(packTitle, cats.Count > 0 ? cats[0] : ""),
				IsPremium(id)));
		}
		return list;
	}

	// Categories that genuinely change driver/system behavior.
		private static readonly HashSet<string> RiskyCats = new(StringComparer.Ordinal)
		{
			"MOUSE/KEYBOARD CLASS DRIVERS",
			"HD-PLAYER PRIORITY (IFEO)",
			"TELEMETRY / PRIVACY",
			"GAME MODE + GPU SCHEDULING + GAME DVR",
			"Power Tweaks",
			"Security",
			"Windows Update"
		};

		private static bool RiskyCat(string cat) => RiskyCats.Contains(cat);

		// Map pack title / entry cat → clean section category (never brand-prefixed).
		private static string CleanCategory(string packTitle, string entryCat)
		{
			switch (entryCat)
			{
				case "MOUSE": case "KEYBOARD": case "DESKTOP":
				case "EXPLORER": case "EXPLORER + TOUCH":
				case "VISUAL": case "NOTIFICATIONS": case "INPUT":
					return "Personalization";
				case "POWER": case "CONTROLLER / USB / BT POWER":
					return "Power Tweaks";
				case "AUDIO LATENCY + MMCSS + GAMES TASK":
				case "GAMING":
				case "GAME MODE + GPU SCHEDULING + GAME DVR":
				case "MOUSE/KEYBOARD CLASS DRIVERS":
					return "Gaming";
				case "DPI / COMPAT FLAGS ON EMULATORS":
				case "BLUESTACKS REGISTRY":
				case "HD-PLAYER PRIORITY (IFEO)":
					return "Emulators";
				case "TELEMETRY / PRIVACY":
					return "Privacy";
				case "SYSTEM": case "KERNEL / MEMORY":
				case "GPU / DISPLAY": case "CPU / SCHEDULING":
				case "STARTUP":
					return "Core Optimizations";
			}
			if (packTitle.StartsWith("CPU Priority", StringComparison.OrdinalIgnoreCase))
				return "CPU Priority";
			if (packTitle.IndexOf("Network", StringComparison.OrdinalIgnoreCase) >= 0)
				return "Network";
			if (packTitle.StartsWith("PRIMEx", StringComparison.OrdinalIgnoreCase))
				return "Core Optimizations";
			if (entryCat is "Input" or "Windows Update" or "Security" or "Bloat")
				return entryCat;
			return string.IsNullOrWhiteSpace(packTitle) ? "Core Optimizations" : packTitle;
		}

	// "groupId" or "groupId::Category" for split groups.
	private static (JsonElement Group, string? Cat) FindGroup(string id)
	{
		string baseId = id, cat = null!;
		int sep = id.IndexOf("::", StringComparison.Ordinal);
		if (sep >= 0)
		{
			baseId = id.Substring(0, sep);
			cat = id.Substring(sep + 2);
		}
		foreach (var g in Doc().RootElement.GetProperty("groups").EnumerateArray())
		{
			if (g.GetProperty("id").GetString() == baseId)
				return (g, cat);
		}
		throw new Exception($"Unknown optimizer group '{id}'.");
	}

	private record Entry(RegistryKey Root, string Key, string? Name, RegistryValueKind Kind, object Value);

	private static RegistryKey OpenRoot(string hive, bool writable)
	{
		return hive switch
		{
			"HKLM" => Registry.LocalMachine,
			"HKCU" => Registry.CurrentUser,
			_ => throw new Exception($"Unsupported hive '{hive}'.")
		};
	}

	private static object ParseValue(string type, JsonElement v)
	{
		return type switch
		{
			"REG_SZ" => v.GetString() ?? "",
			"REG_DWORD" => unchecked((int)v.GetUInt32()),
			"REG_BINARY" => FromHex(v.GetString() ?? ""),
			"HEX(7)" => DecodeMultiSz(FromHex(v.GetString() ?? "")),
			"REG_QWORD" => unchecked((long)ulong.Parse(v.GetString() ?? "0",
				System.Globalization.NumberStyles.HexNumber)),
			"REG_EXPAND_SZ" => v.GetString() ?? "",
			_ => throw new Exception($"Unsupported type '{type}'.")
		};
	}

	private static RegistryValueKind ToKind(string type) => type switch
	{
		"REG_SZ" => RegistryValueKind.String,
		"REG_DWORD" => RegistryValueKind.DWord,
		"REG_BINARY" => RegistryValueKind.Binary,
		"HEX(7)" => RegistryValueKind.MultiString,
		"REG_QWORD" => RegistryValueKind.QWord,
		"REG_EXPAND_SZ" => RegistryValueKind.ExpandString,
		_ => throw new Exception($"Unsupported type '{type}'.")
	};

	private static byte[] FromHex(string hex)
	{
		hex = new string(hex.Where(c => Uri.IsHexDigit(c)).ToArray());
		if (hex.Length % 2 == 1) hex = "0" + hex;
		var b = new byte[hex.Length / 2];
		for (int i = 0; i < b.Length; i++)
			b[i] = Convert.ToByte(hex.Substring(i * 2, 2), 16);
		return b;
	}

	private static string ToHex(byte[] b) =>
		BitConverter.ToString(b).Replace("-", "").ToLowerInvariant();

	private static string[] DecodeMultiSz(byte[] b)
	{
		string s = Encoding.Unicode.GetString(b);
		return s.Split('\0', StringSplitOptions.RemoveEmptyEntries);
	}

	// Resolve static + dynamic entries for a group (optionally one category).
	private static List<(string Hive, string Key, string? Name, string Type, JsonElement Value, string Cat)> Resolve(
		JsonElement group, string? onlyCat)
	{
		var list = new List<(string Hive, string Key, string? Name, string Type, JsonElement Value, string Cat)>();
		string? dyn = group.TryGetProperty("dynamic", out var d) && d.ValueKind == JsonValueKind.String
			? d.GetString() : null;

		if (dyn == "svchost")
		{
			ulong kb = TotalRamKb();
			list.Add(("HKLM", @"SYSTEM\CurrentControlSet\Control", "SvcHostSplitThresholdInKB",
				"REG_DWORD", JsonDocument.Parse(kb.ToString()).RootElement, "POWER"));
			return list;
		}

		if (dyn == "nagle")
		{
			var doc = Doc();
			var tmpl = doc.RootElement.GetProperty("nagle").EnumerateArray().ToList();
			foreach (string sub in NicSubkeys())
			{
				foreach (var t in tmpl)
				{
					string type = t.GetProperty("type").GetString()!;
					JsonElement val = t.GetProperty("value");
					list.Add(("HKLM", @"SYSTEM\CurrentControlSet\Services\Tcpip\Parameters\Interfaces\" + sub,
						t.GetProperty("name").GetString(), type, val, "NETWORK"));
				}
			}
			if (list.Count == 0) throw new Exception("No network interfaces found.");
			return list;
		}

		foreach (var e in group.GetProperty("entries").EnumerateArray())
		{
			if (e.GetProperty("op").GetString() != "set") continue;
			string cat = e.TryGetProperty("cat", out var cc) && cc.ValueKind == JsonValueKind.String
				? cc.GetString()! : "MISC";
			if (onlyCat != null && cat != onlyCat) continue;
			list.Add((e.GetProperty("hive").GetString()!,
				e.GetProperty("key").GetString()!,
				e.TryGetProperty("name", out var n) && n.ValueKind == JsonValueKind.String ? n.GetString() : null,
				e.GetProperty("type").GetString()!,
				e.GetProperty("value"),
				e.TryGetProperty("cat", out var c) && c.ValueKind == JsonValueKind.String ? c.GetString()! : "MISC"));
		}
		return list;
	}

	private static List<string> NicSubkeys()
	{
		var outList = new List<string>();
	 try
		{
			using var k = Registry.LocalMachine.OpenSubKey(
				@"SYSTEM\CurrentControlSet\Services\Tcpip\Parameters\Interfaces", false);
			if (k != null) outList.AddRange(k.GetSubKeyNames());
		}
		catch { }
		return outList;
	}

	[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
	private struct MEMORYSTATUSEX
	{
		public uint dwLength;
		public uint dwMemoryLoad;
		public ulong ullTotalPhys;
		public ulong ullAvailPhys;
		public ulong ullTotalPageFile;
		public ulong ullAvailPageFile;
		public ulong ullTotalVirtual;
		public ulong ullAvailVirtual;
		public ulong ullAvailExtendedVirtual;
	}

	[DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
	private static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX lpBuffer);

	private static ulong TotalRamKb()
	{
		try
		{
			var m = new MEMORYSTATUSEX { dwLength = (uint)Marshal.SizeOf<MEMORYSTATUSEX>() };
			if (GlobalMemoryStatusEx(ref m) && m.ullTotalPhys > 0)
				return m.ullTotalPhys / 1024;
		}
		catch { }
		return 8388608; // fallback 8 GB
	}

	public static ulong TotalKb() => TotalRamKb();

	private static string BackupKeyFor(string group) => $"{BackupRoot}\\{group}";

	public static bool HasBackup(string group)
	{
		try
		{
			using var k = Registry.CurrentUser.OpenSubKey(BackupKeyFor(group), false);
			return k != null && k.GetValue("0.present") != null;
		}
		catch { return false; }
	}

	public static List<object> ListEntries(string groupId)
	{
		var (group, cat) = FindGroup(groupId);
		var entries = Resolve(group, cat);
		var outList = new List<object>();
		foreach (var (hive, key, name, type, val, ccat) in entries)
		{
			string display = type switch
			{
				"REG_DWORD" => $"0x{val.GetUInt32():X8} ({val.GetUInt32()})",
				"REG_SZ" => val.GetString() ?? "",
				"REG_BINARY" => "hex:" + ((val.GetString() ?? "").Length > 48
					? (val.GetString() ?? "").Substring(0, 48) + "…" : val.GetString()),
				"HEX(7)" => "multi-sz: " + string.Join(" | ",
					DecodeMultiSz(FromHex(val.GetString() ?? "")).Take(3)),
				"REG_QWORD" => $"0x{ulong.Parse(val.GetString() ?? "0",
					System.Globalization.NumberStyles.HexNumber):X16}",
				_ => val.ToString()
			};
			outList.Add(new
			{
				hive, key, name, type, cat = ccat,
				value = JsonDocument.Parse(val.GetRawText()).RootElement.Clone(),
				display
			});
		}
		return outList;
	}

	public static string ApplyGroup(string groupId, Action<int, string> report)
	{
		var (group, cat) = FindGroup(groupId);
		var entries = Resolve(group, cat);
		string title = cat ?? group.GetProperty("title").GetString() ?? groupId;
		return ApplyResolved(groupId, title, entries, report);
	}

	public static string ApplyEntryList(
		string groupId, List<System.Text.Json.Nodes.JsonNode?> nodes,
		Action<int, string> report)
	{
		var docs = new List<JsonDocument>();
		try
		{
			var entries = new List<(string Hive, string Key, string? Name, string Type, JsonElement Value, string Cat)>();
			foreach (var n in nodes)
			{
				if (n == null) continue;
				var doc = JsonDocument.Parse(n.ToJsonString());
				docs.Add(doc);
				var r = doc.RootElement;
				entries.Add((
					r.GetProperty("hive").GetString()!,
					r.GetProperty("key").GetString()!,
					r.TryGetProperty("name", out var nn) && nn.ValueKind == JsonValueKind.String ? nn.GetString() : null,
					r.GetProperty("type").GetString()!,
					r.GetProperty("value").Clone(),
					r.TryGetProperty("cat", out var cc) && cc.ValueKind == JsonValueKind.String ? cc.GetString()! : "MISC"));
			}
			if (entries.Count == 0) throw new Exception("No entries selected.");
			return ApplyResolved(groupId, "Custom selection", entries, report);
		}
		finally
		{
			foreach (var d in docs) d.Dispose();
		}
	}

	private static string ApplyResolved(
		string groupId, string title,
		List<(string Hive, string Key, string? Name, string Type, JsonElement Value, string Cat)> entries,
		Action<int, string> report)
	{
		if (entries.Count == 0) throw new Exception("Group is empty.");

		// Wipe previous backup, snapshot current state.
		try { Registry.CurrentUser.DeleteSubKeyTree(BackupKeyFor(groupId), false); } catch { }
		using (RegistryKey bak = Registry.CurrentUser.CreateSubKey(BackupKeyFor(groupId)))
		{
			int i = 0;
			foreach (var (hive, key, name, type, _, _) in entries)
			{
				try
				{
					using var root = OpenRoot(hive, false);
					using var k = root.OpenSubKey(key, false);
					object? cur = (name == null) ? null : k?.GetValue(name);
					RegistryValueKind curKind = RegistryValueKind.Unknown;
					if (k != null && name != null)
					{
						try { curKind = k.GetValueKind(name); } catch { }
					}
					bak.SetValue($"{i}.present", cur == null ? 0 : 1, RegistryValueKind.DWord);
					bak.SetValue($"{i}.hive", hive, RegistryValueKind.String);
					bak.SetValue($"{i}.key", key, RegistryValueKind.String);
					bak.SetValue($"{i}.name", name ?? "@", RegistryValueKind.String);
					bak.SetValue($"{i}.kind", curKind.ToString(), RegistryValueKind.String);
					if (cur != null)
					{
						if (cur is byte[] bb) bak.SetValue($"{i}.hex", ToHex(bb), RegistryValueKind.String);
						else if (cur is string[] sa) bak.SetValue($"{i}.hex", ToHex(Encoding.Unicode.GetBytes(string.Join('\0', sa) + '\0')), RegistryValueKind.String);
						else if (cur is int ii) bak.SetValue($"{i}.dword", unchecked((uint)ii), RegistryValueKind.DWord);
						else if (cur is long ll) bak.SetValue($"{i}.qword", ll, RegistryValueKind.QWord);
						else bak.SetValue($"{i}.str", cur.ToString() ?? "", RegistryValueKind.String);
					}
				}
				catch { }
				i++;
			}
			bak.SetValue("count", entries.Count, RegistryValueKind.DWord);
		}

		// Apply.
		int done = 0, failed = 0;
		foreach (var (hive, key, name, type, val, _) in entries)
		{
			try
			{
				using var root = OpenRoot(hive, false);
				using var k = root.CreateSubKey(key, true);
				if (k == null) throw new Exception("cannot open key");
				object parsed = ParseValue(type, val);
				RegistryValueKind kind = ToKind(type);
				if (name == null) k.SetValue("", parsed, kind);
				else k.SetValue(name, parsed, kind);
				done++;
			}
			catch { failed++; }
			report(done * 100 / entries.Count, $"{done}/{entries.Count}");
		}
		return $"{title} applied: {done} ok, {failed} failed. Reboot recommended.";
	}

	public static string RevertGroup(string groupId, Action<int, string> report)
	{
		using var bak = Registry.CurrentUser.OpenSubKey(BackupKeyFor(groupId), false)
			?? throw new Exception("No backup for this group — nothing to revert.");
		int count = (int)(bak.GetValue("count") as int? ?? Convert.ToInt32(bak.GetValue("count") ?? 0));
		int done = 0;
		for (int i = 0; i < count; i++)
		{
			try
			{
				string hive = bak.GetValue($"{i}.hive")?.ToString() ?? "HKCU";
				string key = bak.GetValue($"{i}.key")?.ToString() ?? "";
				string? name = bak.GetValue($"{i}.name")?.ToString();
				if (name == "@") name = null;
				int present = Convert.ToInt32(bak.GetValue($"{i}.present") ?? 0);
				string kind = bak.GetValue($"{i}.kind")?.ToString() ?? "Unknown";

				using var root = OpenRoot(hive, false);
				if (present == 0)
				{
					using var k = root.OpenSubKey(key, true);
					if (k != null && name != null)
					{
						try { k.DeleteValue(name, false); } catch { }
					}
				}
				else
				{
					using var k = root.CreateSubKey(key, true);
					if (k == null) continue;
					if (name == null) continue;
					switch (kind)
					{
						case "String":
							k.SetValue(name, bak.GetValue($"{i}.str")?.ToString() ?? "", RegistryValueKind.String);
							break;
						case "ExpandString":
							k.SetValue(name, bak.GetValue($"{i}.str")?.ToString() ?? "", RegistryValueKind.ExpandString);
							break;
						case "DWord":
							k.SetValue(name, Convert.ToInt32(bak.GetValue($"{i}.dword") ?? 0), RegistryValueKind.DWord);
							break;
						case "QWord":
							k.SetValue(name, Convert.ToInt64(bak.GetValue($"{i}.qword") ?? (long)0), RegistryValueKind.QWord);
							break;
						case "Binary":
							k.SetValue(name, FromHex(bak.GetValue($"{i}.hex")?.ToString() ?? ""), RegistryValueKind.Binary);
							break;
						case "MultiString":
						{
							string hex = bak.GetValue($"{i}.hex")?.ToString() ?? "";
							k.SetValue(name, DecodeMultiSz(FromHex(hex)), RegistryValueKind.MultiString);
							break;
						}
					}
				}
				done++;
			}
			catch { }
			report(count == 0 ? 100 : done * 100 / count, $"{done}/{count}");
		}
		return "Reverted. Reboot recommended.";
	}
}
