#include <SDL.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>

#include "OSD/Thread.h"

// Minimal platform shims to satisfy Supermodel's core logging expectations
// without pulling in the desktop OSD backends.

static void Logv(SDL_LogPriority prio, const char* fmt, va_list vl)
{
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, vl);
  buf[sizeof(buf) - 1] = '\0';
  SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, prio, "%s", buf);
}

bool ErrorLog(const char* fmt, ...)
{
  va_list vl;
  va_start(vl, fmt);
  Logv(SDL_LOG_PRIORITY_ERROR, fmt, vl);
  va_end(vl);
  return false;
}

void DebugLog(const char* fmt, ...)
{
  va_list vl;
  va_start(vl, fmt);
  Logv(SDL_LOG_PRIORITY_DEBUG, fmt, vl);
  va_end(vl);
}

void InfoLog(const char* fmt, ...)
{
  va_list vl;
  va_start(vl, fmt);
  Logv(SDL_LOG_PRIORITY_INFO, fmt, vl);
  va_end(vl);
}

// --- Threading wrappers using SDL primitives ---

static const char* g_lastThreadError = nullptr;

void CThread::Sleep(UINT32 ms)
{
  SDL_Delay(static_cast<Uint32>(ms));
}

UINT32 CThread::GetTicks()
{
  return SDL_GetTicks();
}

CThread* CThread::CreateThread(const std::string& name, ThreadStart start, void* startParam)
{
  SDL_Thread* t = SDL_CreateThread(
    [](void* p) -> int {
      auto pair = static_cast<std::pair<ThreadStart, void*>*>(p);
      ThreadStart fn = pair->first;
      void* data = pair->second;
      int rc = fn(data);
      delete pair;
      return rc;
    },
    name.c_str(),
    new std::pair<ThreadStart, void*>(start, startParam));

  if (!t) {
    g_lastThreadError = SDL_GetError();
    return nullptr;
  }

  return new CThread(name, t);
}

CSemaphore* CThread::CreateSemaphore(UINT32 initVal)
{
  SDL_sem* sem = SDL_CreateSemaphore(initVal);
  if (!sem) {
    g_lastThreadError = SDL_GetError();
    return nullptr;
  }
  return new CSemaphore(sem);
}

CCondVar* CThread::CreateCondVar()
{
  SDL_cond* cv = SDL_CreateCond();
  if (!cv) {
    g_lastThreadError = SDL_GetError();
    return nullptr;
  }
  return new CCondVar(cv);
}

CMutex* CThread::CreateMutex()
{
  SDL_mutex* mtx = SDL_CreateMutex();
  if (!mtx) {
    g_lastThreadError = SDL_GetError();
    return nullptr;
  }
  return new CMutex(mtx);
}

const char* CThread::GetLastError()
{
  return g_lastThreadError ? g_lastThreadError : "";
}

CThread::CThread(const std::string& name, void* impl)
  : m_name(name), m_impl(impl)
{
}

CThread::~CThread()
{
  auto t = static_cast<SDL_Thread*>(m_impl);
  if (t) {
    // Let caller decide if thread was joined; nothing to do.
  }
}

const std::string& CThread::GetName() const { return m_name; }
UINT32 CThread::GetId() { return m_impl ? static_cast<UINT32>(SDL_GetThreadID(static_cast<SDL_Thread*>(m_impl))) : 0; }
int CThread::Wait()
{
  int status = 0;
  if (auto t = static_cast<SDL_Thread*>(m_impl)) {
    SDL_WaitThread(t, &status);
    m_impl = nullptr;
  }
  return status;
}

CSemaphore::CSemaphore(void* impl) : m_impl(impl) {}
CSemaphore::~CSemaphore()
{
  if (m_impl) {
    SDL_DestroySemaphore(static_cast<SDL_sem*>(m_impl));
    m_impl = nullptr;
  }
}
UINT32 CSemaphore::GetValue()
{
  return m_impl ? SDL_SemValue(static_cast<SDL_sem*>(m_impl)) : 0;
}
bool CSemaphore::Wait()
{
  return m_impl ? SDL_SemWait(static_cast<SDL_sem*>(m_impl)) == 0 : false;
}
bool CSemaphore::Post()
{
  return m_impl ? SDL_SemPost(static_cast<SDL_sem*>(m_impl)) == 0 : false;
}

CCondVar::CCondVar(void* impl) : m_impl(impl) {}
CCondVar::~CCondVar()
{
  if (m_impl) {
    SDL_DestroyCond(static_cast<SDL_cond*>(m_impl));
    m_impl = nullptr;
  }
}
bool CCondVar::Wait(CMutex* mutex)
{
  if (!m_impl || !mutex || !mutex->m_impl)
    return false;
  return SDL_CondWait(static_cast<SDL_cond*>(m_impl), static_cast<SDL_mutex*>(mutex->m_impl)) == 0;
}
bool CCondVar::Signal()
{
  return m_impl ? SDL_CondSignal(static_cast<SDL_cond*>(m_impl)) == 0 : false;
}
bool CCondVar::SignalAll()
{
  return m_impl ? SDL_CondBroadcast(static_cast<SDL_cond*>(m_impl)) == 0 : false;
}

CMutex::CMutex(void* impl) : m_impl(impl) {}
CMutex::~CMutex()
{
  if (m_impl) {
    SDL_DestroyMutex(static_cast<SDL_mutex*>(m_impl));
    m_impl = nullptr;
  }
}
bool CMutex::Lock()
{
  return m_impl ? SDL_LockMutex(static_cast<SDL_mutex*>(m_impl)) == 0 : false;
}
bool CMutex::Unlock()
{
  return m_impl ? SDL_UnlockMutex(static_cast<SDL_mutex*>(m_impl)) == 0 : false;
}
