#include <SDL.h>
#include <vector>
#include <algorithm>

#include "OSD/Outputs.h"
#include "OSD/Audio.h"
#include "OSD/Video.h"
#include "Types.h"
#include "Game.h"

// Minimal OSD implementations for Android/SDL.

const char* COutputs::s_outputNames[] = {
  "pause", "LampStart", "LampView1", "LampView2", "LampView3", "LampView4",
  "LampLeader", "RawDrive", "RawLamps", "BillDigit1", "BillDigit2",
  "BillDigit3", "BillDigit4", "BillDigit5",
};

COutputs::COutputs()
{
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    m_first[i] = false;
    m_values[i] = 0;
  }
}

// Base class plumbing normally provided by OSD implementation.
COutputs::~COutputs() = default;

const Game& COutputs::GetGame() const { return m_game; }

const char* COutputs::GetOutputName(EOutputs /*output*/) { return "stub"; }
EOutputs COutputs::GetOutputByName(const char* /*name*/) { return OutputUnknown; }
void COutputs::SetGame(const Game& game) { m_game = game; }
UINT8 COutputs::GetValue(EOutputs output) const { return m_values[output]; }

void COutputs::SetValue(EOutputs output, UINT8 value)
{
  if (output < 0 || output >= NUM_OUTPUTS)
    return;
  UINT8 prev = m_values[output];
  m_values[output] = value;
  m_first[output] = true;
  SendOutput(output, prev, value);
}

bool COutputs::HasValue(EOutputs output)
{
  return (output >= 0 && output < NUM_OUTPUTS) ? m_first[output] : false;
}

// Provide a trivial Outputs subclass to satisfy pure virtual.
class CStubOutputs : public COutputs
{
public:
  bool Initialize() override { return true; }
  void Attached() override {}
protected:
  void SendOutput(EOutputs /*output*/, UINT8 /*prevValue*/, UINT8 /*value*/) override {}
};

// Audio/video shim
static AudioCallbackFPtr g_audioCallback = nullptr;
static void* g_audioData = nullptr;
static bool g_audioEnabled = true;
static Game::AudioTypes g_audioType = Game::AudioTypes::STEREO_LR;

void SetAudioCallback(AudioCallbackFPtr cb, void* data)
{
  g_audioCallback = cb;
  g_audioData = data;
}

void SetAudioEnabled(bool enabled) { g_audioEnabled = enabled; }
void SetAudioType(Game::AudioTypes type) { g_audioType = type; }

bool BeginFrameVideo() { return true; }
void EndFrameVideo() {}

// SDL audio backend:
// - Mix 4-channel Model3 output down to stereo (S16).
// - Resample/convert to the opened device format via SDL_AudioStream.
// - Feed a ring buffer consumed by the SDL audio callback (prevents choppy audio
//   due to small queue bursts or timing jitter).
static SDL_AudioDeviceID g_audioDevice = 0;
static SDL_AudioSpec g_audioSpec = {};
static SDL_AudioStream* g_audioStream = nullptr;
static std::vector<uint8_t> g_ring;
static uint32_t g_ringRead = 0;
static uint32_t g_ringWrite = 0;
static SDL_mutex* g_audioMutex = nullptr;
static uint32_t g_bytesPerSecond = 0;
static uint32_t g_targetFillBytes = 0;

static uint32_t RingUsed()
{
  if (g_ring.empty())
    return 0;
  if (g_ringWrite >= g_ringRead)
    return g_ringWrite - g_ringRead;
  return static_cast<uint32_t>(g_ring.size()) - (g_ringRead - g_ringWrite);
}

static uint32_t RingFree()
{
  if (g_ring.empty())
    return 0;
  // Keep one byte empty to distinguish full vs empty.
  return static_cast<uint32_t>(g_ring.size() - 1) - RingUsed();
}

static void RingDropOldest(uint32_t bytes)
{
  if (g_ring.empty())
    return;
  const uint32_t used = RingUsed();
  if (bytes >= used)
  {
    g_ringRead = g_ringWrite;
    return;
  }
  g_ringRead = (g_ringRead + bytes) % static_cast<uint32_t>(g_ring.size());
}

static void RingWrite(const uint8_t* data, uint32_t bytes)
{
  if (g_ring.empty() || bytes == 0)
    return;

  // If we don't have room, drop oldest audio to keep latency bounded.
  const uint32_t free = RingFree();
  if (bytes > free)
    RingDropOldest(bytes - free);

  uint32_t remaining = bytes;
  while (remaining > 0)
  {
    const uint32_t toEnd = static_cast<uint32_t>(g_ring.size()) - g_ringWrite;
    const uint32_t chunk = (remaining < toEnd) ? remaining : toEnd;
    std::memcpy(&g_ring[g_ringWrite], data + (bytes - remaining), chunk);
    g_ringWrite = (g_ringWrite + chunk) % static_cast<uint32_t>(g_ring.size());
    remaining -= chunk;
  }
}

static uint32_t RingRead(uint8_t* out, uint32_t bytes)
{
  if (g_ring.empty() || bytes == 0)
    return 0;
  const uint32_t used = RingUsed();
  const uint32_t toRead = (bytes < used) ? bytes : used;

  uint32_t remaining = toRead;
  while (remaining > 0)
  {
    const uint32_t toEnd = static_cast<uint32_t>(g_ring.size()) - g_ringRead;
    const uint32_t chunk = (remaining < toEnd) ? remaining : toEnd;
    std::memcpy(out + (toRead - remaining), &g_ring[g_ringRead], chunk);
    g_ringRead = (g_ringRead + chunk) % static_cast<uint32_t>(g_ring.size());
    remaining -= chunk;
  }
  return toRead;
}

static INT16 Clamp16(int sample)
{
  if (sample > INT16_MAX) return INT16_MAX;
  if (sample < INT16_MIN) return INT16_MIN;
  return static_cast<INT16>(sample);
}

bool OpenAudio(const Util::Config::Node&)
{
  if (g_audioDevice != 0)
    return true;

  SDL_AudioSpec desired{};
  desired.freq = 48000; // common Android output rate; SDL may still choose a different one
  desired.format = AUDIO_S16SYS;
  desired.channels = 2; // stereo out
  desired.samples = 4096;
  desired.callback = [](void*, Uint8* stream, int len) {
    if (!g_audioEnabled || g_ring.empty())
    {
      std::memset(stream, 0, static_cast<size_t>(len));
      return;
    }
    if (g_audioMutex)
      SDL_LockMutex(g_audioMutex);
    const uint32_t got = RingRead(stream, static_cast<uint32_t>(len));
    const uint32_t usedAfterRead = RingUsed();
    if (g_audioMutex)
      SDL_UnlockMutex(g_audioMutex);
    if (got < static_cast<uint32_t>(len))
      std::memset(stream + got, 0, static_cast<size_t>(len - got));

    // When audio is running low, wake the emulator sound thread (if enabled).
    // This matches the desktop "unsync'd sound board thread" design.
    if (g_audioCallback && g_audioData && g_targetFillBytes != 0 && usedAfterRead < (g_targetFillBytes / 2))
      g_audioCallback(g_audioData);
  };

  // Allow SDL to pick a workable device format/rate; we'll convert via SDL_AudioStream.
  g_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &g_audioSpec,
                                      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
  if (g_audioDevice == 0) {
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "SDL_OpenAudioDevice failed: %s", SDL_GetError());
    return false;
  }

  g_bytesPerSecond = static_cast<uint32_t>(g_audioSpec.freq) *
                     static_cast<uint32_t>(g_audioSpec.channels) *
                     static_cast<uint32_t>(SDL_AUDIO_BITSIZE(g_audioSpec.format) / 8);
  // Keep ~250ms buffered; ring holds 2s to bound worst-case jitter.
  g_targetFillBytes = g_bytesPerSecond / 4;

  g_ring.assign((g_bytesPerSecond * 2) + 1, 0);
  g_ringRead = 0;
  g_ringWrite = 0;

  g_audioMutex = SDL_CreateMutex();
  if (!g_audioMutex) {
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "SDL_CreateMutex failed: %s", SDL_GetError());
    SDL_CloseAudioDevice(g_audioDevice);
    g_audioDevice = 0;
    g_ring.clear();
    return false;
  }

  // Convert from core mix format (S16 stereo @ 44100) to device format.
  g_audioStream = SDL_NewAudioStream(AUDIO_S16SYS, 2, 44100, g_audioSpec.format, g_audioSpec.channels, g_audioSpec.freq);
  if (!g_audioStream) {
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "SDL_NewAudioStream failed: %s", SDL_GetError());
    SDL_CloseAudioDevice(g_audioDevice);
    g_audioDevice = 0;
    g_ring.clear();
    SDL_DestroyMutex(g_audioMutex);
    g_audioMutex = nullptr;
    return false;
  }

  SDL_PauseAudioDevice(g_audioDevice, 0);
  return true;
}

bool OutputAudio(unsigned numSamples, INT16* leftFront, INT16* rightFront, INT16* leftRear, INT16* rightRear, bool flipStereo)
{
  if (!g_audioEnabled || g_audioDevice == 0)
    return true;

  // Downmix quad to stereo (S16 @ 44100 Hz).
  static thread_local std::vector<INT16> stereo;
  stereo.clear();
  stereo.resize(static_cast<size_t>(numSamples) * 2);
  for (unsigned i = 0; i < numSamples; ++i) {
    int lf = leftFront ? leftFront[i] : 0;
    int rf = rightFront ? rightFront[i] : 0;
    int lr = leftRear ? leftRear[i] : 0;
    int rr = rightRear ? rightRear[i] : 0;
    int l = Clamp16((lf + lr) / 2);
    int r = Clamp16((rf + rr) / 2);
    if (flipStereo) std::swap(l, r);
    stereo[(i * 2) + 0] = static_cast<INT16>(l);
    stereo[(i * 2) + 1] = static_cast<INT16>(r);
  }

  if (g_audioStream)
  {
    SDL_AudioStreamPut(g_audioStream, stereo.data(), static_cast<int>(stereo.size() * sizeof(INT16)));
    // Pull converted data in chunks into the ring buffer.
    uint8_t tmp[8192];
    while (true)
    {
      const int got = SDL_AudioStreamGet(g_audioStream, tmp, static_cast<int>(sizeof(tmp)));
      if (got <= 0)
        break;
      if (g_audioMutex)
        SDL_LockMutex(g_audioMutex);
      RingWrite(tmp, static_cast<uint32_t>(got));
      if (g_audioMutex)
        SDL_UnlockMutex(g_audioMutex);
    }
  }
  // Tell the core whether the audio buffer is "full enough" (used by the
  // unsync'd sound-board thread to decide whether to run extra frames).
  bool fullEnough = true;
  if (g_audioMutex)
    SDL_LockMutex(g_audioMutex);
  if (g_targetFillBytes != 0)
    fullEnough = RingUsed() >= g_targetFillBytes;
  if (g_audioMutex)
    SDL_UnlockMutex(g_audioMutex);
  return fullEnough;
}

void CloseAudio()
{
  if (g_audioStream) {
    SDL_FreeAudioStream(g_audioStream);
    g_audioStream = nullptr;
  }
  if (g_audioMutex) {
    SDL_DestroyMutex(g_audioMutex);
    g_audioMutex = nullptr;
  }
  g_ring.clear();
  g_bytesPerSecond = 0;
  g_targetFillBytes = 0;
  if (g_audioDevice != 0) {
    SDL_CloseAudioDevice(g_audioDevice);
    g_audioDevice = 0;
  }
}
