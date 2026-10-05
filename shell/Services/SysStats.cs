using LibreHardwareMonitor.Hardware;

namespace PrimexGlass.Services;

// Live CPU/GPU/RAM + temperatures via LibreHardwareMonitor.
// Everything guarded — returns nulls when sensors are unavailable.
public static class SysStats
{
	private static Computer? _computer;
	private static readonly object _lock = new();

	public record Stats(
		double? CpuLoad, double? CpuTemp,
		double? GpuLoad, double? GpuTemp, string? GpuName,
		double? RamUsedPct, double? RamUsedGb, double? RamTotalGb);

	public static Stats Get()
	{
		try
		{
			lock (_lock)
			{
				_computer ??= new Computer
				{
					IsCpuEnabled = true,
					IsGpuEnabled = true,
					IsMemoryEnabled = true
				};
				try { _computer.Open(); } catch { }
			}

			double? cpuLoad = null, cpuTemp = null;
			double? gpuLoad = null, gpuTemp = null;
			string? gpuName = null;
			double? ramPct = null, ramUsed = null, ramTotal = null;

			foreach (IHardware hw in _computer.Hardware)
			{
				try { hw.Update(); } catch { continue; }
				try
				{
					if (hw.HardwareType == HardwareType.Cpu)
					{
						foreach (ISensor s in hw.Sensors)
						{
							if (s.SensorType == SensorType.Load && s.Name.Contains("Total"))
								cpuLoad ??= (double?)s.Value;
							if (s.SensorType == SensorType.Temperature &&
								(s.Name.Contains("Package") || s.Name.Contains("Core")))
								cpuTemp ??= (double?)s.Value;
						}
					}
					else if (hw.HardwareType is HardwareType.GpuNvidia
						or HardwareType.GpuAmd or HardwareType.GpuIntel)
					{
						gpuName ??= hw.Name;
						foreach (ISensor s in hw.Sensors)
						{
							if (s.SensorType == SensorType.Load && s.Name.Contains("Core"))
								gpuLoad ??= (double?)s.Value;
							if (s.SensorType == SensorType.Temperature && s.Name.Contains("Core"))
								gpuTemp ??= (double?)s.Value;
						}
					}
					else if (hw.HardwareType == HardwareType.Memory)
					{
						foreach (ISensor s in hw.Sensors)
						{
							if (s.SensorType == SensorType.Load)
								ramPct ??= (double?)s.Value;
							if (s.SensorType == SensorType.Data && s.Name.Contains("Used"))
								ramUsed ??= (double?)s.Value;
							if (s.SensorType == SensorType.Data && s.Name.Contains("Available") == false
								&& s.Name.Contains("Used") == false && s.Value > 0)
							{
								// total = used + available handled below if needed
							}
						}
					}
					foreach (IHardware sub in hw.SubHardware)
					{
						try { sub.Update(); } catch { }
					}
				}
				catch { }
			}

			if (ramPct == null && ramUsed != null)
			{
				try
				{
					ulong totalKb = TweaksService.TotalKb();
					double totalGb = totalKb / 1048576.0;
					ramTotal = totalGb;
					ramPct = totalGb > 0 ? ramUsed / totalGb * 100.0 : null;
				}
				catch { }
			}

			return new Stats(cpuLoad, cpuTemp, gpuLoad, gpuTemp, gpuName,
				ramPct, ramUsed, ramTotal);
		}
		catch
		{
			return new Stats(null, null, null, null, null, null, null, null);
		}
	}
}
