#include "android_input_system.h"

#include <algorithm>
#include <cstring>

AndroidInputSystem::AndroidInputSystem()
  : CInputSystem("android-sdl"),
    m_keys(SDL_NUM_SCANCODES, 0)
{
}

void AndroidInputSystem::ApplyConfig(const Util::Config::Node& config)
{
  auto get = [&](const char* key, const char* fallback) -> std::string {
    return config[key].ValueAsDefault<std::string>(fallback);
  };

  auto keySc = [&](const std::string& mapping, SDL_Scancode fallback) -> SDL_Scancode {
    if (mapping.empty()) return fallback;
    SDL_Scancode sc = ParseFirstKeyboardScancode(mapping, *this);
    return (sc == SDL_SCANCODE_UNKNOWN) ? fallback : sc;
  };

  m_touchCoin.a = keySc(get("InputCoin1", "KEY_5"), SDL_SCANCODE_5);
  m_touchStart.a = keySc(get("InputStart1", "KEY_1"), SDL_SCANCODE_1);
  m_touchService.a = keySc(get("InputServiceA", "KEY_F1"), SDL_SCANCODE_F1);
  m_touchTest.a = keySc(get("InputTestA", "KEY_F2"), SDL_SCANCODE_F2);

  // For left/right, we press both joy and steering if they differ, to keep menus and driving working.
  const SDL_Scancode joyLeft = keySc(get("InputJoyLeft", "KEY_LEFT"), SDL_SCANCODE_LEFT);
  const SDL_Scancode joyRight = keySc(get("InputJoyRight", "KEY_RIGHT"), SDL_SCANCODE_RIGHT);
  const SDL_Scancode steerLeft = keySc(get("InputSteeringLeft", "KEY_LEFT"), SDL_SCANCODE_LEFT);
  const SDL_Scancode steerRight = keySc(get("InputSteeringRight", "KEY_RIGHT"), SDL_SCANCODE_RIGHT);

  m_touchJoyUp.a = keySc(get("InputJoyUp", "KEY_UP"), SDL_SCANCODE_UP);
  m_touchJoyDown.a = keySc(get("InputJoyDown", "KEY_DOWN"), SDL_SCANCODE_DOWN);

  m_touchJoyLeft = {joyLeft, (joyLeft != steerLeft) ? steerLeft : SDL_SCANCODE_UNKNOWN};
  m_touchJoyRight = {joyRight, (joyRight != steerRight) ? steerRight : SDL_SCANCODE_UNKNOWN};
  m_touchSteerLeft = {steerLeft, (steerLeft != joyLeft) ? joyLeft : SDL_SCANCODE_UNKNOWN};
  m_touchSteerRight = {steerRight, (steerRight != joyRight) ? joyRight : SDL_SCANCODE_UNKNOWN};

  m_touchThrottle.a = keySc(get("InputAccelerator", "KEY_W"), SDL_SCANCODE_W);
  m_touchBrake.a = keySc(get("InputBrake", "KEY_S"), SDL_SCANCODE_S);
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

void AndroidInputSystem::SetKeys(const DualScancode& sc, bool down)
{
  if (sc.a != SDL_SCANCODE_UNKNOWN) SetKey(sc.a, down);
  if (sc.b != SDL_SCANCODE_UNKNOWN) SetKey(sc.b, down);
}

void AndroidInputSystem::PulseKeys(const DualScancode& sc, uint32_t durationMs)
{
  if (sc.a != SDL_SCANCODE_UNKNOWN) PulseKey(sc.a, durationMs);
  if (sc.b != SDL_SCANCODE_UNKNOWN) PulseKey(sc.b, durationMs);
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
  DualScancode sc;
  switch (btn.button)
  {
    case SDL_CONTROLLER_BUTTON_START: sc = m_touchStart; break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: sc = m_touchCoin; break;
    case SDL_CONTROLLER_BUTTON_BACK: sc = m_touchTest; break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: sc = m_touchJoyUp; break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: sc = m_touchJoyDown; break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: sc = m_touchJoyLeft; break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: sc = m_touchJoyRight; break;
    case SDL_CONTROLLER_BUTTON_A: sc = {SDL_SCANCODE_Z, SDL_SCANCODE_UNKNOWN}; break;
    case SDL_CONTROLLER_BUTTON_B: sc = {SDL_SCANCODE_X, SDL_SCANCODE_UNKNOWN}; break;
    case SDL_CONTROLLER_BUTTON_X: sc = {SDL_SCANCODE_C, SDL_SCANCODE_UNKNOWN}; break;
    case SDL_CONTROLLER_BUTTON_Y: sc = {SDL_SCANCODE_V, SDL_SCANCODE_UNKNOWN}; break;
    default: break;
  }

  if (sc.a == SDL_SCANCODE_UNKNOWN && sc.b == SDL_SCANCODE_UNKNOWN)
    return;

  // For coin/start/test, prefer a pulse to avoid repeating while held.
  if (down && (sc.a == m_touchStart.a || sc.a == m_touchCoin.a || sc.a == m_touchService.a || sc.a == m_touchTest.a))
    PulseKeys(sc, 120);
  else
    SetKeys(sc, down);
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
    if (x < 0.25f && y > 0.75f) { PulseKeys(m_touchCoin, 120); return; }
    if (x > 0.75f && y > 0.75f) { PulseKeys(m_touchStart, 120); return; }
    if (x < 0.25f && y < 0.25f) { PulseKeys(m_touchService, 120); return; }
    if (x > 0.75f && y < 0.25f) { PulseKeys(m_touchTest, 120); return; }
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
        SetKeys(it->second, false);
        m_fingerHeldKey.erase(it);
      }
      return;
    }

    DualScancode pedal = (y < 0.575f) ? m_touchThrottle : m_touchBrake;
    m_fingerHeldKey[tf.fingerId] = pedal;
    SetKeys(pedal, true);
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
      SetKeys(it->second, false);
      m_fingerHeldDir.erase(it);
    }
    return;
  }

  // Determine direction relative to center of the d-pad zone.
  const float cx = 0.225f;
  const float cy = 0.55f;
  const float dx = x - cx;
  const float dy = y - cy;

  DualScancode dir;
  if (std::abs(dx) > std::abs(dy))
    dir = (dx < 0.0f) ? m_touchJoyLeft : m_touchJoyRight;
  else
    dir = (dy < 0.0f) ? m_touchJoyUp : m_touchJoyDown;

  if (dir.a != SDL_SCANCODE_UNKNOWN || dir.b != SDL_SCANCODE_UNKNOWN)
  {
    m_fingerHeldDir[tf.fingerId] = dir;
    SetKeys(dir, true);
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
    SetKeys(itKey->second, false);
    m_fingerHeldKey.erase(itKey);
    HandleTouch(tf, true);
    return;
  }

  // Release previous direction and compute a new one.
  SetKeys(it->second, false);
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

SDL_Scancode AndroidInputSystem::ParseFirstKeyboardScancode(const std::string& mapping, const AndroidInputSystem& self)
{
  // mapping examples:
  // - "KEY_F2"
  // - "KEY_RIGHT,JOY1_XAXIS_POS"
  // - "KEY_ALT+KEY_R"
  // - "!KEY_ALT+KEY_P"
  std::string s = mapping;
  size_t start = 0;
  while (start < s.size())
  {
    // Find next token boundary.
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == ',' || s[start] == '+'))
      start++;
    if (start >= s.size()) break;

    size_t end = start;
    while (end < s.size() && s[end] != ' ' && s[end] != '\t' && s[end] != ',' && s[end] != '+')
      end++;

    std::string tok = s.substr(start, end - start);
    if (!tok.empty() && tok[0] == '!')
      tok.erase(tok.begin());

    if (tok.rfind("KEY_", 0) == 0 && tok.size() > 4)
    {
      const std::string keyName = tok.substr(4);
      return self.ScancodeFromSupermodelKeyName(keyName.c_str());
    }

    start = end;
  }

  return SDL_SCANCODE_UNKNOWN;
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
