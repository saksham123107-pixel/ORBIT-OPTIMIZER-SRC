using System.Windows;
using PrimexGlass.Services;

namespace PrimexGlass;

public partial class App : Application
{
	// KeyAuth init state, read by MainWindow before any usage.
	public static Task<bool>? InitTask { get; private set; }
	public static bool AuthInitOk { get; private set; }
	public static bool AuthOffline { get; private set; }
	public static bool AuthVersionMismatch { get; private set; }
	public static string AuthInitError { get; private set; } = "";

	protected override void OnStartup(StartupEventArgs e)
	{
		base.OnStartup(e);

		// Start init immediately but NEVER block window creation on the
		// network — the UI shows instantly with a splash, and usage stays
		// gated until init resolves (see auth:getStatus pending flag).
		InitTask = KeyAuthService.InitAsync().ContinueWith(t =>
		{
			bool ok = false;
			try { ok = t.Result; } catch { }
			AuthInitOk = ok;
			AuthOffline = !KeyAuthService.InitNetworkOk && !KeyAuthService.VersionMismatch;
			AuthVersionMismatch = KeyAuthService.VersionMismatch;
			AuthInitError = KeyAuthService.InitError;
			return ok;
		});
	}
}
