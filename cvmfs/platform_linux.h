/**
 * This file is part of the CernVM File System.
 *
 * Linux specific system/library calls.
 */

#ifndef CVMFS_PLATFORM_LINUX_H_
#define CVMFS_PLATFORM_LINUX_H_

#include <pthread.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <attr/xattr.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/select.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <mntent.h>

#include <cassert>
#include <cstdio>

#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "smalloc.h"

#ifdef CVMFS_NAMESPACE_GUARD
namespace CVMFS_NAMESPACE_GUARD {
#endif


inline std::vector<std::string> platform_mountlist() {
  std::vector<std::string> result;
  FILE *fmnt = setmntent("/proc/mounts", "r");
  struct mntent *mntbuf;  // Static buffer managed by libc!
  while ((mntbuf = getmntent(fmnt)) != NULL) {
    result.push_back(mntbuf->mnt_dir);
  }
  endmntent(fmnt);
  return result;
}


// glibc < 2.11
#ifndef MNT_DETACH
#define MNT_DETACH 0x00000002
#endif
inline bool platform_umount(const char* mountpoint, const bool lazy) {
  struct stat64 mtab_info;
  int retval = lstat64(_PATH_MOUNTED, &mtab_info);
  // If /etc/mtab exists and is not a symlink to /proc/mount
  if ((retval == 0) && S_ISREG(mtab_info.st_mode)) {
    // Lock the modification on /etc/mtab against concurrent
    // crash unmount handlers
    std::string lockfile = std::string(_PATH_MOUNTED) + ".cvmfslock";
    const int fd_lockfile = open(lockfile.c_str(), O_RDONLY | O_CREAT, 0600);
    if (fd_lockfile < 0)
      return false;
    int timeout = 10;
    while ((flock(fd_lockfile, LOCK_EX | LOCK_NB) != 0) && (timeout > 0)) {
      if (errno != EWOULDBLOCK) {
        close(fd_lockfile);
        unlink(lockfile.c_str());
      }
      struct timeval wait_for;
      wait_for.tv_sec = 1;
      wait_for.tv_usec = 0;
      select(0, NULL, NULL, NULL, &wait_for);
      timeout--;
    }

    // Remove entry from /etc/mtab (create new file without entry)
    std::string mntnew = std::string(_PATH_MOUNTED) + ".cvmfstmp";
    FILE *fmntold = setmntent(_PATH_MOUNTED, "r");
    if (!fmntold) {
      flock(fd_lockfile, LOCK_UN);
      close(fd_lockfile);
      unlink(lockfile.c_str());
      return false;
    }
    FILE *fmntnew = setmntent(mntnew.c_str(), "w+");
    if (!fmntnew &&
        (chmod(mntnew.c_str(), mtab_info.st_mode) != 0) &&
        (chown(mntnew.c_str(), mtab_info.st_uid, mtab_info.st_gid) != 0))
    {
      endmntent(fmntold);
      flock(fd_lockfile, LOCK_UN);
      close(fd_lockfile);
      unlink(lockfile.c_str());
      return false;
    }
    struct mntent *mntbuf;  // Static buffer managed by libc!
    while ((mntbuf = getmntent(fmntold)) != NULL) {
      if (strcmp(mntbuf->mnt_dir, mountpoint) != 0) {
        retval = addmntent(fmntnew, mntbuf);
        if (retval != 0) {
          endmntent(fmntold);
          endmntent(fmntnew);
          unlink(mntnew.c_str());
          flock(fd_lockfile, LOCK_UN);
          close(fd_lockfile);
          unlink(lockfile.c_str());
          return false;
        }
      }
    }
    endmntent(fmntold);
    endmntent(fmntnew);
    retval = rename(mntnew.c_str(), _PATH_MOUNTED);
    flock(fd_lockfile, LOCK_UN);
    close(fd_lockfile);
    unlink(lockfile.c_str());
    if (retval != 0)
      return false;
    chmod(_PATH_MOUNTED, mtab_info.st_mode);
    chown(_PATH_MOUNTED, mtab_info.st_uid, mtab_info.st_gid);
  }

  int flags = lazy ? MNT_DETACH : 0;
  retval = umount2(mountpoint, flags);
  return retval == 0;
}


/**
 * Spinlocks are not necessarily part of pthread on all platforms.
 */
typedef pthread_spinlock_t platform_spinlock;

inline int platform_spinlock_init(platform_spinlock *lock, int pshared) {
  return pthread_spin_init(lock, pshared);
}

inline int platform_spinlock_destroy(platform_spinlock *lock) {
  return pthread_spin_destroy(lock);
}

inline int platform_spinlock_trylock(platform_spinlock *lock) {
  return pthread_spin_trylock(lock);
}


/**
 * pthread_self() is not necessarily an unsigned long.
 */
inline unsigned long platform_gettid() {
  return pthread_self();
}


inline int platform_sigwait(const int signum) {
  sigset_t sigset;
  int retval = sigemptyset(&sigset);
  assert(retval == 0);
  retval = sigaddset(&sigset, signum);
  assert(retval == 0);
  retval = sigwaitinfo(&sigset, NULL);
  return retval;
}


/**
 * Grants a PID capabilites for ptrace() usage
 *
 * @param PID  the PID of the process to be granted ptrace()-access
 *             (may be ignored)
 * @return     true when successful
 */
inline bool platform_allow_ptrace(const pid_t pid) {
#ifdef PR_SET_PTRACER
  // On Ubuntu, yama prevents all processes from ptracing other processes, even
  // when they are owned by the same user. Therefore the watchdog would not be
  // able to create a stacktrace, without this extra permission:
  const int retval = prctl(PR_SET_PTRACER, pid, 0, 0, 0);
  return (retval == 0);
#else
  // On other platforms this is currently a no-op
  return true;
#endif
}


/**
 * File system functions, ensure 64bit versions.
 */
typedef struct dirent64 platform_dirent64;

inline platform_dirent64 *platform_readdir(DIR *dirp) {
  return readdir64(dirp);
}

typedef struct stat64 platform_stat64;

inline int platform_stat(const char *path, platform_stat64 *buf) {
  return stat64(path, buf);
}

inline int platform_lstat(const char *path, platform_stat64 *buf) {
  return lstat64(path, buf);
}

inline int platform_fstat(int filedes, platform_stat64 *buf) {
  return fstat64(filedes, buf);
}

inline bool platform_getxattr(const std::string &path, const std::string &name,
                              std::string *value)
{
  int size = 0;
  void *buffer = NULL;
  int retval;
  retval = getxattr(path.c_str(), name.c_str(), buffer, size);
  if (retval > 1) {
    size = retval;
    buffer = smalloc(size);
    retval = getxattr(path.c_str(), name.c_str(), buffer, size);
  }
  if ((retval < 0) || (retval > size)) {
    free(buffer);
    return false;
  }
  if (retval > 0) {
    value->assign(static_cast<const char *>(buffer), size);
    free(buffer);
  } else {
    value->assign("");
  }
  return true;
}

inline void platform_disable_kcache(int filedes) {
  posix_fadvise(filedes, 0, 0, POSIX_FADV_RANDOM | POSIX_FADV_NOREUSE);
}

inline int platform_readahead(int filedes) {
  return readahead(filedes, 0, static_cast<size_t>(-1));
}


inline std::string platform_libname(const std::string &base_name) {
  return "lib" + base_name + ".so";
}


inline const char* platform_getexepath() {
  static char buf[PATH_MAX] = {0};
  if (strlen(buf) == 0) {
    int ret = readlink("/proc/self/exe", buf, PATH_MAX);
    if (ret > 0 && ret < (int)PATH_MAX) {
       buf[ret] = 0;
    }
  }
  return buf;
}

#ifdef CVMFS_NAMESPACE_GUARD
}
#endif

#endif  // CVMFS_PLATFORM_LINUX_H_
