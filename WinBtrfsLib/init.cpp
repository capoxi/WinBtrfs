/* WinBtrfsLib/init.cpp
 * filesystem initialization code
 *
 * WinBtrfs
 * Copyright (c) 2011 Justin Gottula
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "init.h"
#include <cstdio>
#include <vector>
#include <boost/detail/endian.hpp>
#include "btrfs_system.h"
#include "chunktree_parser.h"
#include "dokan_callbacks.h"
#include "fstree_parser.h"
#include "roottree_parser.h"
#include "util.h"
#include "WinBtrfsLib.h"

namespace WinBtrfsLib
{
	DOKAN_OPERATIONS btrfsOperations = {
		&btrfsCreateFile,
		&btrfsOpenDirectory,
		&btrfsCreateDirectory,
		&btrfsCleanup,
		&btrfsCloseFile,
		&btrfsReadFile,
		&btrfsWriteFile,
		&btrfsFlushFileBuffers,
		&btrfsGetFileInformation,
		&btrfsFindFiles,
		NULL, // FindFilesWithPattern
		&btrfsSetFileAttributes,
		&btrfsSetFileTime,
		&btrfsDeleteFile,
		&btrfsDeleteDirectory,
		&btrfsMoveFile,
		&btrfsSetEndOfFile,
		&btrfsSetAllocationSize,
		&btrfsLockFile,
		&btrfsUnlockFile,
		&btrfsGetDiskFreeSpace,
		&btrfsGetVolumeInformation,
		&btrfsUnmount,
		&btrfsGetFileSecurity,
		&btrfsSetFileSecurity
	};

	extern VolumeInfo volumeInfo;
	extern BtrfsObjID mountedSubvol;

	std::vector<const wchar_t *> *devicePaths;

	void init()
	{
		DWORD error;

#ifndef BOOST_DETAIL_ENDIAN_HPP
#error You need to include <boost/detail/endian.hpp>!
#endif

#ifndef BOOST_LITTLE_ENDIAN
#pragma message("Warning: support for non-little-endian architectures is untested!")
		printf("firstTasks: warning: support for non-little-endian architectures is untested!\n");
#endif

		allocateBlockReaders();

		if ((error = setupBigDokanLock()) != 0)
		{
			printf("firstTasks: failed to setup the Big Dokan Lock! (GetLastError: %d)\n", error);

			exit(1);
		}

		if ((error = loadSBs(!volumeInfo.noDump)) != 0)
		{
			cleanUp();
			exit(1);
		}
	
		if (verifyDevices() != 0)
		{
			cleanUp();
			exit(1);
		}

		loadSBChunks(!volumeInfo.noDump);

		if (!volumeInfo.noDump) parseChunkTree(CTOP_DUMP_TREE);
		parseChunkTree(CTOP_LOAD);

		if (!volumeInfo.noDump) parseRootTree(RTOP_DUMP_TREE, NULL, NULL);

		if (!volumeInfo.noDump)
		{
			parseFSTree(OBJID_FS_TREE, FSOP_DUMP_TREE, NULL, NULL, NULL, NULL, NULL);
			parseRootTree(RTOP_DUMP_SUBVOLS, NULL, NULL);
		}

		if (volumeInfo.dumpOnly)
		{
			cleanUp();
			exit(0);
		}
	
		/* aesthetic line break */
		if (!volumeInfo.noDump)
			printf("\n");

		if (!volumeInfo.useSubvolID && !volumeInfo.useSubvolName)
		{
			int result;
			if ((result = parseRootTree(RTOP_DEFAULT_SUBVOL, NULL, NULL)) != 0)
			{
				printf("firstTasks: could not find the default subvolume!\n");
				cleanUp();
				exit(1);
			}
		}
		else if (volumeInfo.useSubvolName)
		{
			if (strcmp(volumeInfo.subvolName, "default") == 0)
				mountedSubvol = OBJID_FS_TREE;
			else
			{
				int result;
				if ((result = parseRootTree(RTOP_GET_SUBVOL_ID, volumeInfo.subvolName, &mountedSubvol)) != 0)
				{
					printf("firstTasks: could not find the subvolume named '%s'!\n",
						volumeInfo.subvolName);
					cleanUp();
					exit(1);
				}
			}
		}
		else
		{
			bool subvolExists;
		
			/* we can be fairly certain that these constraints will always hold */
			/* not enforcing these would allow the user to mount non-FS-type trees, which is definitely bad */
			if ((volumeInfo.subvolID > (BtrfsObjID)0 && volumeInfo.subvolID < (BtrfsObjID)0x100) ||
				(volumeInfo.subvolID > (BtrfsObjID)-0x100 && volumeInfo.subvolID < (BtrfsObjID)-1))
			{
				printf("firstTasks: %I64u is an impossible subvolume ID!\n",
					(unsigned __int64)volumeInfo.subvolID);
				cleanUp();
				exit(1);
			}
		
			parseRootTree(RTOP_SUBVOL_EXISTS, &volumeInfo.subvolID, &subvolExists);

			if (!subvolExists)
			{
				printf("firstTasks: could not find the subvolume with ID %I64u!\n",
					(unsigned __int64)volumeInfo.subvolID);
				cleanUp();
				exit(1);
			}

			mountedSubvol = (volumeInfo.subvolID == (BtrfsObjID)0 ? OBJID_FS_TREE : volumeInfo.subvolID);
		}
		
		PDOKAN_OPTIONS dokanOptions = (PDOKAN_OPTIONS)malloc(sizeof(DOKAN_OPTIONS));

		dokanOptions->Version = 600;
		dokanOptions->ThreadCount = 1;			// eventually set this to zero or a user-definable count
		dokanOptions->Options = 0;				// look into this later
		dokanOptions->GlobalContext = 0;		// use this later if necessary
		dokanOptions->MountPoint = volumeInfo.mountPoint;

		int dokanResult = DokanMain(dokanOptions, &btrfsOperations);
		free(dokanOptions);

		cleanUp();
		dokanError(dokanResult);

		exit(dokanResult);
	}
}
