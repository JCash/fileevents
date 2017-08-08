#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <thread>
#include <chrono>

#include "fileevents.h"


static void _print_flags(EFileEvents flags)
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

static int fileevents_callback(const char* path, EFileEvents flags, void* ctx)
{
	(void)ctx;
	printf("%s ", path);
	_print_flags(flags);
	return 0;
}

static bool forever = true;

static void sighandler(int sig)
{
	(void)sig;
	forever = false;
}

static void print_usage()
{
	printf("Usage: events [-h] [<paths>]\n");
	printf("    Monitors one or more paths for file events.\n");
	printf("    If no paths are specified, if monitors the current directory.\n");
	printf("\n");
	printf("    -h, --help  Prints this message\n");
	printf("\n");
}

int main(int argc, char** argv)
{
	if( argc == 2 )
	{
		if( strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 )
		{
			print_usage();
			return 0;
		}
	}

	signal(SIGABRT, &sighandler);
	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);
	SFileEventsCreateParams params;
	params.m_Callback = fileevents_callback;
	HFES hfes = fe_init(params);

	if( argc < 2 )
	{
		fe_add_watch(hfes, ".", FE_ALL);
	}
	else
	{
		for( int i = 1; i < argc; ++i )
		{
			const char* path = argv[i];
			struct stat sb;

			if( stat(path, &sb) == -1 )
			{
				printf("Path does not exist: '%s'\n", path);
				fe_close(hfes);
				return 1;
			}

			HFESWatchID id = fe_add_watch(hfes, path, FE_ALL);
			if( id < 0 )
			{
				printf("Failed to watch path: '%s'\n", path);
				fe_close(hfes);
				return 1;
			}
		}
	}

	while(forever)
	{
		std::this_thread::sleep_for( std::chrono::milliseconds(1000) );
	}

	fe_close(hfes);
	return 0;
}

