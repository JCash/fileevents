#include <thread>
#include <mutex>
#include <map>
#include <string>

struct SPlatformData;

struct SFileEventSystem
{
	std::thread m_Thread;
	fe_callback m_Callback;
	void*		m_CallbackCtx;

	SPlatformData* m_PlatformData;

	// Lock for the data below
	std::mutex 	m_Lock;
	int64_t 	m_WatchCounter;

	std::map< HFESWatchID, std::pair<std::string, uint32_t> > m_PathsToWatch;

	// Have the path list changed?
	bool m_Updated;
	bool m_Cancel;
	bool m_Verbose;

	bool _padding[5];
};

SPlatformData* fe_platform_init(const SFileEventSystem* hfes);
void fe_platform_close(const SFileEventSystem* hfes);
void platform_thread_run(SFileEventSystem* hfes);
int fe_platform_add_watch(const SFileEventSystem* hfes, HFESWatchID watchid, const char* path, uint32_t mask);
void fe_platform_remove_watch(const SFileEventSystem* hfes, HFESWatchID watchid);

// Used by the unit test to check if the system is up and running yet
bool fe_is_running(const SFileEventSystem* hfes);
