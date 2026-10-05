using System.Runtime.InteropServices;

namespace PrimexGlass.Services;

public static class Resolution
{
	public struct DEVMODE
	{
		[MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
		public string dmDeviceName;
		public short dmSpecVersion;
		public short dmDriverVersion;
		public short dmSize;
		public short dmDriverExtra;
		public int dmFields;
		public int dmPositionX;
		public int dmPositionY;
		public int dmDisplayOrientation;
		public int dmDisplayFixedOutput;
		public short dmColor;
		public short dmDuplex;
		public short dmYResolution;
		public short dmTTOption;
		public short dmCollate;
		[MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
		public string dmFormName;
		public short dmLogPixels;
		public int dmBitsPerPel;
		public int dmPelsWidth;
		public int dmPelsHeight;
		public int dmDisplayFlags;
		public int dmDisplayFrequency;
		public int dmICMMethod;
		public int dmICMIntent;
		public int dmMediaType;
		public int dmDitherType;
		public int dmReserved1;
		public int dmReserved2;
		public int dmPanningWidth;
		public int dmPanningHeight;
	}

	public const int ENUM_CURRENT_SETTINGS = -1;
	public const int DM_PELSWIDTH = 524288;
	public const int DM_PELSHEIGHT = 1048576;

	[DllImport("user32.dll")]
	public static extern int EnumDisplaySettings(string? d, int m, ref DEVMODE dm);

	[DllImport("user32.dll")]
	public static extern int ChangeDisplaySettings(ref DEVMODE dm, int f);

	public static void Change(int w, int h)
	{
		DEVMODE dm = default;
		dm.dmSize = (short)Marshal.SizeOf(dm);
		if (EnumDisplaySettings(null, -1, ref dm) != 0)
		{
			dm.dmPelsWidth = w;
			dm.dmPelsHeight = h;
			dm.dmFields = 1572864;
			ChangeDisplaySettings(ref dm, 0);
		}
	}

	public static (int w, int h) GetCurrent()
	{
		DEVMODE dm = default;
		dm.dmSize = (short)Marshal.SizeOf(dm);
		if (EnumDisplaySettings(null, -1, ref dm) != 0)
			return (dm.dmPelsWidth, dm.dmPelsHeight);
		return (1920, 1080);
	}
}
