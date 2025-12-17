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

// Simple SDL audio backend: mix 4-channel Model3 audio down to stereo and queue
// to the SDL device.
static SDL_AudioDeviceID g_audioDevice = 0;
static SDL_AudioSpec g_audioSpec = {};

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
  desired.freq = 44100;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2; // stereo out
  desired.samples = 1024;
  desired.callback = [](void*, Uint8*, int) {
    // Using SDL_QueueAudio so callback is a no-op.
  };

  g_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &g_audioSpec, 0);
  if (g_audioDevice == 0) {
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "SDL_OpenAudioDevice failed: %s", SDL_GetError());
    return false;
  }

  SDL_PauseAudioDevice(g_audioDevice, 0);
  return true;
}

bool OutputAudio(unsigned numSamples, INT16* leftFront, INT16* rightFront, INT16* leftRear, INT16* rightRear, bool flipStereo)
{
  if (!g_audioEnabled || g_audioDevice == 0)
    return true;

  // Downmix quad to stereo.
  std::vector<INT16> stereo;
  stereo.reserve(numSamples * 2);
  for (unsigned i = 0; i < numSamples; ++i) {
    int lf = leftFront ? leftFront[i] : 0;
    int rf = rightFront ? rightFront[i] : 0;
    int lr = leftRear ? leftRear[i] : 0;
    int rr = rightRear ? rightRear[i] : 0;
    int l = Clamp16((lf + lr) / 2);
    int r = Clamp16((rf + rr) / 2);
    if (flipStereo) std::swap(l, r);
    stereo.push_back(static_cast<INT16>(l));
    stereo.push_back(static_cast<INT16>(r));
  }

  if (SDL_QueueAudio(g_audioDevice, stereo.data(), stereo.size() * sizeof(INT16)) != 0) {
    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "SDL_QueueAudio failed: %s", SDL_GetError());
  }

  if (g_audioCallback && g_audioData) {
    g_audioCallback(g_audioData);
  }
  return true;
}

void CloseAudio()
{
  if (g_audioDevice != 0) {
    SDL_CloseAudioDevice(g_audioDevice);
    g_audioDevice = 0;
  }
}
