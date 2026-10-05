using System.Diagnostics;
using System.Runtime.InteropServices;

namespace PrimexGlass.Services;

public static class RamBooster
{
	[DllImport("psapi.dll")]
	private static extern int EmptyWorkingSet(nint h);

	public static void FlushCore()
	{
		GC.Collect(GC.MaxGeneration, GCCollectionMode.Aggressive, blocking: true, compacting: true);
		GC.WaitForPendingFinalizers();
		GC.Collect(GC.MaxGeneration, GCCollectionMode.Aggressive, blocking: true, compacting: true);
		foreach (Process p in Process.GetProcesses())
		{
			try { EmptyWorkingSet(p.Handle); } catch { }
		}
	}
}
