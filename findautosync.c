#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <libzfs.h>
#include <libzfs_impl.h>
#include <fcntl.h>
#include <string.h>
#include <sys/nvpair.h>

static int cleanup_fd = -1;
libzfs_handle_t *zlibh;

typedef struct snap_data {
        time_t          creation_time;
        const char      *last_snapshot;
} snap_data_t;

static int
ndmp_match_autosync_name(zfs_handle_t *zhp, void *arg)
{
        snap_data_t *sd = (snap_data_t *)arg;
        time_t snap_creation;
        nvlist_t *propval = NULL;
        nvlist_t *userprops = NULL;

        if (zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT) {
                if ((userprops = zfs_get_user_props(zhp)) != NULL) {
                        if (nvlist_lookup_nvlist(userprops,
                            "nms:autosyncmark", &propval) == 0
                            || propval != NULL) {
                                snap_creation = (time_t)zfs_prop_get_int(zhp,
                                    ZFS_PROP_CREATION);
                                if (snap_creation > sd->creation_time) {
                                        strncpy((char *) sd->last_snapshot,
                                            zfs_get_name(zhp), ZFS_MAX_DATASET_NAME_LEN);
					sd->creation_time = snap_creation;
                                }
                        }
                }
        }
	zfs_close(zhp);
        return (0);
}

int
ndmp_find_latest_autosync(zfs_handle_t *zhp, void *arg)
{
        int err;
        snap_data_t *si = (snap_data_t *)arg;

	err = zfs_iter_dependents(zhp, B_FALSE, ndmp_match_autosync_name, (void *)si);
        if (err) {
                fprintf(stdout, "Trying to find AutoSync zfs_iter_snapshots: %d",
                    err);
                si->last_snapshot = '\0';
                return (-1);
        } else {
                fprintf(stdout, "Found most recent AutoSync -> [%s]\n",
                    si->last_snapshot);
        }
        return (0);
}

int main(int argc, char *argv[])
{
	zfs_handle_t *zhp;
	snap_data_t si;
	int rv;
	char snapname[ZFS_MAX_DATASET_NAME_LEN] = {'\0'};

	if ((zlibh = libzfs_init()) == NULL) {
		fprintf(stderr, "Failed to initialize libzfs (%d)\n", errno);
		exit (-1);
	}

	if((zhp = zfs_open(zlibh, "data", ZFS_TYPE_DATASET)) != NULL) {
                si.creation_time = (time_t)0;
                si.last_snapshot = snapname;
                if ((rv = ndmp_find_latest_autosync(zhp, (void *) &si)) != 0) {
                        fprintf(stdout,
                            "backup_dataset_create: Find AutoSync failed (err=%d): %s",
                            errno, libzfs_error_description(zlibh));
                        zfs_close(zhp);
                        return (rv);
                }
                zfs_close(zhp);
                if (strlen(snapname) == 0) {
                        fprintf(stdout, "Auto-Sync mode "
                            "but not found - backup stopped");
                        return (-1);
                }
        }

	libzfs_fini(zlibh);
}
