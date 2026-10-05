using Microsoft.Win32;

namespace PrimexGlass.Services;

// Mouse / aim registry tweaks with backup + restore.
// Apply  = Windows smoothing curves, Enhance Pointer Precision ON,
//          speed 6/11, input queue 100 â€” for a smoother aim feel.
// Curves + speed load at logon â€” user must log off/on (or reboot).
public static class AimRegistry
{
	private const string BackupKey = @"SOFTWARE\PRIMEx Optimizer\MouseBackup";
	private const string MouseKey = @"Control Panel\Mouse";
	private const string MouClassKey = @"SYSTEM\CurrentControlSet\Services\mouclass\Parameters";

	private static readonly byte[] DefaultX = new byte[]
	{
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x15,0x6E,0x0E,0x00,0x00,0x00,0x00,0x00,
		0x00,0x40,0x01,0x00,0x00,0x00,0x00,0x00,
		0xE9,0x03,0x16,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x28,0x00,0x00,0x00,0x00,0x00
	};

	private static readonly byte[] DefaultY = new byte[]
	{
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0xB8,0x0B,0x38,0x00,0x00,0x00,0x00,0x00,
		0xCD,0x04,0x10,0x00,0x00,0x00,0x00,0x00,
		0xE9,0x03,0x16,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00
	};

	// Flat 1:1 curves (acceleration fully off).
	private static readonly byte[] FlatX = new byte[]
	{
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0xC0,0xCC,0x0C,0x00,0x00,0x00,0x00,0x00,
		0x80,0x99,0x19,0x00,0x00,0x00,0x00,0x00,
		0x40,0x66,0x26,0x00,0x00,0x00,0x00,0x00,
		0x00,0x33,0x33,0x00,0x00,0x00,0x00,0x00
	};

	private static readonly byte[] FlatY = new byte[]
	{
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x70,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0xA8,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0xE0,0x00,0x00,0x00,0x00,0x00
	};

	private static readonly string[] MouseStringValues = new string[]
	{
		"MouseSpeed", "MouseThreshold1", "MouseThreshold2",
		"MouseSensitivity", "DoubleClickSpeed", "MouseHoverTime",
		"ActiveWindowTracking", "Beep", "DoubleClickHeight", "DoubleClickWidth",
		"ExtendedSounds", "MouseHoverHeight", "MouseHoverWidth",
		"MouseTrails", "SnapToDefaultButton", "SwapMouseButtons"
	};

	// Personal AIM REG (user's own tuned values).
	private static readonly (string Name, string Value)[] AimRegStrings = new (string, string)[]
	{
		("Beep", "No"),
		("DoubleClickHeight", "4"),
		("DoubleClickSpeed", "500"),
		("DoubleClickWidth", "4"),
		("ExtendedSounds", "No"),
		("MouseHoverHeight", "4"),
		("MouseHoverTime", "9"),
		("MouseHoverWidth", "4"),
		("MouseSensitivity", "6"),
		("MouseSpeed", "1"),
		("MouseThreshold1", "6"),
		("MouseThreshold2", "10"),
		("MouseTrails", "0"),
		("SnapToDefaultButton", "0"),
		("SwapMouseButtons", "0"),
	};

	private static readonly byte[] AimRegX = new byte[]
	{
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0xA0,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x40,0x01,0x00,0x00,0x00,0x00,0x00,
		0x80,0x02,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00
	};

	private static readonly byte[] AimRegY = new byte[]
	{
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x66,0xA6,0x02,0x00,0x00,0x00,0x00,0x00,
		0xCD,0x4C,0x05,0x00,0x00,0x00,0x00,0x00,
		0xA0,0x99,0x0A,0x00,0x00,0x00,0x00,0x00,
		0x38,0x33,0x15,0x00,0x00,0x00,0x00,0x00
	};

	public static void ApplyAimReg()
	{
		BackupCurrent();
		using var key = Registry.CurrentUser.CreateSubKey(MouseKey);
		foreach (var (Name, Value) in AimRegStrings)
			key.SetValue(Name, Value, RegistryValueKind.String);
		key.SetValue("ActiveWindowTracking", 0, RegistryValueKind.DWord);
		key.SetValue("SmoothMouseXCurve", AimRegX, RegistryValueKind.Binary);
		key.SetValue("SmoothMouseYCurve", AimRegY, RegistryValueKind.Binary);
	}

	public static bool HasBackup()
	{
		try
		{
			using RegistryKey? key = Registry.CurrentUser.OpenSubKey(BackupKey, false);
			return key != null && key.GetValue("MouseSpeed") != null;
		}
		catch { return false; }
	}

	public static void BackupCurrent()
	{
		using RegistryKey? src = Registry.CurrentUser.OpenSubKey(MouseKey, false);
		using RegistryKey key = Registry.CurrentUser.CreateSubKey(BackupKey);
		if (src != null)
		{
			foreach (string name in MouseStringValues)
			{
				object? v = src.GetValue(name);
				if (v == null) continue;
				switch (v)
				{
					case int ii:
						key.SetValue(name, ii, RegistryValueKind.DWord);
						break;
					case string ss:
						key.SetValue(name, ss, RegistryValueKind.String);
						break;
					default:
						key.SetValue(name, v.ToString() ?? "", RegistryValueKind.String);
						break;
				}
			}
			object? cx = src.GetValue("SmoothMouseXCurve");
			if (cx is byte[] bx) key.SetValue("SmoothMouseXCurve", bx, RegistryValueKind.Binary);
			object? cy = src.GetValue("SmoothMouseYCurve");
			if (cy is byte[] by) key.SetValue("SmoothMouseYCurve", by, RegistryValueKind.Binary);
		}
		try
		{
			using RegistryKey? mq = Registry.LocalMachine.OpenSubKey(MouClassKey, false);
			object? q = mq?.GetValue("MouseDataQueueSize");
			if (q is int qi)
				key.SetValue("MouseDataQueueSize", qi, RegistryValueKind.DWord);
		}
		catch { }
	}

	public static void Apply()
	{
		BackupCurrent();

		using (RegistryKey key = Registry.CurrentUser.CreateSubKey(MouseKey))
		{
			key.SetValue("MouseSpeed", "1", RegistryValueKind.String);
			key.SetValue("MouseThreshold1", "6", RegistryValueKind.String);
			key.SetValue("MouseThreshold2", "10", RegistryValueKind.String);
			key.SetValue("MouseSensitivity", "10", RegistryValueKind.String);
			key.SetValue("SmoothMouseXCurve", DefaultX, RegistryValueKind.Binary);
			key.SetValue("SmoothMouseYCurve", DefaultY, RegistryValueKind.Binary);
		}

		using (RegistryKey key = Registry.LocalMachine.CreateSubKey(MouClassKey))
		{
			key.SetValue("MouseDataQueueSize", 100, RegistryValueKind.DWord);
		}
	}

	private static bool BytesEqual(byte[] a, byte[] b)
	{
		if (a.Length != b.Length) return false;
		for (int i = 0; i < a.Length; i++)
			if (a[i] != b[i]) return false;
		return true;
	}

	// accelOn = smoothing curves active (anything but flat).
	// precisionOn = Enhance Pointer Precision active (MouseSpeed != 0).
	public static (bool accelOn, bool precisionOn) GetState()
	{
		bool accel = true, prec = true;
		try
		{
			using var key = Registry.CurrentUser.OpenSubKey(MouseKey, false);
			if (key != null)
			{
				if (key.GetValue("SmoothMouseXCurve") is byte[] bx)
					accel = !BytesEqual(bx, FlatX);
				object? sp = key.GetValue("MouseSpeed");
				prec = sp == null || sp.ToString() != "0";
			}
		}
		catch { }
		return (accel, prec);
	}

	public static void SetAccel(bool on)
	{
		BackupCurrent();
		using var key = Registry.CurrentUser.CreateSubKey(MouseKey);
		byte[] x = on ? DefaultX : FlatX;
		byte[] y = on ? DefaultY : FlatY;
		key.SetValue("SmoothMouseXCurve", x, RegistryValueKind.Binary);
		key.SetValue("SmoothMouseYCurve", y, RegistryValueKind.Binary);
	}

	public static void SetPrecision(bool on)
	{
		BackupCurrent();
		using var key = Registry.CurrentUser.CreateSubKey(MouseKey);
		key.SetValue("MouseSpeed", on ? "1" : "0", RegistryValueKind.String);
		key.SetValue("MouseThreshold1", on ? "6" : "0", RegistryValueKind.String);
		key.SetValue("MouseThreshold2", on ? "10" : "0", RegistryValueKind.String);
	}

	public static bool Restore()
	{
		if (!HasBackup())
			return false;

		using RegistryKey? bak = Registry.CurrentUser.OpenSubKey(BackupKey, false);
		using RegistryKey mouse = Registry.CurrentUser.CreateSubKey(MouseKey);
		if (bak != null)
		{
			foreach (string name in MouseStringValues)
			{
				object? v = bak.GetValue(name);
				if (v == null) continue;
				RegistryValueKind kind = RegistryValueKind.String;
				try { kind = bak.GetValueKind(name); } catch { }
				switch (kind)
				{
					case RegistryValueKind.DWord:
						mouse.SetValue(name, Convert.ToInt32(v), RegistryValueKind.DWord);
						break;
					case RegistryValueKind.QWord:
						mouse.SetValue(name, Convert.ToInt64(v), RegistryValueKind.QWord);
						break;
					default:
						mouse.SetValue(name, v.ToString() ?? "", RegistryValueKind.String);
						break;
				}
			}
			object? cx = bak.GetValue("SmoothMouseXCurve");
			if (cx is byte[] bx) mouse.SetValue("SmoothMouseXCurve", bx, RegistryValueKind.Binary);
			object? cy = bak.GetValue("SmoothMouseYCurve");
			if (cy is byte[] by) mouse.SetValue("SmoothMouseYCurve", by, RegistryValueKind.Binary);
			object? q = bak.GetValue("MouseDataQueueSize");
			if (q is int qi)
			{
				try
				{
					using RegistryKey mq = Registry.LocalMachine.CreateSubKey(MouClassKey);
					mq.SetValue("MouseDataQueueSize", qi, RegistryValueKind.DWord);
				}
				catch { }
			}
		}
		return true;
	}
}
