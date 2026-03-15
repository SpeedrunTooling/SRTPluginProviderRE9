#ifndef SRTPLUGINRE9_VTABLESCANNER_H
#define SRTPLUGINRE9_VTABLESCANNER_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <expected>
#include <string>

namespace SRTPluginRE9::Hook
{
	struct DX12VTableAddresses
	{
		void *present;
		void *resizeBuffers;
		void *executeCommandLists;
	};

	struct DInput8VTableAddresses
	{
		void *getDeviceState;
		void *getDeviceData;
	};

	struct VTableAddresses : DX12VTableAddresses, DInput8VTableAddresses
	{
	};

	/// @brief Resolves D3D12/DXGI vtable addresses by creating temporary COM
	/// objects using APIs that other hooking frameworks don't monitor.
	///
	/// REFramework hooks IDXGIFactory2::CreateSwapChainForHwnd (vtable index 15)
	/// but does NOT hook IDXGIFactory2::CreateSwapChainForComposition (index 24).
	/// By using CreateSwapChainForComposition, we create a real swap chain that
	/// is completely invisible to REFramework.
	class VTableResolver
	{
	public:
		/// @brief Resolve Present, ResizeBuffers, and ExecuteCommandLists addresses
		/// by creating temporary COM objects via unmonitored API paths.
		[[nodiscard]] static auto Resolve()
		    -> std::expected<VTableAddresses, std::string>;

	private:
		[[nodiscard]] static auto ResolveDX12VTables()
		    -> std::expected<DX12VTableAddresses, std::string>;

		[[nodiscard]] static auto ResolveDInput8VTables()
		    -> std::expected<DInput8VTableAddresses, std::string>;
	};
}

#endif
