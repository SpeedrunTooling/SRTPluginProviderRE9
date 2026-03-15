#ifndef SRTPLUGINRE9_RENDER_H
#define SRTPLUGINRE9_RENDER_H

#include <cstdint>
#include <d3d12.h>

bool LoadTextureFromMemory(const uint8_t *rawRGBAImageData, const uint32_t rawRGBAImageWidth, const uint32_t rawRGBAImageHeight, ID3D12Device *d3d_device, D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_handle, ID3D12Resource **out_tex_resource);

void DestroyTexture(ID3D12Resource **tex_resources);

#endif
