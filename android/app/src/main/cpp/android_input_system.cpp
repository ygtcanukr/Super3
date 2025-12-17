#include "android_input_system.h"

#include <algorithm>
#include <cstring>

AndroidInputSystem::AndroidInputSystem()
  : CInputSystem("android-sdl"),
    m_keys(SDL_NUM_SCANCODES, 0)
{
}

bool AndroidInputSystem::InitializeSystem()
{
  std::fill(m_keys.begin(), m_keys.end(), 0);
  m_fingerHeldDir.clear();
  m_fingerHeldKey.clear();
  m_pulseUntilMs.clear();
  return true;
}

void AndroidInputSystem::SetKey(SDL_Scancode sc, bool down)
{
  if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_NUM_SCANCODES)
    return;
  m_keys[static_cast<size_t>(sc)] = down ? 1 : 0;
}

void AndroidInputSystem::PulseKey(SDL_Scancode sc, uint32_t durationMs)
{
  if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_NUM_SCANCODES)
    return;
  SetKey(sc, true);
  m_pulseUntilMs[sc] = SDL_GetTicks() + durationMs;
}

bool AndroidInputSystem::Poll()
{
  const uint32_t now = SDL_GetTicks();
  for (auto it = m_pulseUntilMs.begin(); it != m_pulseUntilMs.end();)
  {
    if (now >= it->second)
    {
      SetKey(static_cast<SDL_Scancode>(it->first), false);
      it = m_pulseUntilMs.erase(it);
    }
    else
    {
      ++it;
    }
  }
  return true;
}

void AndroidInputSystem::HandleEvent(const SDL_Event& ev)
{
  switch (ev.type)
  {
    case SDL_KEYDOWN:
      HandleKeyEvent(ev.key, true);
      break;
    case SDL_KEYUP:
      HandleKeyEvent(ev.key, false);
      break;
    case SDL_CONTROLLERBUTTONDOWN:
      HandleControllerButtonEvent(ev.cbutton, true);
      break;
    case SDL_CONTROLLERBUTTONUP:
      HandleControllerButtonEvent(ev.cbutton, false);
      break;
    case SDL_FINGERDOWN:
      HandleTouch(ev.tfinger, true);
      break;
    case SDL_FINGERUP:
      HandleTouch(ev.tfinger, false);
      break;
    case SDL_FINGERMOTION:
      HandleTouchMotion(ev.tfinger);
      break;
    default:
      break;
  }
}

void AndroidInputSystem::HandleKeyEvent(const SDL_KeyboardEvent& key, bool down)
{
  SetKey(key.keysym.scancode, down);
}

void AndroidInputSystem::HandleControllerButtonEvent(const SDL_ControllerButtonEvent& btn, bool down)
{
  // Basic defaults:
  // - Start: Start1
  // - Right shoulder: Coin1
  // - Back: TestA
  // - D-pad: menu navigation
  // - A/B/X/Y: map to common keys for future bindings
  SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
  switch (btn.button)
  {
    case SDL_CONTROLLER_BUTTON_START: sc = SDL_SCANCODE_1; break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: sc = SDL_SCANCODE_5; break;
    case SDL_CONTROLLER_BUTTON_BACK: sc = SDL_SCANCODE_F2; break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: sc = SDL_SCANCODE_UP; break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: sc = SDL_SCANCODE_DOWN; break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: sc = SDL_SCANCODE_LEFT; break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: sc = SDL_SCANCODE_RIGHT; break;
    case SDL_CONTROLLER_BUTTON_A: sc = SDL_SCANCODE_Z; break;
    case SDL_CONTROLLER_BUTTON_B: sc = SDL_SCANCODE_X; break;
    case SDL_CONTROLLER_BUTTON_X: sc = SDL_SCANCODE_C; break;
    case SDL_CONTROLLER_BUTTON_Y: sc = SDL_SCANCODE_V; break;
    default: break;
  }

  if (sc == SDL_SCANCODE_UNKNOWN)
    return;

  // For coin/start/test, prefer a pulse to avoid repeating while held.
  if (down && (sc == SDL_SCANCODE_1 || sc == SDL_SCANCODE_5 || sc == SDL_SCANCODE_F1 || sc == SDL_SCANCODE_F2))
    PulseKey(sc, 120);
  else
    SetKey(sc, down);
}

void AndroidInputSystem::HandleTouch(const SDL_TouchFingerEvent& tf, bool down)
{
  const float x = tf.x;
  const float y = tf.y;

  // Tap zones (momentary):
  // - Bottom-left: Coin (KEY_5)
  // - Bottom-right: Start (KEY_1)
  // - Top-left: Service (KEY_F1)
  // - Top-right: Test (KEY_F2)
  if (down)
  {
    if (x < 0.25f && y > 0.75f) { PulseKey(SDL_SCANCODE_5, 120); return; }
    if (x > 0.75f && y > 0.75f) { PulseKey(SDL_SCANCODE_1, 120); return; }
    if (x < 0.25f && y < 0.25f) { PulseKey(SDL_SCANCODE_F1, 120); return; }
    if (x > 0.75f && y < 0.25f) { PulseKey(SDL_SCANCODE_F2, 120); return; }
  }

  // Held throttle/brake zone (right-middle): hold to accelerate/brake.
  // - Upper half: throttle (KEY_W)
  // - Lower half: brake (KEY_S)
  const bool inPedalZone = (x > 0.55f && y >= 0.25f && y <= 0.90f);
  if (inPedalZone)
  {
    if (!down)
    {
      auto it = m_fingerHeldKey.find(tf.fingerId);
      if (it != m_fingerHeldKey.end())
      {
        SetKey(it->second, false);
        m_fingerHeldKey.erase(it);
      }
      return;
    }

    SDL_Scancode pedal = (y < 0.575f) ? SDL_SCANCODE_W : SDL_SCANCODE_S;
    m_fingerHeldKey[tf.fingerId] = pedal;
    SetKey(pedal, true);
    return;
  }

  // Held D-pad zone (left-middle): press one of the arrow keys based on direction.
  const bool inDpadZone = (x < 0.45f && y >= 0.35f && y <= 0.75f);
  if (!inDpadZone)
    return;

  if (!down)
  {
    auto it = m_fingerHeldDir.find(tf.fingerId);
    if (it != m_fingerHeldDir.end())
    {
      SetKey(it->second, false);
      m_fingerHeldDir.erase(it);
    }
    return;
  }

  // Determine direction relative to center of the d-pad zone.
  const float cx = 0.225f;
  const float cy = 0.55f;
  const float dx = x - cx;
  const float dy = y - cy;

  SDL_Scancode dir = SDL_SCANCODE_UNKNOWN;
  if (std::abs(dx) > std::abs(dy))
    dir = (dx < 0.0f) ? SDL_SCANCODE_LEFT : SDL_SCANCODE_RIGHT;
  else
    dir = (dy < 0.0f) ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN;

  if (dir != SDL_SCANCODE_UNKNOWN)
  {
    m_fingerHeldDir[tf.fingerId] = dir;
    SetKey(dir, true);
  }
}

void AndroidInputSystem::HandleTouchMotion(const SDL_TouchFingerEvent& tf)
{
  // Update held d-pad direction.
  auto it = m_fingerHeldDir.find(tf.fingerId);
  if (it == m_fingerHeldDir.end())
  {
    // Update held pedal if this finger is a pedal touch.
    auto itKey = m_fingerHeldKey.find(tf.fingerId);
    if (itKey == m_fingerHeldKey.end())
      return;

    // Release previous pedal and re-evaluate based on new Y position.
    SetKey(itKey->second, false);
    m_fingerHeldKey.erase(itKey);
    HandleTouch(tf, true);
    return;
  }

  // Release previous direction and compute a new one.
  SetKey(it->second, false);
  m_fingerHeldDir.erase(it);
  HandleTouch(tf, true);
}

SDL_Scancode AndroidInputSystem::ScancodeFromSupermodelKeyName(const char* keyName) const
{
  if (!keyName || !keyName[0])
    return SDL_SCANCODE_UNKNOWN;

  // Fast-path common names used by our defaults.
  if (strcmp(keyName, "UP") == 0) return SDL_SCANCODE_UP;
  if (strcmp(keyName, "DOWN") == 0) return SDL_SCANCODE_DOWN;
  if (strcmp(keyName, "LEFT") == 0) return SDL_SCANCODE_LEFT;
  if (strcmp(keyName, "RIGHT") == 0) return SDL_SCANCODE_RIGHT;
  if (strcmp(keyName, "RETURN") == 0) return SDL_SCANCODE_RETURN;
  if (strcmp(keyName, "ESCAPE") == 0) return SDL_SCANCODE_ESCAPE;
  if (strcmp(keyName, "SPACE") == 0) return SDL_SCANCODE_SPACE;

  // Digits and letters match SDL key names.
  if ((keyName[0] >= '0' && keyName[0] <= '9') && keyName[1] == '\0')
    return SDL_GetScancodeFromName(keyName);
  if ((keyName[0] >= 'A' && keyName[0] <= 'Z') && keyName[1] == '\0')
    return SDL_GetScancodeFromName(keyName);

  // Function keys.
  if (keyName[0] == 'F' && keyName[1] >= '1' && keyName[1] <= '9')
    return SDL_GetScancodeFromName(keyName);

  // Try SDL's name lookup as a fallback (works for many names, albeit with different casing).
  return SDL_GetScancodeFromName(keyName);
}

int AndroidInputSystem::GetKeyIndex(const char* keyName)
{
  const SDL_Scancode sc = ScancodeFromSupermodelKeyName(keyName);
  return (sc == SDL_SCANCODE_UNKNOWN) ? -1 : (int)sc;
}

const char* AndroidInputSystem::GetKeyName(int keyIndex)
{
  if (keyIndex <= SDL_SCANCODE_UNKNOWN || keyIndex >= SDL_NUM_SCANCODES)
    return "";
  return SDL_GetScancodeName(static_cast<SDL_Scancode>(keyIndex));
}

bool AndroidInputSystem::IsKeyPressed(int /*kbdNum*/, int keyIndex)
{
  if (keyIndex <= SDL_SCANCODE_UNKNOWN || keyIndex >= SDL_NUM_SCANCODES)
    return false;
  return m_keys[static_cast<size_t>(keyIndex)] != 0;
}
