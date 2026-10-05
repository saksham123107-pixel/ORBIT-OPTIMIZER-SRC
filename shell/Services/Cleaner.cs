using System.IO;

namespace PrimexGlass.Services;

// File cleaners. Returns deleted item count. Skips locked files.
public static class Cleaner
{
	public static int CleanPath(string path)
	{
		int n = 0;
		try
		{
			if (!Directory.Exists(path)) return 0;
			foreach (string f in Directory.GetFiles(path))
			{
				try { File.Delete(f); n++; } catch { }
			}
			foreach (string d in Directory.GetDirectories(path))
			{
				try { Directory.Delete(d, true); n++; } catch { }
			}
		}
		catch { }
		return n;
	}

	public static int ForceCleanCount(string path)
	{
		int n = 0;
		try
		{
			var root = new DirectoryInfo(path);
			foreach (FileInfo f in root.EnumerateFiles("*", SearchOption.AllDirectories))
			{
				try { f.Attributes = FileAttributes.Normal; f.Delete(); n++; } catch { }
			}
			DirectoryInfo[] dirs = root.GetDirectories("*", SearchOption.AllDirectories);
			Array.Reverse(dirs);
			foreach (DirectoryInfo d in dirs)
			{
				try { d.Delete(); n++; } catch { }
			}
		}
		catch { }
		return n;
	}
}
