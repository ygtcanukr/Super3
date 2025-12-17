#pragma once

#include <SDL.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Inputs/InputSystem.h"
#include "Inputs/Input.h"

// Minimal SDL-backed input system for Android.
// - Uses SDL scancodes as key indices.
// - Provides simple touch controls by synthesizing "keyboard" scancode presses.
class AndroidInputSystem final : public CInputSystem
{
public:
  AndroidInputSystem();
  ~AndroidInputSystem() override = default;

  // Called from the SDL event loop (main thread).
  void HandleEvent(const SDL_Event& ev);

  bool InitializeSystem() override;
  int GetKeyIndex(const char* keyName) override;
  const char* GetKeyName(int keyIndex) override;
  bool IsKeyPressed(int /*kbdNum*/, int keyIndex) override;

  int GetMouseAxisValue(int /*mseNum*/, int /*axisNum*/) override { return 0; }
  int GetMouseWheelDir(int /*mseNum*/) override { return 0; }
  bool IsMouseButPressed(int /*mseNum*/, int /*butNum*/) override { return false; }

  int GetJoyAxisValue(int /*joyNum*/, int /*axisNum*/) override { return 0; }
  bool IsJoyPOVInDir(int /*joyNum*/, int /*povNum*/, int /*povDir*/) override { return false; }
  bool IsJoyButPressed(int /*joyNum*/, int /*butNum*/) override { return false; }
  bool ProcessForceFeedbackCmd(int /*joyNum*/, int /*axisNum*/, ForceFeedbackCmd /*ffCmd*/) override { return false; }

  int GetNumKeyboards() override { return 1; }
  int GetNumMice() override { return 0; }
  int GetNumJoysticks() override { return 0; }

  const KeyDetails* GetKeyDetails(int /*kbdNum*/) override { return nullptr; }
  const MouseDetails* GetMouseDetails(int /*mseNum*/) override { return nullptr; }
  const JoyDetails* GetJoyDetails(int /*joyNum*/) override { return nullptr; }

  bool Poll() override;

  void SetMouseVisibility(bool /*visible*/) override {}
  void GrabMouse() override {}
  void UngrabMouse() override {}

private:
  void SetKey(SDL_Scancode sc, bool down);
  void PulseKey(SDL_Scancode sc, uint32_t durationMs);

  void HandleKeyEvent(const SDL_KeyboardEvent& key, bool down);
  void HandleControllerButtonEvent(const SDL_ControllerButtonEvent& btn, bool down);
  void HandleTouch(const SDL_TouchFingerEvent& tf, bool down);
  void HandleTouchMotion(const SDL_TouchFingerEvent& tf);

  SDL_Scancode ScancodeFromSupermodelKeyName(const char* keyName) const;

  std::vector<uint8_t> m_keys; // indexed by SDL_Scancode
  std::unordered_map<SDL_FingerID, SDL_Scancode> m_fingerHeldDir;
  std::unordered_map<SDL_FingerID, SDL_Scancode> m_fingerHeldKey;
  std::unordered_map<SDL_Scancode, uint32_t> m_pulseUntilMs;
};
