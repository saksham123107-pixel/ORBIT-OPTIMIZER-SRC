using System.Runtime.InteropServices;
using System.Windows.Input;
using System.Windows.Interop;

namespace PrimexGlass.Services;

// Global hotkey: polls one bound key, runs an action on press.
// WPF Key -> virtual-key code via KeyInterop (same VK space as Win32).
public sealed class HotkeyManager : IDisposable
{
	[DllImport("user32.dll")]
	private static extern short GetAsyncKeyState(int vKey);

	private Thread? _thread;
	private volatile bool _running;
	private int _vk;

	public bool IsRunning => _running;
	public string BoundName { get; private set; } = "";
	public event Action<string>? Log;

	public void Start(Key key, Action action) =>
		StartVk(KeyInterop.VirtualKeyFromKey(key), key.ToString(), action);

	public void StartVk(int vk, Action action) =>
		StartVk(vk, $"VK 0x{vk:X}", action);

	public void StartVk(int vk, string name, Action action)
	{
		Stop();
		_vk = vk;
		BoundName = name;
		_running = true;
		_thread = new Thread(() =>
		{
			while (_running)
			{
				try
				{
					if ((GetAsyncKeyState(_vk) & 0x8000) != 0)
					{
						Log?.Invoke($"Hotkey {BoundName} pressed â€” running.");
						action();
						while ((GetAsyncKeyState(_vk) & 0x8000) != 0)
							Thread.Sleep(10);
					}
				}
				catch { }
				Thread.Sleep(50);
			}
		})
		{ IsBackground = true };
		_thread.Start();
	}

	public void Stop()
	{
		_running = false;
		_thread = null;
	}

	public void Dispose() => Stop();
}
