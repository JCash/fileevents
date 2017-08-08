
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <set>
#if defined(_MSC_VER)
	#include <direct.h>
	#define PATH_MAX _MAX_PATH
#else
	#include <unistd.h>
#endif
#include "greatest.h"
#include "fileevents.h"
#include "fileevents_internal.h"

#include <thread>
#include <chrono>


struct SOperation
{
	uint64_t 	m_Flags;
	std::string	m_Path;
};

static void _print_flags(uint64_t flags)
{
	if( flags & FE_CREATED ) 		printf("Created, ");
	if( flags & FE_REMOVED ) 		printf("Removed, ");
	if( flags & FE_RENAMED ) 		printf("Renamed, ");
	if( flags & FE_MODIFIED ) 		printf("Modified, ");
	if( flags & FE_ATTRIBUTE ) 		printf("Attribute, ");
	if( flags & FE_IS_FILE ) 		printf("IsFile, ");
	if( flags & FE_IS_DIR ) 		printf("IsDir, ");
	if( flags & FE_IS_SYMLINK ) 	printf("IsSymlink, ");
	printf("\n");
}

class FileEventsTest
{
	std::vector<SOperation>	m_PerformedOperations;
	std::vector<SOperation>	m_CallbackOperations;
	std::map<HFESWatchID, std::string>	m_WatchList;

	std::set<std::string>	m_CreatedFiles;
	std::set<std::string>	m_CreatedFolders;
	std::string				m_Cwd;

	HFES m_FileEvents;

public:
	void SetUp()
	{
		char cwd[PATH_MAX];
		::getcwd(cwd, sizeof(cwd));
		m_Cwd = cwd;

		SFileEventsCreateParams params;
		params.m_Callback = FileEventsTest::FileCallback;
		params.m_CallbackCtx = this;
		params.m_Verbose = true;
		m_FileEvents = fe_init(params);
	}

	void TearDown()
	{
		if( m_FileEvents )
		{
			fe_close(m_FileEvents);
			fflush(stdout);
		}
		m_FileEvents = 0;

		for(const auto path : m_CreatedFiles)
		{
			printf("Cleaning up %s\n", path.c_str());
			fflush(stdout);
			remove(path.c_str());
		}

		for(const auto path : m_CreatedFolders)
		{
			printf("Cleaning up %s\n", path.c_str());
			fflush(stdout);
			remove(path.c_str());
		}

		wait(100);
	}

	const char* getcwd() const
	{
		return m_Cwd.c_str();
	}

	size_t get_num_callback_operations() const
	{
		return m_CallbackOperations.size();
	}

	HFESWatchID add_watch(const char* path, uint32_t flags)
	{
		HFESWatchID id = fe_add_watch(m_FileEvents, path, flags);
		if( id < 0 )
			return id;
		m_WatchList[id] = path;
		return id;
	}

	int32_t remove_watch(HFESWatchID id)
	{
		int32_t result = fe_remove_watch(m_FileEvents, id);
		auto it = m_WatchList.find(id);
		if( it != m_WatchList.end() )
			m_WatchList.erase(it);
		return result;
	}

	bool wait_running()
	{
		printf("Waiting:");
		int i = 0;
		while(i++ < 100)
		{
			if(fe_is_running(m_FileEvents))
			{
				printf("started running after %d waits\n", i);
				return true;
			}
			printf(".");
			wait(100);
		}
		return false;
	}

	std::string get_path(const char* filename)
	{
		return m_Cwd + std::string("/") + filename;
	}

	uint32_t create_file(const char* path)
	{
		SOperation op;
		op.m_Flags = FE_CREATED | FE_IS_FILE;
		op.m_Path = path;
		m_PerformedOperations.push_back(op);

		FILE* file = fopen(path, "wb");
		if( !file )
		{
			fprintf(stderr, "Failed creating file %s\n", path);
			fclose(file);
			return 0;
		}

		printf("Created file %s\n", path);
		fflush(stdout);
		m_CreatedFiles.insert(path);
		return 1;
	}

	uint32_t modify_file(const char* path, uint8_t* data, size_t size)
	{
		SOperation op;
		op.m_Flags = FE_MODIFIED | FE_IS_FILE;
		op.m_Path = path;
		m_PerformedOperations.push_back(op);

		FILE* file = fopen(path, "wb");
		if( file )
		{
			fwrite(data, size, 1, file);
			fclose(file);
			return 0;
		}
		return 1;
	}

	uint32_t remove_file(const char* path)
	{
		SOperation op;
		op.m_Flags = FE_REMOVED | FE_IS_FILE;
		op.m_Path = path;
		m_PerformedOperations.push_back(op);

		auto it = m_CreatedFiles.find(path);
		if( it != m_CreatedFiles.end() )
			m_CreatedFiles.erase(it);

		return remove(path) == 0 ? 0 : 1;
	}

	uint32_t rename_file(const char* path, const char* destpath)
	{
		SOperation op1;
		op1.m_Flags = FE_RENAMED | FE_IS_FILE;
		op1.m_Path = path;
		m_PerformedOperations.push_back(op1);
		SOperation op2;
		op2.m_Flags = FE_RENAMED | FE_IS_FILE;
		op2.m_Path = destpath;
		m_PerformedOperations.push_back(op2);

		auto it = m_CreatedFiles.find(path);
		if( it != m_CreatedFiles.end() )
			m_CreatedFiles.erase(it);
		m_CreatedFiles.insert(path);

		return rename(path, destpath) == 0 ? 0 : 1;
	}

	void wait(uint64_t ms)
	{
		std::this_thread::sleep_for( std::chrono::milliseconds(ms) );
	}

	int validate()
	{
		const size_t minlen = std::min( m_PerformedOperations.size(), m_CallbackOperations.size() );

		for( size_t i = 0; i < minlen; ++i )
		{
			const SOperation& performed = m_PerformedOperations[i];
			const SOperation& callback = m_CallbackOperations[i];

			ASSERT_STR_EQ( performed.m_Path.c_str(), callback.m_Path.c_str() );
			ASSERT_EQ( performed.m_Flags, callback.m_Flags );
			if( performed.m_Flags != callback.m_Flags )
			{
				printf("wanted flags: ");
				_print_flags( performed.m_Flags );

				printf("callback flags: ");
				_print_flags( callback.m_Flags );
				return -1;
			}
		}
		ASSERT_EQ( m_PerformedOperations.size(), m_CallbackOperations.size() );
		return 0;
	}

	static int FileCallback( const char* path, EFileEvents flags, void* _ctx )
	{
		FileEventsTest* ctx = (FileEventsTest*)_ctx;
		SOperation op;
		op.m_Flags = flags;
		op.m_Path = path;
		ctx->m_CallbackOperations.push_back(op);
		printf("path %s   flags %0X\n", path, flags);
		return 0;
	}
};

#define FETEST()		printf("%s:\n", __FUNCTION__); \
						FileEventsTest fe; \
						fe.SetUp();

#define FETESTEND()		fe.TearDown(); \
						return fe.validate();

/*
TEST FE_CreateDestroy()
{
	FETEST();
	HFESWatchID wid = fe.add_watch(fe.getcwd(), 0);
	ASSERT_NE( 0, wid );

	int32_t result = fe.remove_watch(wid);
	ASSERT_EQ( 0, result );
	FETESTEND();
}

TEST FE_NoWatchers()
{
	FETEST();
	fe.create_file( fe.get_path("foobar1.txt").c_str() );

	ASSERT_EQ(0, fe.get_num_callback_operations());
	fe.TearDown();
	PASS();
}

TEST FE_EventAfterWatchWasRemoved()
{
	FETEST();
	// Tests
	HFESWatchID wid = fe.add_watch(fe.getcwd(), 0);
	ASSERT_NE( 0, wid );

	int32_t result = fe.remove_watch(wid);
	ASSERT_EQ( 0, result );

	fe.create_file( fe.get_path("foobar2.txt").c_str() );

	ASSERT_EQ(0, fe.get_num_callback_operations());
	fe.TearDown();
	PASS();
}
*/

TEST FE_OneCreateEvent()
{
	FETEST();
	// Tests
	HFESWatchID wid = fe.add_watch(fe.getcwd(), 0);
	ASSERT_NE( 0, wid );

	fe.wait_running();

	fe.create_file( fe.get_path("foobar3.txt").c_str() );

	fe.wait(3500);
	ASSERT_EQ(1, fe.get_num_callback_operations());

	int32_t result = fe.remove_watch(wid);
	ASSERT_EQ( 0, result );

	FETESTEND();
}

static SUITE(the_suite) {
    //RUN_TEST(FE_CreateDestroy);
    //RUN_TEST(FE_NoWatchers);
    //RUN_TEST(FE_EventAfterWatchWasRemoved);
    RUN_TEST(FE_OneCreateEvent);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();      /* command-line arguments, initialization. */
    RUN_SUITE(the_suite);
    GREATEST_MAIN_END();        /* display results */
}
