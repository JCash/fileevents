#include <string.h>
#include "fileevents.h"
#include "fileevents_internal.h"

SFileEventsCreateParams::SFileEventsCreateParams()
{
	memset(this, 0, sizeof(SFileEventsCreateParams));
}

HFES fe_init(const SFileEventsCreateParams& params)//fe_callback callback, void* ctx)
{
	SFileEventSystem* hfes = new SFileEventSystem;

	hfes->m_Callback = params.m_Callback;
	hfes->m_CallbackCtx = params.m_CallbackCtx;
	hfes->m_Cancel = false;
	hfes->m_Updated = false;
	hfes->m_Verbose = params.m_Verbose;

	hfes->m_WatchCounter = 0;

	hfes->m_PlatformData = fe_platform_init(hfes);

	hfes->m_Thread = std::thread(platform_thread_run, hfes);

	return hfes;
}

void fe_close(SFileEventSystem* hfes)
{
	hfes->m_Cancel = true;
	hfes->m_Thread.join();
	fe_platform_close(hfes);
	delete hfes;
}

HFESWatchID fe_add_watch(SFileEventSystem* hfes, const char* path, uint32_t mask)
{
	if( !hfes )
		return -1;
	if( !path )
		return -1;

	std::lock_guard<std::mutex> lock(hfes->m_Lock);

	if( mask == 0 )
		mask = FE_ALL;

	// Check if it already exists, then update the mask
	int i = 0;
    for(auto &pair : hfes->m_PathsToWatch)
    {
    	if( pair.second.first == path )
    	{
    		// Only trigger an update if the mask actually changed
    		hfes->m_Updated = mask != pair.second.second;
    		pair.second.second = mask;
    		return i;
    	}

        ++i;
    }

    // never count down
	hfes->m_WatchCounter++;

	HFESWatchID watchid = (HFESWatchID)hfes->m_WatchCounter;
	hfes->m_PathsToWatch[watchid] = std::pair<std::string, uint32_t>(path, mask);

	int result = fe_platform_add_watch(hfes, watchid, path, mask);
	if( result != 0 )
	{
		hfes->m_PathsToWatch.erase(watchid);
		return 0;
	}

	hfes->m_Updated = true;

	return hfes->m_WatchCounter;
}

int32_t fe_remove_watch(SFileEventSystem* hfes, HFESWatchID id)
{
	std::lock_guard<std::mutex> lock(hfes->m_Lock);

	std::map< HFESWatchID, std::pair<std::string, uint32_t> >::iterator it = hfes->m_PathsToWatch.find( id );
	if( it != hfes->m_PathsToWatch.end() )
	{
		fe_platform_remove_watch(hfes, id);
		hfes->m_PathsToWatch.erase(it);
		hfes->m_Updated = true;
		return 0;
	}
	return -1;
}


/*
 * Rubbadubbadubb
 *
 * 5cl Light Rum
 * 15cl Mango & Passion Juice
 * 3 cl Roses Lime
 * 2 cl Grenadine
 * Garnish with Mint
 *
 *
 * (3/5)
 */


/*
 * Manatee
 *
 * 5 cl Light rum
 * 5 cl Mojito Mint
 * 10 cl Mango & Passion Juice
 * top it up with Sockerdricka
 * and ice
 * Garnish with Water Melon cubes and Mint
 *
 * (4.5/5, sweet)
 */


/*
 * Melon Crush
 * Muddle some melon cubes with Mint and some Raw Sugar
 * 3 cl Light Rum
 * 3 cl Dark Rum
 * 3 cl Mojito Mint
 * 5 cl Mango & Passion Juice
 * 1 cl Grenadine
 * top it up with Sockerdricka and ice
 *
 * (3.5/5, sweet)
 */


/*
 *
 *
 * 2 cl Light rum
 * 4 cl Dark rum
 * 3 cl Mojito Mint
 * 2 cl Grenadine
 * 4 cl Coffee
 * 15 cl Milk
 * Crush ice and put all ingredients in shaker
 */
