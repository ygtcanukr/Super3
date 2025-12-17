#pragma once

#include <GLES3/gl3.h>

#include "Graphics/IRender3D.h"

// Tiny GLES-backed IRender3D implementation used as a stepping stone:
// - Verifies the 3D renderer hook is being called from the Real3D/GPU path.
// - Does NOT implement Model 3 Real3D yet.
class GlesStubRender3D final : public IRender3D
{
public:
  bool Init(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned) override { return true; }
  void AttachMemory(const uint32_t*, const uint32_t*, const uint32_t*, const uint32_t*, const uint16_t*) override {}
  void UploadTextures(unsigned, unsigned, unsigned, unsigned, unsigned) override {}
  void SetStepping(int) override {}
  void SetSunClamp(bool) override {}
  void SetSignedShade(bool) override {}
  float GetLosValue(int) override { return 0.f; }

  void BeginFrame() override;
  void RenderFrame() override;
  void EndFrame() override {}

private:
  bool m_inited = false;
  GLuint m_program = 0;
  GLuint m_vao = 0;
  GLuint m_vbo = 0;
};

