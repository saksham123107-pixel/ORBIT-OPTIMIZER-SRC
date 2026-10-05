using System.Runtime.InteropServices;
using System.ServiceProcess;

namespace PrimexGlass.Services;

// Windows Service Control Manager — Services tab (list / start / stop / start-type).
// SafeDisable only sets start=disabled for a curated list (never force-stops).
public static class WinServices
{
	// Curated service packs (safe start=disabled only - no force-stop)..
	private static readonly Dictionary<string, string[]> ServicePacks = new(StringComparer.OrdinalIgnoreCase)
	{
		["telemetry"] = new[]
		{
			"DiagTrack", "dmwappushservice", "WerSvc", "PcaSvc", "DPS",
			"WdiServiceHost", "WdiSystemHost", "diagsvc", "diagnosticshub.standardcollector.service",
			"InventorySvc", "wercplsupport", "TroubleshootingSvc", "DcpSvc",
		},
		["updates"] = new[]
		{
			"wuauserv", "UsoSvc", "WaaSMedicSvc", "DoSvc", "BITS",
			"edgeupdate", "edgeupdatem", "MicrosoftEdgeElevationService",
		},
		["xbox"] = new[]
		{
			"XblAuthManager", "XblGameSave", "XboxNetApiSvc", "XboxGipSvc",
		},
		["store"] = new[]
		{
			"InstallService", "PushToInstall", "OneSyncSvc", "UserDataSvc",
			"UnistoreSvc", "PimIndexMaintenanceSvc", "MessagingService",
			"BcastDVRUserService", "WSearch", "MapsBroker", "wisvc",
		},
		["print-sensor"] = new[]
		{
			"Spooler", "PrintNotify", "StiSvc", "WiaRpc", "WbioSrvc",
			"SensorService", "SensorDataService", "SensrSvc", "FrameServer", "WPDBusEnum",
		},
		["remote"] = new[]
		{
			"RemoteRegistry", "TermService", "SessionEnv", "UmRdpService",
			"WinRM", "RemoteAccess", "RasMan", "RasAuto",
		},
		["misc"] = new[]
		{
			"SysMain", "Fax", "MapsBroker", "RetailDemo", "WMPNetworkSvc",
			"PhoneSvc", "TrkWks", "StiSvc", "wisvc", "TabletInputService",
			"RpcLocator", "SNMPTRAP", "vds", "MSDTC", "SDRSVC", "smphost", "wbengine",
			"VSS", "SwPrv", "ssh-agent", "WbioSrvc", "BDESVC", "iphlpsvc",
		},
	};

	private static readonly string[] SafeDisableList =
	{
		"WSearch", "AxInstSV", "BITS", "BrokerInfrastructure", "BDESVC",
		"AssignedAccessManagerSvc", "DiagTrack", "CryptSvc", "diagsvc", "DPS",
		"WdiServiceHost", "lfsvc", "iphlpsvc", "Netlogon", "PhoneSvc",
		"Spooler", "SessionEnv", "TermService", "UmRdpService", "SensorService",
		"SCardSvr", "ScDeviceEnum", "SCPolicySvc", "WbioSrvc", "WerSvc",
		"workfolderssvc", "XboxGipSvc",
		// curated additions
		"dmwappushservice", "PcaSvc", "SysMain", "Fax", "MapsBroker",
		"RetailDemo", "WMPNetworkSvc", "TrkWks", "StiSvc", "wisvc",
		"RemoteRegistry", "RpcLocator", "SNMPTRAP", "vds", "MSDTC",
		"SDRSVC", "smphost", "wbengine", "XblAuthManager", "XblGameSave",
		"XboxNetApiSvc", "UsoSvc", "WaaSMedicSvc",
	};

	public static IReadOnlyDictionary<string, string[]> Packs => ServicePacks;

	[DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	private static extern IntPtr OpenSCManager(string? machineName, string? databaseName, int dwDesiredAccess);

	[DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	private static extern IntPtr OpenService(IntPtr hSCManager, string lpServiceName, int dwDesiredAccess);

	[DllImport("advapi32.dll", SetLastError = true)]
	private static extern bool ChangeServiceConfig(IntPtr hService, int nServiceType, int dwStartType,
		int dwErrorControl, string? lpBinaryPathName, string? lpLoadOrderGroup, IntPtr lpdwTagId,
		string? lpDependencies, string? lpServiceStartName, string? lpPassword, string? lpDisplayName);

	[DllImport("advapi32.dll", SetLastError = true)]
	private static extern bool CloseServiceHandle(IntPtr hSCObject);

	private const int SC_MANAGER_CONNECT = 0x0001;
	private const int SERVICE_CHANGE_CONFIG = 0x0002;
	private const int SERVICE_START = 0x0010;
	private const int SERVICE_STOP = 0x0020;
	private const int SERVICE_QUERY_STATUS = 0x0004;
	private const int SERVICE_NO_CHANGE = -1;
	private const int SERVICE_AUTO_START = 0x00000002;
	private const int SERVICE_DEMAND_START = 0x00000003;
	private const int SERVICE_DISABLED = 0x00000004;

	public record SvcInfo(string Name, string DisplayName, string Status, string StartType);

	public static List<object> List()
	{
		var list = new List<object>();
		try
		{
			foreach (var sc in ServiceController.GetServices())
			{
				using (sc)
				{
					string status = sc.Status switch
					{
						ServiceControllerStatus.Running => "running",
						ServiceControllerStatus.Stopped => "stopped",
						ServiceControllerStatus.StartPending => "starting",
						ServiceControllerStatus.StopPending => "stopping",
						ServiceControllerStatus.Paused => "paused",
						_ => "other",
					};
					list.Add(new SvcInfo(sc.ServiceName, sc.DisplayName, status, QueryStartType(sc.ServiceName)));
				}
			}
		}
		catch (Exception ex)
		{
			throw new Exception("Could not enumerate services: " + ex.Message);
		}
		return list;
	}

	private static string QueryStartType(string name)
	{
		try
		{
			using var sc = new ServiceController(name);
			// ServiceController.StartType is available on modern .NET
			return sc.StartType switch
			{
				ServiceStartMode.Automatic => "auto",
				ServiceStartMode.Manual => "manual",
				ServiceStartMode.Disabled => "disabled",
				ServiceStartMode.Boot => "auto",
				ServiceStartMode.System => "auto",
				_ => "unknown",
			};
		}
		catch
		{
			return "unknown";
		}
	}

	private static void SetStartType(string name, int startType)
	{
		IntPtr scm = OpenSCManager(null, null, SC_MANAGER_CONNECT);
		if (scm == IntPtr.Zero)
			throw new Exception("Service manager unavailable (administrator required).");
		try
		{
			IntPtr h = OpenService(scm, name, SERVICE_CHANGE_CONFIG);
			if (h == IntPtr.Zero)
				throw new Exception($"Could not open '{name}' (administrator required).");
			try
			{
				if (!ChangeServiceConfig(h, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE,
					null, null, IntPtr.Zero, null, null, null, null))
					throw new Exception($"Could not change start type for '{name}'.");
			}
			finally { CloseServiceHandle(h); }
		}
		finally { CloseServiceHandle(scm); }
	}

	public static object Configure(string name, string action)
	{
		if (string.IsNullOrWhiteSpace(name))
			throw new Exception("No service name.");
		action = action.Trim().ToLowerInvariant();
		switch (action)
		{
			case "disable":
				SetStartType(name, SERVICE_DISABLED);
				return new { message = $"{name} start type set to disabled." };
			case "manual":
				SetStartType(name, SERVICE_DEMAND_START);
				return new { message = $"{name} start type set to manual." };
			case "auto":
				SetStartType(name, SERVICE_AUTO_START);
				return new { message = $"{name} start type set to automatic." };
			case "start":
			{
				using var sc = new ServiceController(name);
				if (sc.Status == ServiceControllerStatus.Running)
					return new { message = $"{name} is already running." };
				sc.Start();
				sc.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(15));
				return new { message = $"{name} started." };
			}
			case "stop":
			{
				using var sc = new ServiceController(name);
				if (sc.Status == ServiceControllerStatus.Stopped)
					return new { message = $"{name} is already stopped." };
				sc.Stop();
				sc.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(15));
				return new { message = $"{name} stopped." };
			}
			default:
				throw new Exception("Unknown service action: " + action);
		}
	}

	public static object SafeDisable(Action<int, string>? report)
	{
		return DisablePack(SafeDisableList, "Safe disable", report);
	}

	public static object DisablePack(string[] names, string label, Action<int, string>? report)
	{
		int ok = 0, fail = 0;
		for (int i = 0; i < names.Length; i++)
		{
			string svc = names[i];
			report?.Invoke(i * 100 / names.Length, $"Setting start=disabled: {svc}");
			try
			{
				SetStartType(svc, SERVICE_DISABLED);
				ok++;
			}
			catch
			{
				fail++; // not present on this SKU / access denied
			}
		}
		report?.Invoke(100, $"{label} complete.");
		string msg = $"{label}: {ok} services set to disabled start type" +
			(fail > 0 ? $" ({fail} not present/accessible)" : "") +
			". Running services stay up until stopped or rebooted.";
		return new { message = msg, okCount = ok, failCount = fail };
	}

	public static object DisableNamed(string pack, Action<int, string>? report)
	{
		if (!ServicePacks.TryGetValue(pack, out var names))
			throw new Exception($"Unknown service pack '{pack}'.");
		return DisablePack(names, pack, report);
	}
}
